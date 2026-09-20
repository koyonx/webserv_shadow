#include "webserv/handler/CgiHandler.hpp"

#include "webserv/StringUtil.hpp"

#include <cstddef>
#include <sstream>
#include <sys/stat.h>
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

std::string extensionOf(const std::string &path)
{
	std::string::size_type dot   = path.rfind('.');
	std::string::size_type slash = path.find_last_of('/');
	if (dot == std::string::npos)                 return "";
	if (slash != std::string::npos && dot < slash) return "";
	return strutil::toLower(path.substr(dot));
}

std::string parentDir(const std::string &path)
{
	std::string::size_type slash = path.find_last_of('/');
	if (slash == std::string::npos) return ".";
	if (slash == 0)                 return "/";
	return path.substr(0, slash);
}

// If `path` is relative, prepend the current working directory so the
// CGI child can find it after chdir(workDir).
std::string ensureAbsolute(const std::string &path)
{
	if (!path.empty() && path[0] == '/') return path;
	char buf[4096];
	if (::getcwd(buf, sizeof(buf)) == NULL) return path;
	std::string cwd = buf;
	if (path.empty()) return cwd;
	if (!cwd.empty() && cwd[cwd.size() - 1] == '/') return cwd + path;
	return cwd + "/" + path;
}

} // anonymous

bool cgiMatch(const webserv::RouteMatch &match,
              const std::string         &method,
              std::string               &interpreterOut,
              std::string               &scriptPathOut,
              std::string               &scriptUriOut,
              std::string               &pathInfoOut,
              std::string               &workDirOut)
{
	if (match.location == NULL || match.location->cgiPass.empty()) {
		return false;
	}
	std::string ext = extensionOf(match.normalizedPath);
	if (ext.empty()) return false;

	std::map<std::string, std::string>::const_iterator it =
		match.location->cgiPass.find(ext);
	if (it == match.location->cgiPass.end()) return false;

	std::string root = (!match.location->root.empty())
	                 ? match.location->root
	                 : match.server->root;
	if (root.empty()) return false;

	// Subject-correct root: strip the location's prefix so the URL
	// under `root` (nginx's `alias` semantics per the subject example).
	std::string relPath = webserv::Router::stripLocationPrefix(
		match.location->path, match.normalizedPath);
	std::string abs = ensureAbsolute(joinPath(root, relPath));
	// GET on a missing script gets 404 (users likely mistyped the URL).
	// POST on a missing script still routes into the CGI — the 42
	// tester specifically probes "POST /directory/youpla.bla" (non-
	// existent .bla) and expects the 500 the tester binary itself
	// emits ("PATH_INFO not found"), not a webserv-side 404. Sending
	// 404 from webserv trips its "bad status code" check.
	struct stat st;
	bool scriptExists = (::stat(abs.c_str(), &st) == 0 && S_ISREG(st.st_mode));
	if (!scriptExists && method != "POST") {
		return false;
	}

	// The interpreter often looks like "./testers/cgi_tester" — that
	// relative path breaks after we chdir(workDir) in the child, so
	// absolutize the interpreter too. A bare name like "python3" is
	// left as-is: PATH lookup in execve/execvp is fine.
	std::string interp = it->second;
	if (!interp.empty()
	 && interp[0] != '/'
	 && interp.find('/') != std::string::npos) {
		interp = ensureAbsolute(interp);
	}

	interpreterOut = interp;
	scriptPathOut  = abs;
	scriptUriOut   = match.normalizedPath;
	// The 42 tester's cgi_tester binary validates that PATH_INFO equals
	// the request URI (its "PATH_INFO incorrect" branch). Strict RFC
	// 3875 leaves PATH_INFO empty when the request URI IS the script
	// path — but that trips the tester. Setting PATH_INFO to the URI
	// path is what the subject example implies and matches nginx's
	// behavior under fastcgi_split_path_info-with-a-catch-all pattern.
	// It's a superset: standard interpreters (python, bash, php-cgi)
	// don't validate PATH_INFO's value, so they still work.
	pathInfoOut    = match.normalizedPath;
	workDirOut     = parentDir(scriptPathOut);
	return true;
}

int parseCgiHeaders(const std::string       &headerBlock,
                    webserv::http::Response &response)
{
	// L1: an empty header block (no bytes before the terminator, or
	// no ':' at all in it) means the CGI didn't produce a well-formed
	// response. Signal 502 to the caller without touching response.
	if (headerBlock.empty()) {
		return 502;
	}

	// NPH prefix: "HTTP/1.1 STATUS reason\r\n" is legal per RFC 3875
	// §6.2.4. We don't do full passthrough (our Response builder still
	// wants to inject Date/Server); just strip the status-line and
	// treat the rest as CGI headers.
	std::size_t offset = 0;
	if (headerBlock.compare(0, 5, "HTTP/") == 0) {
		std::size_t nl = headerBlock.find('\n');
		if (nl != std::string::npos) offset = nl + 1;
	}

	// L1 continued: at least one 'name: value' pair is required.
	{
		bool hasHeader = false;
		for (std::size_t k = offset; k < headerBlock.size(); ++k) {
			if (headerBlock[k] == ':') { hasHeader = true; break; }
		}
		if (!hasHeader) {
			return 502;
		}
	}

	int         status = 200;
	std::string statusReason;
	std::vector<std::pair<std::string, std::string> > userHeaders;

	std::size_t p = offset;
	while (p < headerBlock.size()) {
		std::string::size_type nl = headerBlock.find('\n', p);
		std::string line = (nl == std::string::npos)
		                 ? headerBlock.substr(p)
		                 : headerBlock.substr(p, nl - p);
		p = (nl == std::string::npos) ? headerBlock.size() : nl + 1;
		if (!line.empty() && line[line.size() - 1] == '\r') {
			line.erase(line.size() - 1);
		}
		if (line.empty()) continue;
		std::string::size_type colon = line.find(':');
		if (colon == std::string::npos) continue;
		std::string name  = strutil::trim(line.substr(0, colon));
		std::string value = strutil::trim(line.substr(colon + 1));
		if (name.empty()) continue;

		if (strutil::iequals(name, "Status")) {
			// Format: "Status: NNN Reason"
			std::size_t sp = value.find(' ');
			std::string codeStr = (sp == std::string::npos) ? value : value.substr(0, sp);
			long code = 0;
			if (strutil::parseLong(codeStr, code) && code >= 100 && code <= 599) {
				status = static_cast<int>(code);
			}
			if (sp != std::string::npos) statusReason = value.substr(sp + 1);
			continue;
		}
		if (strutil::iequals(name, "Location") && status == 200) {
			// CGI Local/Client redirect: default to 302 unless
			// Status is set. Absolute vs relative doesn't matter for
			// our response builder.
			status = 302;
		}
		userHeaders.push_back(std::make_pair(name, value));
	}

	response.setStatus(status);
	if (!statusReason.empty()) response.setReason(statusReason);
	for (std::size_t i = 0; i < userHeaders.size(); ++i) {
		// Set-Cookie can legitimately repeat; every other header we
		// let CGI override anything already set on the response.
		if (strutil::iequals(userHeaders[i].first, "Set-Cookie")) {
			response.addHeader(userHeaders[i].first, userHeaders[i].second);
		} else {
			response.setHeader(userHeaders[i].first, userHeaders[i].second);
		}
	}
	return 0;
}

void applyCgiOutput(const std::string       &raw,
                    webserv::http::Response &response)
{
	// Locate CRLF CRLF or LF LF as end-of-headers, without stripping
	// any NPH prefix here — parseCgiHeaders handles that.
	std::string::size_type endHdr = raw.find("\r\n\r\n");
	std::size_t bodyStart;
	std::string headerBlock;
	if (endHdr != std::string::npos) {
		headerBlock = raw.substr(0, endHdr);
		bodyStart   = endHdr + 4;
	} else {
		std::string::size_type endHdr2 = raw.find("\n\n");
		if (endHdr2 != std::string::npos) {
			headerBlock = raw.substr(0, endHdr2);
			bodyStart   = endHdr2 + 2;
		} else {
			// L1: no terminator at all.
			response.setStatus(502);
			response.setContentType("text/plain; charset=utf-8");
			response.setBody("502 Bad Gateway (CGI: missing header terminator)\n");
			return;
		}
	}

	int rc = parseCgiHeaders(headerBlock, response);
	if (rc != 0) {
		// L1: empty header block or no name:value pair.
		response.setStatus(502);
		response.setContentType("text/plain; charset=utf-8");
		response.setBody("502 Bad Gateway (CGI: no header fields)\n");
		return;
	}
	response.setBody(raw.substr(bodyStart));
}

} // namespace handler
} // namespace webserv
