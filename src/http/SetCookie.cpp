#include "webserv/http/SetCookie.hpp"

#include "webserv/StringUtil.hpp"

#include <sstream>

namespace webserv {
namespace http {

SetCookieBuilder::SetCookieBuilder(const std::string &name, const std::string &value)
	: m_name(name), m_value(value),
	  m_path(), m_domain(), m_expires(), m_sameSite(),
	  m_maxAge(0), m_hasMaxAge(false),
	  m_secure(false), m_httpOnly(false)
{}

SetCookieBuilder &SetCookieBuilder::path(const std::string &p)      { m_path = p; return *this; }
SetCookieBuilder &SetCookieBuilder::domain(const std::string &d)    { m_domain = d; return *this; }
SetCookieBuilder &SetCookieBuilder::maxAge(long s)                  { m_maxAge = s; m_hasMaxAge = true; return *this; }
SetCookieBuilder &SetCookieBuilder::expiresGmt(const std::string &d){ m_expires = d; return *this; }
SetCookieBuilder &SetCookieBuilder::secure()                        { m_secure = true; return *this; }
SetCookieBuilder &SetCookieBuilder::httpOnly()                      { m_httpOnly = true; return *this; }
SetCookieBuilder &SetCookieBuilder::sameSite(const std::string &p)  { m_sameSite = p; return *this; }

std::string SetCookieBuilder::build() const
{
	std::ostringstream oss;
	oss << m_name << "=" << m_value;
	if (!m_path.empty())        oss << "; Path="     << m_path;
	if (!m_domain.empty())      oss << "; Domain="   << m_domain;
	if (m_hasMaxAge)            oss << "; Max-Age="  << m_maxAge;
	if (!m_expires.empty())     oss << "; Expires="  << m_expires;
	if (!m_sameSite.empty())    oss << "; SameSite=" << m_sameSite;
	if (m_secure)               oss << "; Secure";
	if (m_httpOnly)             oss << "; HttpOnly";
	return oss.str();
}

} // namespace http
} // namespace webserv
