#include "webserv/handler/DeleteHandler.hpp"

#include "webserv/Log.hpp"
#include "webserv/handler/ErrorPage.hpp"
#include "webserv/net/Router.hpp"

#include <cerrno>
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

} // anonymous

void serveDelete(const webserv::http::Request &req,
                 const webserv::RouteMatch    &match,
                 webserv::http::Response      &response)
{
	response.setKeepAlive(req.keepAlive);

	std::string root = (match.location != NULL
	                   && !match.location->root.empty())
	                 ? match.location->root
	                 : match.server->root;
	if (root.empty()) {
		emitError(500, &match, response);
		return;
	}
	// Subject-correct root: strip location prefix before joining.
	const std::string &locPath = (match.location != NULL)
	                             ? match.location->path
	                             : std::string("/");
	std::string relPath = webserv::Router::stripLocationPrefix(
		locPath, match.normalizedPath);
	std::string fsPath = joinPath(root, relPath);

	struct stat st;
	if (::stat(fsPath.c_str(), &st) < 0) {
		int err = errno;
		emitError((err == ENOENT || err == ENOTDIR) ? 404 : 500,
		          &match, response);
		return;
	}

	if (S_ISDIR(st.st_mode)) {
		// Directory removal via DELETE is intentionally not supported;
		// upload cleanup workflows operate on files only.
		emitError(403, &match, response);
		return;
	}
	if (!S_ISREG(st.st_mode)) {
		emitError(403, &match, response);
		return;
	}

	if (::unlink(fsPath.c_str()) < 0) {
		int err = errno;
		int status = (err == EACCES || err == EPERM) ? 403 : 500;
		LOG_WARN("delete: unlink(" << fsPath << ") failed errno=" << err);
		emitError(status, &match, response);
		return;
	}

	// 204 No Content: MUST NOT have a message body (RFC 7230 §3.3.3).
	response.setStatus(204);
	response.setBody(std::string());
	LOG_INFO("delete: " << req.path << " -> " << fsPath);
}

} // namespace handler
} // namespace webserv
