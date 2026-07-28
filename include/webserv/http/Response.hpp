#ifndef WEBSERV_HTTP_RESPONSE_HPP
#define WEBSERV_HTTP_RESPONSE_HPP

#include "webserv/http/Request.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace webserv {
namespace http {

// A ready-to-serialize HTTP response.
//
// Layered API: set status / headers / body incrementally; serialize()
// produces the exact byte sequence to write to the socket. The
// following headers are auto-populated unless already set:
//   Date            (RFC 7231 §7.1.1.1, always in GMT)
//   Server          ("webserv/0.1")
//   Content-Length  (based on body.size())
//   Content-Type    (defaults to text/plain; charset=utf-8)
//   Connection      (keep-alive or close, derived from setKeepAlive)
class Response {
public:
	Response();
	void clear();

	Response &setVersion(const Version &v);
	Response &setStatus(int code);
	Response &setReason(const std::string &reason);
	Response &setKeepAlive(bool ka);

	// Header operations. "set" replaces any prior same-name header;
	// "add" appends (useful for Set-Cookie, which cannot legally
	// be combined into one field-value).
	Response &setHeader(const std::string &name, const std::string &value);
	Response &addHeader(const std::string &name, const std::string &value);

	Response &setBody(const std::string &body);
	Response &setBody(const char *data, std::size_t len);
	Response &setContentType(const std::string &mime);

	// HEAD request: suppress the body from the wire but keep the
	// Content-Length header reflecting the equivalent GET response
	// (RFC 7231 §4.3.2). Centralizing this in Response avoids every
	// handler needing an "if (method == HEAD) setBody(\"\")" branch,
	// which is easy to forget on error paths — a missing check leaks
	// bytes onto a keep-alive connection and desyncs the next response.
	Response &setSuppressBody(bool suppress);

	// Chunked streaming mode.
	//
	// Turning this on emits `Transfer-Encoding: chunked` instead of
	// `Content-Length: N` in serializeHeaders(), so a caller that
	// doesn't know the body size up-front (a CGI script's stdout is
	// the canonical case) can stream body chunks to the wire without
	// buffering. In this mode:
	//   - serializeHeaders() returns just the status line + headers
	//     + the empty-line separator; m_body is not written.
	//   - Body bytes must be encoded via chunkFrame() before being
	//     handed to the socket (one frame per stdout read is fine).
	//   - The stream MUST be terminated by chunkTerminator() — a
	//     "0\r\n\r\n" — otherwise the peer waits forever.
	Response &setChunked(bool on);

	int    status()    const;
	bool   keepAlive() const;
	bool   chunked()   const;

	std::string serialize() const;

	// Header-only serialization for streaming responses. Emits the
	// status line, user + default headers (Content-Length is
	// suppressed when chunked mode is on), and the terminating blank
	// line. m_body is NOT appended — the caller feeds body bytes
	// separately via chunkFrame() / chunkTerminator().
	std::string serializeHeaders() const;

	// Frame `data`/`len` bytes as one HTTP/1.1 Transfer-Encoding
	// chunk: "<hexlen>\r\n<bytes>\r\n". Empty chunks are a no-op
	// (the terminator is a *separate* helper so this method can be
	// called safely inside a loop without accidentally closing the
	// stream on a zero-byte read).
	static std::string chunkFrame(const char *data, std::size_t len);

	// The zero-length chunk that ends a chunked response body:
	// "0\r\n\r\n". Must be sent exactly once after the last
	// chunkFrame() call.
	static std::string chunkTerminator();

	// Convenience: build a plain-text error response with the standard
	// reason phrase and a short body "STATUS reason".
	static Response makeError(int status);

private:
	typedef std::vector<std::pair<std::string, std::string> > HeaderList;

	bool  hasHeader(const std::string &name) const;

	Version     m_version;
	int         m_status;
	std::string m_reason;
	HeaderList  m_headers;
	std::string m_body;
	bool        m_keepAlive;
	bool        m_suppressBody;
	bool        m_chunked;
};

const char *reasonPhrase(int status);
std::string httpDateNow();     // RFC 7231 IMF-fixdate for the current moment

} // namespace http
} // namespace webserv

#endif
