#include "webserv/handler/Autoindex.hpp"

#include "webserv/StringUtil.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <ctime>
#include <dirent.h>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

namespace webserv {
namespace handler {

namespace {

struct Entry {
	std::string name;
	bool        isDir;
	off_t       size;
	std::time_t mtime;

	Entry() : name(), isDir(false), size(0), mtime(0) {}

	bool operator<(const Entry &o) const {
		if (isDir != o.isDir) return isDir; // directories first
		return name < o.name;
	}
};

std::string htmlEscape(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (std::size_t i = 0; i < s.size(); ++i) {
		char c = s[i];
		switch (c) {
			case '&':  out += "&amp;";  break;
			case '<':  out += "&lt;";   break;
			case '>':  out += "&gt;";   break;
			case '"':  out += "&quot;"; break;
			case '\'': out += "&#39;";  break;
			default:   out += c;        break;
		}
	}
	return out;
}

char toHex(int n)
{
	n &= 0xF;
	return (n < 10) ? static_cast<char>('0' + n)
	                : static_cast<char>('a' + n - 10);
}

std::string urlEncodePath(const std::string &s)
{
	// Percent-encode everything outside the unreserved set (RFC 3986)
	// plus '/', which is a path separator and must remain literal.
	std::string out;
	out.reserve(s.size());
	for (std::size_t i = 0; i < s.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		bool unreserved =
			(c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
		if (unreserved) {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += toHex(c >> 4);
			out += toHex(c);
		}
	}
	return out;
}

std::string formatSize(off_t bytes, bool isDir)
{
	if (isDir) return "-";
	std::ostringstream oss;
	oss << bytes;
	return oss.str();
}

std::string formatTime(std::time_t t)
{
	static const char *mon[] = {"Jan","Feb","Mar","Apr","May","Jun",
	                            "Jul","Aug","Sep","Oct","Nov","Dec"};
	std::tm *tm = std::gmtime(&t);
	if (tm == NULL) return "-";
	std::ostringstream oss;
	oss << std::setfill('0')
	    << std::setw(2) << tm->tm_mday << '-' << mon[tm->tm_mon] << '-'
	    << (tm->tm_year + 1900) << ' '
	    << std::setw(2) << tm->tm_hour << ':'
	    << std::setw(2) << tm->tm_min;
	return oss.str();
}

std::string joinFs(const std::string &dir, const std::string &name)
{
	if (dir.empty()) return name;
	if (dir[dir.size() - 1] == '/') return dir + name;
	return dir + "/" + name;
}

// Pad `s` on the right to at least `width` chars.
std::string padRight(const std::string &s, std::size_t width)
{
	if (s.size() >= width) return s + " ";
	return s + std::string(width - s.size(), ' ');
}

std::string padLeft(const std::string &s, std::size_t width)
{
	if (s.size() >= width) return " " + s;
	return std::string(width - s.size(), ' ') + s;
}

} // anonymous

int renderAutoindex(const std::string &fsDir,
                    const std::string &requestPath,
                    std::string       &outHtml)
{
	DIR *d = ::opendir(fsDir.c_str());
	if (d == NULL) {
		int err = errno;
		if (err == ENOENT || err == ENOTDIR) return 404;
		if (err == EACCES)                   return 403;
		return 500;
	}

	std::vector<Entry> entries;
	struct dirent     *de;
	while ((de = ::readdir(d)) != NULL) {
		std::string name = de->d_name;
		if (name == ".") continue;
		Entry e;
		e.name = name;
		std::string full = joinFs(fsDir, name);
		struct stat st;
		if (::stat(full.c_str(), &st) == 0) {
			e.isDir = S_ISDIR(st.st_mode);
			e.size  = st.st_size;
			e.mtime = st.st_mtime;
		}
		if (name == "..") {
			// Only include ".." when we're not at the vhost root.
			if (requestPath == "/") continue;
			e.isDir = true;
		}
		entries.push_back(e);
	}
	::closedir(d);

	std::sort(entries.begin(), entries.end());

	std::ostringstream oss;
	std::string titlePath = htmlEscape(requestPath);
	oss << "<!doctype html>\n"
	    << "<html>\n"
	    << "<head><meta charset=\"utf-8\"><title>Index of "
	    << titlePath << "</title></head>\n"
	    << "<body>\n"
	    << "<h1>Index of " << titlePath << "</h1>\n"
	    << "<hr>\n"
	    << "<pre>\n";

	for (std::size_t i = 0; i < entries.size(); ++i) {
		const Entry &e = entries[i];
		std::string display = e.name;
		if (e.isDir && !display.empty()
		 && display[display.size() - 1] != '/') {
			display += '/';
		}
		std::string href = urlEncodePath(display);
		std::string col1    = padRight("<a href=\"" + href + "\">" +
		                               htmlEscape(display) + "</a>", 50);
		std::string col2    = padLeft(formatSize(e.size, e.isDir), 12);
		std::string col3    = "  " + formatTime(e.mtime);
		oss << col1 << col2 << col3 << "\n";
	}

	oss << "</pre>\n"
	    << "<hr>\n"
	    << "<address>webserv/0.1</address>\n"
	    << "</body>\n"
	    << "</html>\n";
	outHtml = oss.str();
	return 0;
}

} // namespace handler
} // namespace webserv
