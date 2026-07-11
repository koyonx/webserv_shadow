#include "webserv/handler/StaticHandler.hpp"

#include "webserv/Log.hpp"
#include "webserv/StringUtil.hpp"
#include "webserv/handler/Autoindex.hpp"
#include "webserv/handler/ErrorPage.hpp"
#include "webserv/http/Conditional.hpp"
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
		// M1: ENAMETOOLONG surfaces when the URI is longer than the
		// filesystem allows -> 414 (URI Too Long) rather than 500.
		// ELOOP means a symlink cycle -> 404 (as if it didn't exist).
		// ENOENT/ENOTDIR are the ordinary "no such file" cases.
		if (err == ENAMETOOLONG)             return 414;
		if (err == ENOENT || err == ENOTDIR) return 404;
		if (err == ELOOP)                    return 404;
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

	// Dispatch has already applied allowed_methods and `return`
	// short-circuits, so we can go straight to file resolution.

	// Assemble filesystem path from the location's root (or the server's
	// root if the location didn't set one).
	std::string root = (match.location != NULL && !match.location->root.empty())
	                 ? match.location->root
	                 : match.server->root;
	if (root.empty()) {
		writeErrorBody(response, 500, &match);
		return;
	}
	// Subject-correct root: strip the location's prefix from the URL
	// before joining with root. Example from the subject: /kapouet
	// rooted at /tmp/www serves /kapouet/pouic/toto/pouet from
	// /tmp/www/pouic/toto/pouet — i.e., alias-style, not append.
	const std::string &locPath = (match.location != NULL)
	                             ? match.location->path
	                             : std::string("/");
	std::string relPath = webserv::Router::stripLocationPrefix(
		locPath, match.normalizedPath);
	std::string fsPath  = joinPath(root, relPath);

	struct stat st;
	if (::stat(fsPath.c_str(), &st) < 0) {
		int err = errno;
		int status;
		// M1: keep 5xx for real server-side problems only. Long/looped
		// URIs and permission errors are client-side conditions.
		if (err == ENAMETOOLONG)              status = 414;
		else if (err == ENOENT || err == ENOTDIR
		      || err == ELOOP)                 status = 404;
		else if (err == EACCES || err == EPERM) status = 403;
		else                                    status = 500;
		writeErrorBody(response, status, &match);
		return;
	}

	// Directory: try index files first, then autoindex if enabled.
	if (S_ISDIR(st.st_mode)) {
		// Redirect a directory URL without a trailing slash so relative
		// links inside the index / autoindex resolve correctly.
		// Use the full request path (match.normalizedPath), NOT the
		// alias-stripped relPath — otherwise the Location header drops
		// the location prefix (e.g. /directory/nop -> Location: /nop/).
		const std::string &reqPath = match.normalizedPath;
		if (!reqPath.empty() && reqPath[reqPath.size() - 1] != '/') {
			response.setStatus(301);
			response.setHeader("Location", reqPath + "/");
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
				// No index resolved and no autoindex: the subject wording
				// for /directory/ is "if no file are requested, it should
				// search for youpi.bad_extension files" — i.e. absence of
				// the index is a not-found condition, not a permission
				// error. 404 matches the 42 tester's expectation and
				// still round-trips through error_page handling.
				writeErrorBody(response, 404, &match);
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

	// Conditional GET / Range prep. We have `st` fully populated from
	// the stat/fstat path above, and the file at `fsPath` is regular.
	const std::size_t fileSize = static_cast<std::size_t>(st.st_size);
	const std::string etag     = webserv::http::buildETag(fileSize, st.st_mtime);
	const std::string lastMod  = webserv::http::httpDateFromTime(st.st_mtime);

	// Every static response gets these headers so an intermediary /
	// browser can cache and downstream tools see we support range.
	response.setHeader("Last-Modified",  lastMod);
	response.setHeader("ETag",           etag);
	response.setHeader("Accept-Ranges", "bytes");

	// If-None-Match / If-Modified-Since: 304 short-circuits body reads.
	if (webserv::http::evaluatePreconditions(
	        req.headers, st.st_mtime, etag)
	    == webserv::http::kProceedNotModified) {
		response.setStatus(304);
		response.setBody(std::string());
		LOG_INFO("static: " << req.method << " " << req.path
		         << " -> " << fsPath << " (304 Not Modified)");
		return;
	}

	// Range / If-Range.
	webserv::http::RangeSpec   rspec;
	webserv::http::RangeResult rres = webserv::http::parseRange(
		req.headers, fileSize, st.st_mtime, etag, rspec);

	if (rres == webserv::http::kRangeUnsatisfiable) {
		std::ostringstream cr;
		cr << "bytes */" << fileSize;
		response.setHeader("Content-Range", cr.str());
		writeErrorBody(response, 416, &match);
		return;
	}

	std::string body;
	int status = readWholeFile(fsPath, kMaxStaticFile, body);
	if (status != 0) {
		writeErrorBody(response, status, &match);
		return;
	}

	if (rres == webserv::http::kRangeValid) {
		std::size_t len = rspec.end - rspec.start + 1;
		body = body.substr(rspec.start, len);
		response.setStatus(206);
		std::ostringstream cr;
		cr << "bytes " << rspec.start << "-" << rspec.end
		   << "/" << rspec.total;
		response.setHeader("Content-Range", cr.str());
	} else {
		response.setStatus(200);
	}

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
	         << " -> " << fsPath
	         << " (" << body.size() << "B"
	         << (rres == webserv::http::kRangeValid ? " partial" : "")
	         << ")");
}

} // namespace handler
} // namespace webserv
