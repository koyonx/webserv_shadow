#include "webserv/http/RequestParser.hpp"

#include <cstddef>

namespace webserv {
namespace http {

// ---------- character class helpers ----------

static bool isTChar(unsigned char c)
{
	// RFC 7230 §3.2.6 tchar.
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

// RFC 3986 unreserved and reserved chars we allow *raw* in request-target.
// SP and CTLs are excluded; "<>{}|\^`" are RFC 3986 delimiters that MUST
// be percent-encoded in a URI. High bytes (>= 0x80) must be encoded too;
// we reject them raw for strictness.
static bool isValidTargetChar(unsigned char c)
{
	if (c < 0x21)  return false;   // CTL and SP
	if (c > 0x7E)  return false;   // DEL and high bytes
	switch (c) {
		case '<': case '>': case '{': case '}':
		case '|': case '\\': case '^': case '`': case '"':
			return false;
	}
	return true;
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
			if (i + 2 >= in.size()) {
				return false;
			}
			int h1 = 0, h2 = 0;
			if (!hexDigit(in[i + 1], h1) || !hexDigit(in[i + 2], h2)) {
				return false;
			}
			unsigned char b = static_cast<unsigned char>((h1 << 4) | h2);
			if (rejectNul && b == 0) {
				return false;
			}
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

// absolute-form: scheme "://" authority path-abempty [ "?" query ]
static bool parseAbsoluteForm(const std::string &target,
                              std::string       &authority,
                              std::string       &pathQuery)
{
	std::string::size_type ss = target.find("://");
	if (ss == std::string::npos || ss == 0) {
		return false;
	}
	std::string scheme = target.substr(0, ss);
	for (std::size_t i = 0; i < scheme.size(); ++i) {
		char c = scheme[i];
		bool ok =
			(('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) ||
			(i > 0 && ('0' <= c && c <= '9')) ||
			(i > 0 && (c == '+' || c == '-' || c == '.'));
		if (!ok) {
			return false;
		}
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
	  m_headerBuf(),
	  m_maxLine(8192),
	  m_status(0),
	  m_errmsg(NULL)
{}

void RequestParser::reset()
{
	m_phase = kPhaseRequestLine;
	m_req.clear();
	m_line.clear();
	m_headerBuf.clear();
	m_status = 0;
	m_errmsg = NULL;
}

const Request &RequestParser::request()      const { return m_req; }
int            RequestParser::errorStatus()  const { return m_status; }
const char    *RequestParser::errorMessage() const { return m_errmsg; }
void           RequestParser::setMaxRequestLine(std::size_t b) { m_maxLine = b; }

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
			if (m_phase != kPhaseHeadersStub) {
				// Still in request line but need more bytes.
				return kParseNeedMore;
			}
			// Fall through to headers stub in same call so we make
			// progress on any leftover bytes in this feed.
		}
		if (m_phase == kPhaseHeadersStub) {
			std::size_t c = 0;
			ParseResult r = feedHeadersStub(data + consumed, len - consumed, c);
			consumed += c;
			if (r != kParseNeedMore) return r;
			return kParseNeedMore;
		}
		if (consumed == before) {
			// Guard against zero-progress loop.
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
			// Strip trailing '\r' if the line ended with CRLF.
			if (!m_line.empty() && m_line[m_line.size() - 1] == '\r') {
				m_line.erase(m_line.size() - 1);
			}
			if (m_line.empty()) {
				// RFC 7230 §3.5: servers SHOULD ignore at least one
				// leading empty line before the request-line.
				continue;
			}
			if (!parseAccumulatedLine()) {
				return kParseError;
			}
			m_phase = kPhaseHeadersStub;
			return kParseNeedMore;
		}
		// CTLs (except \r as line-terminator prefix) are invalid.
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

bool RequestParser::parseAccumulatedLine()
{
	// Split "METHOD SP TARGET SP HTTP/n.n" — SP is one space, no tabs.
	std::string::size_type sp1 = m_line.find(' ');
	if (sp1 == std::string::npos) {
		setError(400, "request-line missing SP after method"); return false;
	}
	std::string::size_type sp2 = m_line.find(' ', sp1 + 1);
	if (sp2 == std::string::npos) {
		setError(400, "request-line missing SP after target"); return false;
	}
	// Reject a third SP (multiple SPs => malformed).
	if (m_line.find(' ', sp2 + 1) != std::string::npos) {
		setError(400, "extra whitespace in request-line"); return false;
	}

	m_req.method = m_line.substr(0, sp1);
	m_req.target = m_line.substr(sp1 + 1, sp2 - sp1 - 1);
	std::string ver = m_line.substr(sp2 + 1);

	// Method: 1*tchar
	if (m_req.method.empty()) {
		setError(400, "empty method"); return false;
	}
	for (std::size_t i = 0; i < m_req.method.size(); ++i) {
		if (!isTChar(static_cast<unsigned char>(m_req.method[i]))) {
			setError(400, "invalid character in method");
			return false;
		}
	}

	// Target: character validity
	if (m_req.target.empty()) {
		setError(400, "empty request-target"); return false;
	}
	for (std::size_t i = 0; i < m_req.target.size(); ++i) {
		if (!isValidTargetChar(static_cast<unsigned char>(m_req.target[i]))) {
			setError(400, "invalid character in request-target");
			return false;
		}
	}

	if (!parseRequestTarget()) {
		return false;
	}
	if (!parseVersion(ver)) {
		return false;
	}
	return true;
}

bool RequestParser::parseRequestTarget()
{
	const std::string &t = m_req.target;

	// asterisk-form: only for OPTIONS
	if (t == "*") {
		if (m_req.method != "OPTIONS") {
			setError(400, "asterisk-form only allowed with OPTIONS");
			return false;
		}
		m_req.path.clear();
		m_req.query.clear();
		return true;
	}

	// origin-form: absolute-path starts with '/'
	if (t[0] == '/') {
		std::string rawPath, rawQuery;
		splitPathQuery(t, rawPath, rawQuery);
		if (!percentDecode(rawPath, m_req.path, /*rejectNul=*/true)) {
			setError(400, "invalid percent-encoding in path");
			return false;
		}
		m_req.query = rawQuery;
		return true;
	}

	// absolute-form: contains "://"
	if (t.find("://") != std::string::npos) {
		std::string authority, pathQuery;
		if (!parseAbsoluteForm(t, authority, pathQuery)) {
			setError(400, "malformed absolute-form URI");
			return false;
		}
		m_req.authority = authority;
		std::string rawPath, rawQuery;
		splitPathQuery(pathQuery, rawPath, rawQuery);
		if (rawPath.empty()) {
			rawPath = "/";
		}
		if (!percentDecode(rawPath, m_req.path, true)) {
			setError(400, "invalid percent-encoding in absolute-form path");
			return false;
		}
		m_req.query = rawQuery;
		return true;
	}

	// authority-form: only for CONNECT
	if (m_req.method == "CONNECT") {
		m_req.authority = t;
		m_req.path.clear();
		m_req.query.clear();
		return true;
	}

	setError(400, "unrecognized request-target form");
	return false;
}

bool RequestParser::parseVersion(const std::string &ver)
{
	// Strict form: "HTTP/<DIGIT>.<DIGIT>"
	if (ver.size() != 8) {
		setError(400, "HTTP-version must be exactly 8 chars"); return false;
	}
	if (ver[0] != 'H' || ver[1] != 'T' || ver[2] != 'T' || ver[3] != 'P'
	 || ver[4] != '/' || ver[6] != '.') {
		setError(400, "malformed HTTP-version"); return false;
	}
	if (!('0' <= ver[5] && ver[5] <= '9')
	 || !('0' <= ver[7] && ver[7] <= '9')) {
		setError(400, "non-digit in HTTP-version"); return false;
	}
	m_req.version.major = ver[5] - '0';
	m_req.version.minor = ver[7] - '0';

	// We only accept HTTP/1.0 and HTTP/1.1.
	if (m_req.version.major != 1
	 || (m_req.version.minor != 0 && m_req.version.minor != 1)) {
		setError(505, "HTTP version not supported");
		return false;
	}
	return true;
}

// -------- headers stub (feat/11 replaces this) --------

ParseResult RequestParser::feedHeadersStub(const char *data,
                                           std::size_t len,
                                           std::size_t &consumed)
{
	consumed = 0;
	while (consumed < len) {
		m_headerBuf += data[consumed++];
		if (m_headerBuf.size() >= 4) {
			if (m_headerBuf.compare(m_headerBuf.size() - 4, 4, "\r\n\r\n") == 0
			 || (m_headerBuf.size() >= 2
			  && m_headerBuf.compare(m_headerBuf.size() - 2, 2, "\n\n") == 0)) {
				m_phase = kPhaseDone;
				return kParseComplete;
			}
		} else if (m_headerBuf.size() >= 2
		        && m_headerBuf.compare(m_headerBuf.size() - 2, 2, "\n\n") == 0) {
			m_phase = kPhaseDone;
			return kParseComplete;
		}
		if (m_headerBuf.size() > 32 * 1024) {
			setError(431, "headers too large");
			return kParseError;
		}
	}
	return kParseNeedMore;
}

} // namespace http
} // namespace webserv
