#include "webserv/handler/StaticHandler.hpp"

#include "webserv/Log.hpp"
#include "webserv/StringUtil.hpp"
#include "webserv/handler/Autoindex.hpp"
#include "webserv/handler/ErrorPage.hpp"
#include "webserv/http/Mime.hpp"

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace webserv {
namespace handler {

namespace {

const std::size_t kMaxStaticFile = 10UL * 1024UL * 1024UL;   // 10 MiB

bool methodAllowed(const std::string                                   &method,
                   const std::vector<std::string>                     &allowed)
{
	if (allowed.empty()) {
		// No explicit list -> permit the three supported methods.
		return method == "GET"    || method == "POST" || method == "DELETE"
		    || method == "HEAD";
	}
	for (std::size_t i = 0; i < allowed.size(); ++i) {
		if (allowed[i] == method)                       return true;
		if (method == "HEAD" && allowed[i] == "GET")    return true;
	}
	return false;
}

std::string allowHeaderFromMethods(const std::vector<std::string> &allowed)
{
	if (allowed.empty()) {
		return "GET, POST, DELETE";
	}
	std::string out;
	for (std::size_t i = 0; i < allowed.size(); ++i) {
		if (i > 0) out += ", ";
		out += allowed[i];
	}
	return out;
}

std::string joinPath(const std::string &root, const std::string &rel)
{
	if (root.empty()) return rel;
	if (rel.empty())  return root;
	bool rootEndsSlash = root[root.size() - 1] == '/';
	bool relStartsSlash = rel[0] == '/';
	if (rootEndsSlash && relStartsSlash)   return root + rel.substr(1);
	if (!rootEndsSlash && !relStartsSlash) return root + "/" + rel;
	return root + rel;
}

// Read the whole regular file at `path` into `out`, up to maxBytes.
// Returns 0 on success, or a suggested HTTP status on failure.
int readWholeFile(const std::string &path,
                  std::size_t        maxBytes,
                  std::string       &out)
{
	int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0) {
		int err = errno;
		if (err == ENOENT || err == ENOTDIR) return 404;
		if (err == EACCES || err == EPERM)   return 403;
		return 500;
	}
	struct stat st;
	if (::fstat(fd, &st) < 0) {
		::close(fd);
		return 500;
	}
	if (!S_ISREG(st.st_mode)) {
		::close(fd);
		return 500;
	}
	if (static_cast<std::size_t>(st.st_size) > maxBytes) {
		::close(fd);
		return 413;
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
	if (total != static_cast<std::size_t>(st.st_size)) {
		out.resize(total);
	}
	return 0;
}

// If path is a directory, look for the configured index files in order.
// Returns 0 and sets outPath to the resolved file path on success.
// Returns a non-zero HTTP status if none match (403 when no autoindex,
// 404 when the directory itself is missing after stat).
int resolveIndex(const std::string              &dirPath,
                 const std::vector<std::string> &indexes,
                 std::string                    &outPath)
{
	for (std::size_t i = 0; i < indexes.size(); ++i) {
		std::string candidate = joinPath(dirPath, indexes[i]);
		struct stat st;
		if (::stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
			outPath = candidate;
			return 0;
		}
	}
	return 403;    // no index; autoindex handling is feat/16
}

void writeErrorBody(webserv::http::Response      &r,
                    int                           status,
                    const webserv::RouteMatch    *match)
{
	emitError(status, match, r);
}

} // anonymous

void serveStatic(const webserv::http::Request &req,
                 const webserv::RouteMatch    &match,
                 webserv::http::Response      &response)
{
	response.setKeepAlive(req.keepAlive);

	// A router-level failure at this point still owns error_page
	// resolution when the server itself was identified (the reordered
	// Router::match keeps server != NULL even for path traversal 400).
	if (match.errorStatus != 0 || match.server == NULL) {
		writeErrorBody(response,
		               match.errorStatus ? match.errorStatus : 500,
		               &match);
		return;
	}

	// Method policy: prefer location's list; fall back to server-wide.
	const std::vector<std::string> *allowed = NULL;
	if (match.location != NULL && !match.location->allowedMethods.empty()) {
		allowed = &match.location->allowedMethods;
	}
	std::vector<std::string> empty;
	if (allowed == NULL) allowed = &empty;
	if (!methodAllowed(req.method, *allowed)) {
		response.setHeader("Allow", allowHeaderFromMethods(*allowed));
		writeErrorBody(response, 405, &match);
		return;
	}

	// Location may declare `return CODE [URL];` — honor that first.
	if (match.location != NULL && match.location->hasReturn) {
		int code = match.location->ret.code;
		response.setStatus(code);
		response.setContentType("text/plain; charset=utf-8");
		if (!match.location->ret.url.empty()) {
			response.setHeader("Location", match.location->ret.url);
		}
		std::string body;
		body += strutil::toStr(static_cast<long>(code));
		body += " ";
		body += webserv::http::reasonPhrase(code);
		body += "\n";
		response.setBody(body);
		return;
	}

	// Assemble filesystem path from the location's root (or the server's
	// root if the location didn't set one).
	std::string root = (match.location != NULL && !match.location->root.empty())
	                 ? match.location->root
	                 : match.server->root;
	if (root.empty()) {
		writeErrorBody(response, 500, &match);
		return;
	}
	std::string relPath = match.normalizedPath.empty() ? "/" : match.normalizedPath;
	std::string fsPath  = joinPath(root, relPath);

	struct stat st;
	if (::stat(fsPath.c_str(), &st) < 0) {
		int err = errno;
		writeErrorBody(response,
		               (err == ENOENT || err == ENOTDIR) ? 404 : 500,
		               &match);
		return;
	}

	// Directory: try index files first, then autoindex if enabled.
	if (S_ISDIR(st.st_mode)) {
		// Redirect a directory URL without a trailing slash so relative
		// links inside the index / autoindex resolve correctly.
		if (!relPath.empty() && relPath[relPath.size() - 1] != '/') {
			response.setStatus(301);
			response.setHeader("Location", relPath + "/");
			response.setContentType("text/plain; charset=utf-8");
			response.setBody("301 Moved Permanently\n");
			return;
		}

		// Prefer location-level indexes if set, else server-level.
		const std::vector<std::string> *indexes =
			(match.location != NULL && !match.location->indexes.empty())
			? &match.location->indexes
			: &match.server->indexes;
		bool autoindex = (match.location != NULL)
		               ? match.location->autoindex
		               : match.server->autoindex;

		std::string idxPath;
		bool resolvedIndex = false;
		if (!indexes->empty()) {
			if (resolveIndex(fsPath, *indexes, idxPath) == 0) {
				resolvedIndex = true;
			}
		}

		if (!resolvedIndex) {
			if (!autoindex) {
				writeErrorBody(response, 403, &match);
				return;
			}
			std::string html;
			int st_ai = renderAutoindex(fsPath, relPath, html);
			if (st_ai != 0) {
				writeErrorBody(response, st_ai, &match);
				return;
			}
			response.setStatus(200);
			response.setContentType("text/html; charset=utf-8");
			if (req.method == "HEAD") {
				response.setHeader("Content-Length",
				                   strutil::toStr(static_cast<long>(html.size())));
				response.setBody(std::string());
			} else {
				response.setBody(html);
			}
			LOG_INFO("static: " << req.method << " " << req.path
			         << " -> autoindex(" << fsPath << ", "
			         << html.size() << "B)");
			return;
		}

		fsPath = idxPath;
		if (::stat(fsPath.c_str(), &st) < 0) {
			writeErrorBody(response, 404, &match);
			return;
		}
	}

	if (!S_ISREG(st.st_mode)) {
		writeErrorBody(response, 403, &match);
		return;
	}

	std::string body;
	int status = readWholeFile(fsPath, kMaxStaticFile, body);
	if (status != 0) {
		writeErrorBody(response, status, &match);
		return;
	}

	response.setStatus(200);
	response.setContentType(webserv::http::mimeForFilename(fsPath));
	// HEAD: same headers, empty body per RFC 7231.
	if (req.method == "HEAD") {
		response.setHeader("Content-Length",
		                   strutil::toStr(static_cast<long>(body.size())));
		response.setBody(std::string());
	} else {
		response.setBody(body);
	}

	LOG_INFO("static: " << req.method << " " << req.path
	         << " -> " << fsPath << " (" << body.size() << "B)");
}

} // namespace handler
} // namespace webserv
