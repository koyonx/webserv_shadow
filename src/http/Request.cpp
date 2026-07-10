#include "webserv/http/Request.hpp"

namespace webserv {
namespace http {

Version::Version()               : major(0), minor(0) {}
Version::Version(int a, int b)   : major(a), minor(b) {}

bool Version::operator==(const Version &o) const
{
	return major == o.major && minor == o.minor;
}

bool Version::operator!=(const Version &o) const
{
	return !(*this == o);
}

bool Version::operator<(const Version &o) const
{
	if (major != o.major) {
		return major < o.major;
	}
	return minor < o.minor;
}

static unsigned char asciiLowerU(unsigned char c)
{
	return ('A' <= c && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

bool HeaderNameLess::operator()(const std::string &a, const std::string &b) const
{
	std::size_t n = a.size() < b.size() ? a.size() : b.size();
	for (std::size_t i = 0; i < n; ++i) {
		unsigned char ca = asciiLowerU(static_cast<unsigned char>(a[i]));
		unsigned char cb = asciiLowerU(static_cast<unsigned char>(b[i]));
		if (ca != cb) {
			return ca < cb;
		}
	}
	return a.size() < b.size();
}

Request::Request()
	: method(),
	  target(),
	  path(),
	  query(),
	  authority(),
	  version(),
	  headers(),
	  body(),
	  keepAlive(false),
	  contentLength(0),
	  chunked(false)
{}

void Request::clear()
{
	method.clear();
	target.clear();
	path.clear();
	query.clear();
	authority.clear();
	version = Version();
	headers.clear();
	body.clear();
	keepAlive     = false;
	contentLength = 0;
	chunked       = false;
}

} // namespace http
} // namespace webserv
