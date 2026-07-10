#include "webserv/handler/PostHandler.hpp"

#include "webserv/Log.hpp"
#include "webserv/StringUtil.hpp"
#include "webserv/handler/ErrorPage.hpp"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace webserv {
namespace handler {

namespace {

std::string joinPath(const std::string &root, const std::string &rel)
{
	if (root.empty()) return rel;
	if (rel.empty())  return root;
	bool r1 = root[root.size() - 1] == '/';
	bool r2 = rel[0] == '/';
	if (r1 && r2)   return root + rel.substr(1);
	if (!r1 && !r2) return root + "/" + rel;
	return root + rel;
}

// Content-Type may carry parameters (e.g. "text/plain; charset=utf-8").
// Return just the media type in lowercase.
std::string primaryMediaType(const std::string &raw)
{
	std::string s = raw;
	std::string::size_type semi = s.find(';');
	if (semi != std::string::npos) s = s.substr(0, semi);
	return strutil::toLower(strutil::trim(s));
}

std::string extensionForMedia(const std::string &media)
{
	if (media == "application/json")                   return ".json";
	if (media == "application/xml"   || media == "text/xml") return ".xml";
	if (media == "text/plain")                          return ".txt";
	if (media == "text/html")                           return ".html";
	if (media == "text/css")                            return ".css";
	if (media == "application/javascript"
	 || media == "text/javascript")                     return ".js";
	if (media == "image/png")                           return ".png";
	if (media == "image/jpeg")                          return ".jpg";
	if (media == "image/gif")                           return ".gif";
	if (media == "application/x-www-form-urlencoded")   return ".form";
	return ".bin";
}

std::string generateUploadName(const std::string &ext)
{
	static unsigned long counter = 0;
	++counter;

	std::ostringstream oss;
	oss << static_cast<long>(std::time(NULL))
	    << '-' << static_cast<long>(::getpid())
	    << '-' << counter
	    << ext;
	return oss.str();
}

bool writeAll(int fd, const std::string &data)
{
	std::size_t written = 0;
	while (written < data.size()) {
		ssize_t w = ::write(fd, data.data() + written, data.size() - written);
		if (w <= 0) return false;
		written += static_cast<std::size_t>(w);
	}
	return true;
}

void jsonEscape(const std::string &in, std::string &out)
{
	for (std::size_t i = 0; i < in.size(); ++i) {
		char c = in[i];
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					out += "?";
				} else {
					out += c;
				}
		}
	}
}

std::string echoPreview(const std::string &body, std::size_t maxBytes)
{
	std::string preview = (body.size() <= maxBytes)
	                    ? body
	                    : body.substr(0, maxBytes);
	std::string safe;
	jsonEscape(preview, safe);
	return safe;
}

} // anonymous

void servePost(const webserv::http::Request &req,
               const webserv::RouteMatch    &match,
               webserv::http::Response      &response)
{
	response.setKeepAlive(req.keepAlive);

	const webserv::config::LocationConfig *loc = match.location;
	const std::string mediaType = primaryMediaType(
		req.headers.count("Content-Type")
		? req.headers.find("Content-Type")->second
		: std::string());

	// ---- Upload-store path ----
	if (loc != NULL && !loc->uploadStore.empty()) {
		struct stat st;
		if (::stat(loc->uploadStore.c_str(), &st) < 0
		 || !S_ISDIR(st.st_mode)) {
			LOG_WARN("post: upload_store not a directory: " << loc->uploadStore);
			emitError(500, &match, response);
			return;
		}

		std::string ext     = extensionForMedia(mediaType);
		std::string name    = generateUploadName(ext);
		std::string fsPath  = joinPath(loc->uploadStore, name);

		int fd = ::open(fsPath.c_str(),
		                O_WRONLY | O_CREAT | O_EXCL,
		                0644);
		if (fd < 0) {
			LOG_WARN("post: open(" << fsPath << ") failed errno=" << errno);
			emitError(500, &match, response);
			return;
		}
		if (!writeAll(fd, req.body)) {
			::close(fd);
			::unlink(fsPath.c_str());
			emitError(500, &match, response);
			return;
		}
		::close(fd);

		std::string locationUrl = loc->path;
		if (locationUrl.empty() || locationUrl[locationUrl.size() - 1] != '/') {
			locationUrl += '/';
		}
		locationUrl += name;

		response.setStatus(201);
		response.setContentType("application/json");
		response.setHeader("Location", locationUrl);
		std::ostringstream body;
		body << "{\"stored\":\"" << name
		     << "\",\"path\":\"" << locationUrl
		     << "\",\"size\":"   << req.body.size()
		     << ",\"media\":\""  << (mediaType.empty() ? "unspecified" : mediaType)
		     << "\"}\n";
		response.setBody(body.str());
		LOG_INFO("post: stored " << req.body.size() << "B -> " << fsPath);
		return;
	}

	// ---- No upload target: acknowledge with 200 and a preview ----
	response.setStatus(200);
	response.setContentType("application/json");
	std::ostringstream body;
	body << "{\"received\":"    << req.body.size()
	     << ",\"media\":\""     << (mediaType.empty() ? "unspecified" : mediaType)
	     << "\",\"preview\":\"" << echoPreview(req.body, 512)
	     << "\"}\n";
	response.setBody(body.str());
	LOG_INFO("post: ack " << req.body.size() << "B (no upload_store)");
}

} // namespace handler
} // namespace webserv
