#ifndef WEBSERV_HTTP_SET_COOKIE_HPP
#define WEBSERV_HTTP_SET_COOKIE_HPP

#include <string>

namespace webserv {
namespace http {

// Fluent builder for a Set-Cookie header value.
// Multiple attributes may be chained:
//   SetCookieBuilder("sid", token)
//       .path("/")
//       .maxAge(3600)
//       .httpOnly()
//       .sameSite("Lax");
//   response.addHeader("Set-Cookie", builder.build());
//
// Notes:
//  - Cookie name/value are emitted verbatim; the caller is responsible
//    for keeping them within RFC 6265 cookie-octet set.
//  - domain / path values are emitted as given (no encoding).
class SetCookieBuilder {
public:
	SetCookieBuilder(const std::string &name, const std::string &value);

	SetCookieBuilder &path(const std::string &p);
	SetCookieBuilder &domain(const std::string &d);
	SetCookieBuilder &maxAge(long seconds);
	SetCookieBuilder &expiresGmt(const std::string &imfFixdate);
	SetCookieBuilder &secure();
	SetCookieBuilder &httpOnly();
	SetCookieBuilder &sameSite(const std::string &policy); // "Lax" / "Strict" / "None"

	std::string build() const;

private:
	std::string m_name;
	std::string m_value;
	std::string m_path;
	std::string m_domain;
	std::string m_expires;
	std::string m_sameSite;
	long        m_maxAge;
	bool        m_hasMaxAge;
	bool        m_secure;
	bool        m_httpOnly;
};

} // namespace http
} // namespace webserv

#endif
