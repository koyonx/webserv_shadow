#include "webserv/handler/ErrorPage.hpp"

#include "webserv/Log.hpp"
#include "webserv/StringUtil.hpp"
#include "webserv/http/Mime.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace webserv {
namespace handler {

namespace {

const std::size_t kMaxErrorPageBytes = 1UL * 1024UL * 1024UL;   // 1 MiB

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

const std::string *lookupErrorPage(int                        status,
                                    const webserv::RouteMatch &match)
{
	if (match.location != NULL) {
		std::map<int, std::string>::const_iterator it =
			match.location->errorPages.find(status);
		if (it != match.location->errorPages.end()) return &it->second;
	}
	if (match.server != NULL) {
		std::map<int, std::string>::const_iterator it =
			match.server->errorPages.find(status);
		if (it != match.server->errorPages.end()) return &it->second;
	}
	return NULL;
}

bool readFile(const std::string &path,
              std::size_t        maxBytes,
              std::string       &out)
{
	int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0) return false;
	struct stat st;
	if (::fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)
	 || static_cast<std::size_t>(st.st_size) > maxBytes) {
		::close(fd);
		return false;
	}
	out.resize(static_cast<std::size_t>(st.st_size));
	std::size_t total = 0;
	while (total < static_cast<std::size_t>(st.st_size)) {
		ssize_t r = ::read(fd,
		                   const_cast<char *>(out.data()) + total,
		                   static_cast<std::size_t>(st.st_size) - total);
		if (r <= 0) break;
		total += static_cast<std::size_t>(r);
	}
	::close(fd);
	if (total != static_cast<std::size_t>(st.st_size)) out.resize(total);
	return true;
}

void plainError(int status, webserv::http::Response &r)
{
	std::ostringstream body;
	body << status << " " << webserv::http::reasonPhrase(status) << "\n";
	r.setStatus(status);
	r.setContentType("text/plain; charset=utf-8");
	r.setBody(body.str());
}

} // anonymous

void emitError(int                        status,
               const webserv::RouteMatch *match,
               webserv::http::Response   &response)
{
	if (match == NULL || match->server == NULL) {
		plainError(status, response);
		return;
	}

	const std::string *pagePath = lookupErrorPage(status, *match);
	if (pagePath == NULL) {
		plainError(status, response);
		return;
	}

	// Resolve the error page relative to the effective root for the
	// matched scope (location.root beats server.root when set).
	std::string root = (match->location != NULL
	                   && !match->location->root.empty())
	                 ? match->location->root
	                 : match->server->root;
	if (root.empty()) {
		plainError(status, response);
		return;
	}
	std::string fsPath = joinPath(root, *pagePath);

	std::string body;
	if (!readFile(fsPath, kMaxErrorPageBytes, body)) {
		LOG_WARN("error_page: cannot read " << fsPath
		         << " for status " << status
		         << " -- falling back to plain text");
		plainError(status, response);
		return;
	}

	response.setStatus(status);
	response.setContentType(webserv::http::mimeForFilename(fsPath));
	response.setBody(body);
	LOG_INFO("error_page: " << status << " -> " << fsPath
	         << " (" << body.size() << "B)");
}

} // namespace handler
} // namespace webserv
