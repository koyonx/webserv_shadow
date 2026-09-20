#include "webserv/http/RequestParser.hpp"

#include "webserv/StringUtil.hpp"

#include <climits>
#include <cstddef>
#include <sstream>

namespace webserv {
namespace http {

// ---------- character class helpers ----------

static bool isTChar(unsigned char c)
{
	if (('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || ('0' <= c && c <= '9')) {
		return true;
	}
	switch (c) {
		case '!': case '#': case '$': case '%': case '&': case '\'':
		case '*': case '+': case '-': case '.': case '^': case '_':
		case '`': case '|': case '~':
			return true;
	}
	return false;
}

static bool isValidTargetChar(unsigned char c)
{
	if (c < 0x21)  return false;
	if (c > 0x7E)  return false;
	switch (c) {
		case '<': case '>': case '{': case '}':
		case '|': case '\\': case '^': case '`': case '"':
			return false;
	}
	return true;
}

// field-vchar (RFC 7230 §3.2.6). Allows obs-text (0x80..0xFF) for
// backwards compat with older clients.
static bool isFieldValueByte(unsigned char c)
{
	if (c == '\t' || c == ' ') return true;
	if (c >= 0x21 && c <= 0x7E) return true;
	if (c >= 0x80)              return true;   // obs-text
	return false;
}

static bool hexDigit(char c, int &out)
{
	if ('0' <= c && c <= '9') { out = c - '0';       return true; }
	if ('a' <= c && c <= 'f') { out = c - 'a' + 10;  return true; }
	if ('A' <= c && c <= 'F') { out = c - 'A' + 10;  return true; }
	return false;
}

static bool percentDecode(const std::string &in,
                          std::string       &out,
                          bool               rejectNul)
{
	out.clear();
	out.reserve(in.size());
	for (std::size_t i = 0; i < in.size();) {
		char c = in[i];
		if (c == '%') {
			if (i + 2 >= in.size()) return false;
			int h1 = 0, h2 = 0;
			if (!hexDigit(in[i + 1], h1) || !hexDigit(in[i + 2], h2)) return false;
			unsigned char b = static_cast<unsigned char>((h1 << 4) | h2);
			if (rejectNul && b == 0) return false;
			out += static_cast<char>(b);
			i   += 3;
		} else {
			out += c;
			i   += 1;
		}
	}
	return true;
}

static void splitPathQuery(const std::string &target,
                           std::string       &rawPath,
                           std::string       &rawQuery)
{
	std::string::size_type q = target.find('?');
	if (q == std::string::npos) {
		rawPath  = target;
		rawQuery.clear();
	} else {
		rawPath  = target.substr(0, q);
		rawQuery = target.substr(q + 1);
	}
}

static bool parseAbsoluteForm(const std::string &target,
                              std::string       &authority,
                              std::string       &pathQuery)
{
	std::string::size_type ss = target.find("://");
	if (ss == std::string::npos || ss == 0) return false;
	std::string scheme = target.substr(0, ss);
	for (std::size_t i = 0; i < scheme.size(); ++i) {
		char c = scheme[i];
		bool ok =
			(('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) ||
			(i > 0 && ('0' <= c && c <= '9')) ||
			(i > 0 && (c == '+' || c == '-' || c == '.'));
		if (!ok) return false;
	}
	std::string             rest  = target.substr(ss + 3);
	std::string::size_type  slash = rest.find('/');
	if (slash == std::string::npos) {
		authority = rest;
		pathQuery = "/";
	} else {
		authority = rest.substr(0, slash);
		pathQuery = rest.substr(slash);
	}
	return !authority.empty();
}

// ---------- RequestParser ----------

RequestParser::RequestParser()
	: m_phase(kPhaseRequestLine),
	  m_req(),
	  m_line(),
	  m_maxLine(8192),
	  m_maxHeaderBytes(32 * 1024),
	  m_maxHeaderCount(100),
	  m_maxBody(1024UL * 1024UL),
	  m_headerBytesSoFar(0),
	  m_headerCountSoFar(0),
	  m_bodyBytesRead(0),
	  m_chunkRemaining(0),
	  m_seenHost(false),
	  m_seenContentLength(false),
	  m_seenTransferEncoding(false),
	  m_status(0),
	  m_errmsg(NULL),
	  m_bodySink(NULL),
	  m_bodyEndFired(false)
{}

void RequestParser::reset()
{
	m_phase = kPhaseRequestLine;
	m_req.clear();
	m_line.clear();
	m_headerBytesSoFar      = 0;
	m_headerCountSoFar      = 0;
	m_bodyBytesRead         = 0;
	m_chunkRemaining        = 0;
	m_seenHost              = false;
	m_seenContentLength     = false;
	m_seenTransferEncoding  = false;
	m_status = 0;
	m_errmsg = NULL;
	m_bodySink     = NULL;
	m_bodyEndFired = false;
}

const Request &RequestParser::request()      const { return m_req; }
int            RequestParser::errorStatus()  const { return m_status; }
const char    *RequestParser::errorMessage() const { return m_errmsg; }
void           RequestParser::setMaxRequestLine(std::size_t b) { m_maxLine = b; }
void           RequestParser::setMaxHeaderBytes(std::size_t b) { m_maxHeaderBytes = b; }
void           RequestParser::setMaxHeaderCount(std::size_t n) { m_maxHeaderCount = n; }
void           RequestParser::setMaxBodySize(std::size_t b)    { m_maxBody = b; }
void           RequestParser::setBodySink(IBodyChunkSink *s)   { m_bodySink = s; }
bool           RequestParser::headersReady()             const
{
	// Any body phase — CL, chunked, trailer — or kPhaseDone means
	// the request-line + all header fields have been fully consumed.
	// kPhaseError is intentionally excluded (caller can still tell
	// from errorStatus()).
	return m_phase >= kPhaseBodyCL && m_phase <= kPhaseDone;
}

void RequestParser::setError(int status, const char *msg)
{
	m_phase  = kPhaseError;
	m_status = status;
	m_errmsg = msg;
}

ParseResult RequestParser::feed(const char *data,
                                std::size_t len,
                                std::size_t &consumed)
{
	consumed = 0;
	if (m_phase == kPhaseError) return kParseError;
	if (m_phase == kPhaseDone)  return kParseComplete;

	while (consumed < len) {
		std::size_t before = consumed;
		if (m_phase == kPhaseRequestLine) {
			std::size_t c = 0;
			ParseResult r = feedRequestLine(data + consumed, len - consumed, c);
			consumed += c;
			if (r != kParseNeedMore) return r;
			if (m_phase != kPhaseHeaders) return kParseNeedMore;
		}
		if (m_phase == kPhaseHeaders) {
			std::size_t c = 0;
			ParseResult r = feedHeaders(data + consumed, len - consumed, c);
			consumed += c;
			if (r != kParseNeedMore) return r;
			if (m_phase < kPhaseBodyCL) return kParseNeedMore;
			// fall through into body phase in same feed()
		}
		if (m_phase >= kPhaseBodyCL && m_phase <= kPhaseBodyTrailer) {
			std::size_t c = 0;
			ParseResult r = feedBody(data + consumed, len - consumed, c);
			consumed += c;
			if (r != kParseNeedMore) return r;
			return kParseNeedMore;
		}
		if (consumed == before) {
			return kParseNeedMore;
		}
	}
	return kParseNeedMore;
}

// -------- request-line --------

ParseResult RequestParser::feedRequestLine(const char *data,
                                           std::size_t len,
                                           std::size_t &consumed)
{
	consumed = 0;
	while (consumed < len) {
		char c = data[consumed++];
		if (c == '\n') {
			if (!m_line.empty() && m_line[m_line.size() - 1] == '\r') {
				m_line.erase(m_line.size() - 1);
			}
			if (m_line.empty()) {
				continue;   // leading empty line permitted
			}
			if (!parseAccumulatedRequestLine()) return kParseError;
			m_line.clear();
			m_phase = kPhaseHeaders;
			return kParseNeedMore;
		}
		if (static_cast<unsigned char>(c) < 0x20 && c != '\r') {
			setError(400, "control character in request line");
			return kParseError;
		}
		if (static_cast<unsigned char>(c) == 0x7F) {
			setError(400, "DEL character in request line");
			return kParseError;
		}
		m_line += c;
		if (m_line.size() > m_maxLine) {
			setError(414, "request-line too long");
			return kParseError;
		}
	}
	return kParseNeedMore;
}

bool RequestParser::parseAccumulatedRequestLine()
{
	std::string::size_type sp1 = m_line.find(' ');
	if (sp1 == std::string::npos) { setError(400, "request-line missing SP after method"); return false; }
	std::string::size_type sp2 = m_line.find(' ', sp1 + 1);
	if (sp2 == std::string::npos) { setError(400, "request-line missing SP after target"); return false; }
	if (m_line.find(' ', sp2 + 1) != std::string::npos) {
		setError(400, "extra whitespace in request-line"); return false;
	}

	m_req.method = m_line.substr(0, sp1);
	m_req.target = m_line.substr(sp1 + 1, sp2 - sp1 - 1);
	std::string ver = m_line.substr(sp2 + 1);

	if (m_req.method.empty()) { setError(400, "empty method"); return false; }
	for (std::size_t i = 0; i < m_req.method.size(); ++i) {
		if (!isTChar(static_cast<unsigned char>(m_req.method[i]))) {
			setError(400, "invalid character in method"); return false;
		}
	}

	if (m_req.target.empty()) { setError(400, "empty request-target"); return false; }
	for (std::size_t i = 0; i < m_req.target.size(); ++i) {
		if (!isValidTargetChar(static_cast<unsigned char>(m_req.target[i]))) {
			setError(400, "invalid character in request-target"); return false;
		}
	}

	return parseRequestTarget() && parseVersion(ver);
}

bool RequestParser::parseRequestTarget()
{
	const std::string &t = m_req.target;

	if (t == "*") {
		if (m_req.method != "OPTIONS") {
			setError(400, "asterisk-form only allowed with OPTIONS");
			return false;
		}
		m_req.path.clear();
		m_req.query.clear();
		return true;
	}

	if (t[0] == '/') {
		std::string rawPath, rawQuery;
		splitPathQuery(t, rawPath, rawQuery);
		if (!percentDecode(rawPath, m_req.path, true)) {
			setError(400, "invalid percent-encoding in path"); return false;
		}
		m_req.query = rawQuery;
		return true;
	}

	if (t.find("://") != std::string::npos) {
		std::string authority, pathQuery;
		if (!parseAbsoluteForm(t, authority, pathQuery)) {
			setError(400, "malformed absolute-form URI"); return false;
		}
		m_req.authority = authority;
		std::string rawPath, rawQuery;
		splitPathQuery(pathQuery, rawPath, rawQuery);
		if (rawPath.empty()) rawPath = "/";
		if (!percentDecode(rawPath, m_req.path, true)) {
			setError(400, "invalid percent-encoding in absolute-form path"); return false;
		}
		m_req.query = rawQuery;
		return true;
	}

	if (m_req.method == "CONNECT") {
		m_req.authority = t;
		return true;
	}

	setError(400, "unrecognized request-target form");
	return false;
}

bool RequestParser::parseVersion(const std::string &ver)
{
	if (ver.size() != 8) { setError(400, "HTTP-version must be exactly 8 chars"); return false; }
	if (ver[0] != 'H' || ver[1] != 'T' || ver[2] != 'T' || ver[3] != 'P'
	 || ver[4] != '/' || ver[6] != '.') {
		setError(400, "malformed HTTP-version"); return false;
	}
	if (!('0' <= ver[5] && ver[5] <= '9') || !('0' <= ver[7] && ver[7] <= '9')) {
		setError(400, "non-digit in HTTP-version"); return false;
	}
	m_req.version.major = ver[5] - '0';
	m_req.version.minor = ver[7] - '0';
	if (m_req.version.major != 1
	 || (m_req.version.minor != 0 && m_req.version.minor != 1)) {
		setError(505, "HTTP version not supported"); return false;
	}
	return true;
}

// -------- headers --------

ParseResult RequestParser::feedHeaders(const char *data,
                                       std::size_t len,
                                       std::size_t &consumed)
{
	consumed = 0;
	while (consumed < len) {
		char c = data[consumed++];
		if (c == '\n') {
			if (!m_line.empty() && m_line[m_line.size() - 1] == '\r') {
				m_line.erase(m_line.size() - 1);
			}
			if (m_line.empty()) {
				// End of headers. finalizeHeaders() decides whether
				// we still need to read a body.
				if (!finalizeHeaders()) return kParseError;
				if (m_phase == kPhaseDone) return kParseComplete;
				return kParseNeedMore;
			}
			// Reject obs-fold: line starting with SP/HTAB continues
			// the previous field. RFC 7230 §3.2.4 says a server MUST
			// respond with 400 (or otherwise reject) for obs-fold in
			// any request field except within Content-Type multipart.
			if (m_line[0] == ' ' || m_line[0] == '\t') {
				setError(400, "obsolete line folding in headers");
				return kParseError;
			}
			if (!parseHeaderLine(m_line)) return kParseError;
			m_line.clear();
			continue;
		}
		// Any octet is fine while accumulating; validation happens per
		// line so we only flag structural bytes here.
		m_line += c;
		++m_headerBytesSoFar;
		if (m_line.size() > m_maxLine) {
			setError(431, "header line too long"); return kParseError;
		}
		if (m_headerBytesSoFar > m_maxHeaderBytes) {
			setError(431, "total header bytes exceeded"); return kParseError;
		}
	}
	return kParseNeedMore;
}

bool RequestParser::parseHeaderLine(const std::string &line)
{
	// name : OWS value OWS   (obs-fold rejected above)
	std::string::size_type colon = line.find(':');
	if (colon == std::string::npos) {
		setError(400, "header missing ':'"); return false;
	}
	if (colon == 0) {
		setError(400, "empty header name"); return false;
	}

	// name: tchar only, no whitespace before ':' (RFC 7230 §3.2.4)
	for (std::size_t i = 0; i < colon; ++i) {
		unsigned char c = static_cast<unsigned char>(line[i]);
		if (!isTChar(c)) {
			setError(400, "invalid character in header name"); return false;
		}
	}

	std::string name = line.substr(0, colon);

	// Extract value with OWS trimmed from both sides.
	std::size_t vstart = colon + 1;
	while (vstart < line.size() && (line[vstart] == ' ' || line[vstart] == '\t')) ++vstart;
	std::size_t vend = line.size();
	while (vend > vstart && (line[vend - 1] == ' ' || line[vend - 1] == '\t')) --vend;
	std::string value = line.substr(vstart, vend - vstart);

	// Validate value bytes (obs-text allowed).
	for (std::size_t i = 0; i < value.size(); ++i) {
		if (!isFieldValueByte(static_cast<unsigned char>(value[i]))) {
			setError(400, "invalid byte in header value"); return false;
		}
	}

	++m_headerCountSoFar;
	if (m_headerCountSoFar > m_maxHeaderCount) {
		setError(431, "too many header fields"); return false;
	}

	// Duplicate handling.
	if (strutil::iequals(name, "Host")) {
		if (m_seenHost) { setError(400, "duplicate Host header"); return false; }
		m_seenHost = true;
	}
	if (strutil::iequals(name, "Content-Length")) {
		if (m_seenContentLength) {
			setError(400, "duplicate Content-Length header"); return false;
		}
		m_seenContentLength = true;
	}
	if (strutil::iequals(name, "Transfer-Encoding")) {
		m_seenTransferEncoding = true;
	}

	// Insert or combine with ', ' (RFC 7230 §3.2.2, except Set-Cookie).
	HeaderMap::iterator it = m_req.headers.find(name);
	if (it == m_req.headers.end()) {
		m_req.headers.insert(std::make_pair(name, value));
	} else {
		it->second += ", ";
		it->second += value;
	}
	return true;
}

bool RequestParser::finalizeHeaders()
{
	// HTTP/1.1 requires exactly one Host header.
	if (m_req.version == Version(1, 1) && !m_seenHost) {
		setError(400, "HTTP/1.1 request missing Host header"); return false;
	}

	// If absolute-form was used, prefer its authority over Host.
	if (!m_req.authority.empty() && m_seenHost) {
		// Both provided; the request-target's authority takes priority
		// per RFC 7230 §5.4. Nothing to change; both stay in Request.
	} else if (m_req.authority.empty() && m_seenHost) {
		m_req.authority = m_req.headers.find("Host")->second;
	}

	// Transfer-Encoding + Content-Length combination is forbidden.
	if (m_seenTransferEncoding && m_seenContentLength) {
		setError(400, "Content-Length and Transfer-Encoding are mutually exclusive");
		return false;
	}

	// Transfer-Encoding: check chunked is last coding.
	if (m_seenTransferEncoding) {
		HeaderMap::const_iterator it = m_req.headers.find("Transfer-Encoding");
		std::vector<std::string> codings = strutil::split(it->second, ',');
		if (codings.empty()) {
			setError(400, "empty Transfer-Encoding"); return false;
		}
		for (std::size_t i = 0; i < codings.size(); ++i) {
			codings[i] = strutil::trim(codings[i]);
		}
		if (!strutil::iequals(codings.back(), "chunked")) {
			setError(400, "Transfer-Encoding must end with 'chunked'"); return false;
		}
		for (std::size_t i = 0; i + 1 < codings.size(); ++i) {
			if (strutil::iequals(codings[i], "chunked")) {
				setError(400, "'chunked' may appear only as the final coding"); return false;
			}
		}
		m_req.chunked = true;
	}

	// Content-Length: parse to non-negative integer.
	if (m_seenContentLength) {
		HeaderMap::const_iterator it = m_req.headers.find("Content-Length");
		std::string v = strutil::trim(it->second);
		if (v.empty()) {
			setError(400, "empty Content-Length"); return false;
		}
		long n = 0;
		if (!strutil::parseLong(v, n) || n < 0) {
			setError(400, "invalid Content-Length"); return false;
		}
		m_req.contentLength = static_cast<std::size_t>(n);
	}

	// Connection: decide keep-alive.
	//   HTTP/1.1 defaults to keep-alive unless "close" is present.
	//   HTTP/1.0 defaults to close unless "keep-alive" is present.
	bool keepAliveDefault = (m_req.version == Version(1, 1));
	m_req.keepAlive = keepAliveDefault;
	HeaderMap::const_iterator cIt = m_req.headers.find("Connection");
	if (cIt != m_req.headers.end()) {
		std::vector<std::string> tokens = strutil::split(cIt->second, ',');
		for (std::size_t i = 0; i < tokens.size(); ++i) {
			std::string tok = strutil::trim(tokens[i]);
			if (strutil::iequals(tok, "close"))       m_req.keepAlive = false;
			if (strutil::iequals(tok, "keep-alive"))  m_req.keepAlive = true;
		}
	}

	// Cookie header (RFC 6265 §5.4). Names are case-sensitive.
	HeaderMap::const_iterator ckIt = m_req.headers.find("Cookie");
	if (ckIt != m_req.headers.end()) {
		std::vector<std::string> pairs = strutil::split(ckIt->second, ';');
		for (std::size_t i = 0; i < pairs.size(); ++i) {
			std::string p = strutil::trim(pairs[i]);
			if (p.empty()) continue;
			std::string::size_type eq = p.find('=');
			if (eq == std::string::npos) continue;
			std::string name  = strutil::trim(p.substr(0, eq));
			std::string value = strutil::trim(p.substr(eq + 1));
			// Strip surrounding quotes on the value.
			if (value.size() >= 2 && value[0] == '"'
			 && value[value.size() - 1] == '"') {
				value = value.substr(1, value.size() - 2);
			}
			if (name.empty()) continue;
			m_req.cookies[name] = value;
		}
	}

	// Decide the next phase: body (CL or chunked) or done.
	m_line.clear();
	if (m_req.chunked) {
		m_phase = kPhaseBodyChunkSize;
	} else if (m_req.contentLength > 0) {
		if (m_req.contentLength > m_maxBody) {
			setError(413, "declared Content-Length exceeds max body size");
			return false;
		}
		// Only pre-reserve when we're going to store the body ourselves.
		// A streaming sink absorbs each chunk directly so req.body would
		// stay empty; reserving a fresh 100 MB just to hold nothing is
		// exactly the peak-memory pathology streaming exists to avoid.
		if (m_bodySink == NULL) {
			m_req.body.reserve(m_req.contentLength);
		}
		m_phase = kPhaseBodyCL;
	} else {
		m_phase = kPhaseDone;
		// No body → fire onBodyEnd immediately so the sink can close
		// its stdin (relevant for POST with Content-Length: 0).
		if (m_bodySink != NULL && !m_bodyEndFired) {
			m_bodyEndFired = true;
			m_bodySink->onBodyEnd();
		}
	}
	return true;
}

// -------- body --------

bool RequestParser::parseChunkSizeLine()
{
	std::string s = m_line;
	std::string::size_type semi = s.find(';');
	if (semi != std::string::npos) {
		s = s.substr(0, semi);
	}
	while (!s.empty() && (s[s.size() - 1] == ' ' || s[s.size() - 1] == '\t')) {
		s.erase(s.size() - 1);
	}
	if (s.empty()) {
		setError(400, "empty chunk size"); return false;
	}
	std::size_t sz = 0;
	for (std::size_t i = 0; i < s.size(); ++i) {
		int h = 0;
		if (!hexDigit(s[i], h)) {
			setError(400, "invalid chunk size digit"); return false;
		}
		if (sz > (static_cast<std::size_t>(-1) - static_cast<std::size_t>(h)) / 16) {
			setError(413, "chunk size overflow"); return false;
		}
		sz = sz * 16 + static_cast<std::size_t>(h);
	}
	if (m_bodyBytesRead + sz > m_maxBody) {
		setError(413, "body exceeds max size"); return false;
	}
	m_chunkRemaining = sz;
	return true;
}

ParseResult RequestParser::feedBody(const char *data,
                                    std::size_t len,
                                    std::size_t &consumed)
{
	consumed = 0;
	while (consumed < len) {
		if (m_phase == kPhaseBodyCL) {
			std::size_t need  = m_req.contentLength - m_bodyBytesRead;
			std::size_t avail = len - consumed;
			std::size_t take  = (need < avail) ? need : avail;
			if (m_bodySink != NULL) {
				// Streaming: forward the chunk instead of growing req.body.
				m_bodySink->onBodyChunk(data + consumed, take);
			} else {
				m_req.body.append(data + consumed, take);
			}
			m_bodyBytesRead += take;
			consumed        += take;
			if (m_bodyBytesRead == m_req.contentLength) {
				m_phase = kPhaseDone;
				if (m_bodySink != NULL && !m_bodyEndFired) {
					m_bodyEndFired = true;
					m_bodySink->onBodyEnd();
				}
				return kParseComplete;
			}
			return kParseNeedMore;
		}

		if (m_phase == kPhaseBodyChunkSize) {
			char c = data[consumed++];
			if (c == '\n') {
				if (!m_line.empty() && m_line[m_line.size() - 1] == '\r') {
					m_line.erase(m_line.size() - 1);
				}
				if (!parseChunkSizeLine()) return kParseError;
				m_line.clear();
				m_phase = (m_chunkRemaining == 0)
				        ? kPhaseBodyTrailer
				        : kPhaseBodyChunkData;
				continue;
			}
			m_line += c;
			if (m_line.size() > 1024) {
				setError(400, "chunk size line too long");
				return kParseError;
			}
			continue;
		}

		if (m_phase == kPhaseBodyChunkData) {
			std::size_t avail = len - consumed;
			std::size_t take  = (m_chunkRemaining < avail) ? m_chunkRemaining : avail;
			if (m_bodySink != NULL) {
				m_bodySink->onBodyChunk(data + consumed, take);
			} else {
				m_req.body.append(data + consumed, take);
			}
			m_bodyBytesRead  += take;
			m_chunkRemaining -= take;
			consumed         += take;
			if (m_bodyBytesRead > m_maxBody) {
				setError(413, "body exceeds max size");
				return kParseError;
			}
			if (m_chunkRemaining == 0) {
				m_line.clear();
				m_phase = kPhaseBodyChunkCRLF;
			}
			continue;
		}

		if (m_phase == kPhaseBodyChunkCRLF) {
			char c = data[consumed++];
			m_line += c;
			if (m_line == "\r\n" || m_line == "\n") {
				m_line.clear();
				m_phase = kPhaseBodyChunkSize;
				continue;
			}
			if (m_line.size() >= 2) {
				setError(400, "expected CRLF after chunk data");
				return kParseError;
			}
			continue;
		}

		if (m_phase == kPhaseBodyTrailer) {
			char c = data[consumed++];
			if (c == '\n') {
				if (!m_line.empty() && m_line[m_line.size() - 1] == '\r') {
					m_line.erase(m_line.size() - 1);
				}
				if (m_line.empty()) {
					m_phase = kPhaseDone;
					if (m_bodySink != NULL && !m_bodyEndFired) {
						m_bodyEndFired = true;
						m_bodySink->onBodyEnd();
					}
					return kParseComplete;
				}
				// Trailer field-line: discarded (not merged into headers).
				m_line.clear();
				continue;
			}
			m_line += c;
			if (m_line.size() > 4096) {
				setError(431, "trailer line too long");
				return kParseError;
			}
			continue;
		}
		break; // unreachable
	}
	return kParseNeedMore;
}

} // namespace http
} // namespace webserv
