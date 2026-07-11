#include "webserv/http/Response.hpp"

#include "webserv/StringUtil.hpp"
#include "webserv/http/Conditional.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace webserv {
namespace http {

const char *reasonPhrase(int status)
{
	switch (status) {
		case 100: return "Continue";
		case 101: return "Switching Protocols";
		case 200: return "OK";
		case 201: return "Created";
		case 202: return "Accepted";
		case 204: return "No Content";
		case 206: return "Partial Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 304: return "Not Modified";
		case 307: return "Temporary Redirect";
		case 308: return "Permanent Redirect";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 406: return "Not Acceptable";
		case 408: return "Request Timeout";
		case 409: return "Conflict";
		case 411: return "Length Required";
		case 413: return "Payload Too Large";
		case 414: return "URI Too Long";
		case 415: return "Unsupported Media Type";
		case 416: return "Range Not Satisfiable";
		case 431: return "Request Header Fields Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Timeout";
		case 505: return "HTTP Version Not Supported";
	}
	return (status < 500) ? "Error" : "Server Error";
}

std::string httpDateNow()
{
	// Delegates to httpDateFromTime() in Conditional so we have a single
	// locale-independent IMF-fixdate formatter shared by Date, ETag
	// checks, and Last-Modified.
	return httpDateFromTime(std::time(NULL));
}

// ---------------------- Response ----------------------

Response::Response()
	: m_version(1, 1),
	  m_status(200),
	  m_reason(),
	  m_headers(),
	  m_body(),
	  m_keepAlive(false)
{}

void Response::clear()
{
	m_version   = Version(1, 1);
	m_status    = 200;
	m_reason.clear();
	m_headers.clear();
	m_body.clear();
	m_keepAlive = false;
}

Response &Response::setVersion(const Version &v) { m_version   = v; return *this; }
Response &Response::setStatus(int code)          { m_status    = code; return *this; }
Response &Response::setReason(const std::string &r) { m_reason = r; return *this; }
Response &Response::setKeepAlive(bool ka)        { m_keepAlive = ka; return *this; }

Response &Response::setHeader(const std::string &name, const std::string &value)
{
	for (HeaderList::iterator it = m_headers.begin(); it != m_headers.end(); ) {
		if (strutil::iequals(it->first, name)) {
			it = m_headers.erase(it);
		} else {
			++it;
		}
	}
	m_headers.push_back(std::make_pair(name, value));
	return *this;
}

Response &Response::addHeader(const std::string &name, const std::string &value)
{
	m_headers.push_back(std::make_pair(name, value));
	return *this;
}

Response &Response::setBody(const std::string &body)
{
	m_body = body;
	return *this;
}

Response &Response::setBody(const char *data, std::size_t len)
{
	m_body.assign(data, len);
	return *this;
}

Response &Response::setContentType(const std::string &mime)
{
	return setHeader("Content-Type", mime);
}

int  Response::status()    const { return m_status; }
bool Response::keepAlive() const { return m_keepAlive; }

bool Response::hasHeader(const std::string &name) const
{
	for (HeaderList::const_iterator it = m_headers.begin();
	     it != m_headers.end(); ++it) {
		if (strutil::iequals(it->first, name)) {
			return true;
		}
	}
	return false;
}

std::string Response::serialize() const
{
	std::ostringstream oss;

	// Status line
	const std::string &reason = m_reason.empty()
	                          ? std::string(reasonPhrase(m_status))
	                          : m_reason;
	oss << "HTTP/" << m_version.major << "." << m_version.minor
	    << " "     << m_status
	    << " "     << reason << "\r\n";

	// Emit user headers first (skipping the auto ones so we can insert
	// defaults after — auto headers can still be overridden by user
	// values via hasHeader() below).
	for (HeaderList::const_iterator it = m_headers.begin();
	     it != m_headers.end(); ++it) {
		oss << it->first << ": " << it->second << "\r\n";
	}

	// Defaults
	if (!hasHeader("Date")) {
		oss << "Date: " << httpDateNow() << "\r\n";
	}
	if (!hasHeader("Server")) {
		oss << "Server: webserv/0.1\r\n";
	}
	if (!hasHeader("Content-Type") && !m_body.empty()) {
		oss << "Content-Type: text/plain; charset=utf-8\r\n";
	}
	if (!hasHeader("Content-Length")) {
		oss << "Content-Length: " << m_body.size() << "\r\n";
	}
	if (!hasHeader("Connection")) {
		oss << "Connection: " << (m_keepAlive ? "keep-alive" : "close") << "\r\n";
	}

	oss << "\r\n";
	oss << m_body;
	return oss.str();
}

Response Response::makeError(int status)
{
	Response r;
	r.setStatus(status);
	r.setKeepAlive(false);
	std::ostringstream body;
	body << status << " " << reasonPhrase(status) << "\n";
	r.setBody(body.str());
	r.setContentType("text/plain; charset=utf-8");
	return r;
}

} // namespace http
} // namespace webserv
