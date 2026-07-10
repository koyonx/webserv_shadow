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

	int    status()    const;
	bool   keepAlive() const;

	std::string serialize() const;

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
};

const char *reasonPhrase(int status);
std::string httpDateNow();     // RFC 7231 IMF-fixdate for the current moment

} // namespace http
} // namespace webserv

#endif
