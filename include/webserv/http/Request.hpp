#ifndef WEBSERV_HTTP_REQUEST_HPP
#define WEBSERV_HTTP_REQUEST_HPP

#include <cstddef>
#include <map>
#include <string>

namespace webserv {
namespace http {

struct Version {
	int major;
	int minor;

	Version();
	Version(int a, int b);

	bool operator==(const Version &o) const;
	bool operator!=(const Version &o) const;
	bool operator<(const Version &o)  const;
};

// ASCII case-insensitive comparator for header field names.
// Header field names are case-insensitive per RFC 7230 §3.2.
struct HeaderNameLess {
	bool operator()(const std::string &a, const std::string &b) const;
};

typedef std::map<std::string, std::string, HeaderNameLess> HeaderMap;

typedef std::map<std::string, std::string> CookieMap;

// The parsed HTTP request. Fields are filled in progressively by the
// RequestParser: request-line fields first, then headers, then body.
struct Request {
	// -------- Request line --------
	std::string method;      // uppercased on ingest? -> No, kept as-is
	std::string target;      // raw request-target
	std::string path;        // %-decoded path (origin/absolute form) or "*"
	std::string query;       // raw query string (post-'?'), not decoded
	std::string authority;   // only set for absolute-form and authority-form
	Version     version;

	// -------- Headers (filled by feat/11) --------
	HeaderMap   headers;

	// -------- Body (filled by feat/12) --------
	std::string body;

	// -------- Semantic flags (filled by feat/11) --------
	bool        keepAlive;
	std::size_t contentLength;
	bool        chunked;

	// -------- Cookies (filled by feat/24) --------
	// Case-sensitive names per RFC 6265.
	CookieMap   cookies;

	Request();
	void clear();
};

} // namespace http
} // namespace webserv

#endif
