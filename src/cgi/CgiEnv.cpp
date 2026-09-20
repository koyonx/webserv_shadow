#include "webserv/cgi/CgiEnv.hpp"

#include "webserv/StringUtil.hpp"

#include <sstream>

namespace webserv {
namespace cgi {

std::string headerNameToCgi(const std::string &name)
{
	std::string out = "HTTP_";
	out.reserve(out.size() + name.size());
	for (std::size_t i = 0; i < name.size(); ++i) {
		char c = name[i];
		if (c == '-') {
			out += '_';
		} else if ('a' <= c && c <= 'z') {
			out += static_cast<char>(c - ('a' - 'A'));
		} else {
			out += c;
		}
	}
	return out;
}

namespace {

void add(std::vector<std::string> &env,
         const std::string        &key,
         const std::string        &value)
{
	env.push_back(key + "=" + value);
}

} // anonymous

std::vector<std::string> buildEnv(
	const webserv::http::Request        &req,
	const webserv::config::Listen       &origin,
	const webserv::config::ServerConfig &server,
	const std::string                   &scriptPath,
	const std::string                   &scriptUri,
	const std::string                   &pathInfo)
{
	std::vector<std::string> env;

	// -------- CGI 1.1 required (RFC 3875 §4.1) --------
	add(env, "GATEWAY_INTERFACE", "CGI/1.1");
	add(env, "SERVER_SOFTWARE",   "webserv/0.1");
	add(env, "SERVER_PROTOCOL",
	    "HTTP/" + strutil::toStr(static_cast<long>(req.version.major)) + "." +
	              strutil::toStr(static_cast<long>(req.version.minor)));
	add(env, "SERVER_PORT",       strutil::toStr(static_cast<long>(origin.port)));

	// SERVER_NAME: prefer Host header (already lowercased in req.authority)
	// then the first server_name, then the listen host.
	std::string serverName;
	if (!req.authority.empty()) {
		serverName = req.authority;
		std::string::size_type colon = serverName.rfind(':');
		if (colon != std::string::npos) {
			bool digits = colon + 1 < serverName.size();
			for (std::size_t i = colon + 1; digits && i < serverName.size(); ++i) {
				if (serverName[i] < '0' || serverName[i] > '9') { digits = false; break; }
			}
			if (digits) serverName = serverName.substr(0, colon);
		}
	} else if (!server.serverNames.empty()) {
		serverName = server.serverNames.front();
	} else {
		serverName = origin.host;
	}
	add(env, "SERVER_NAME", serverName);

	add(env, "REQUEST_METHOD", req.method);
	add(env, "REQUEST_URI",    req.target);
	add(env, "QUERY_STRING",   req.query);

	add(env, "SCRIPT_NAME",     scriptUri);
	add(env, "SCRIPT_FILENAME", scriptPath);   // PHP-CGI requires this
	add(env, "PATH_INFO",       pathInfo);
	if (!pathInfo.empty() && !server.root.empty()) {
		std::string translated = server.root;
		if (!translated.empty() && translated[translated.size() - 1] == '/') {
			translated.erase(translated.size() - 1);
		}
		translated += pathInfo;
		add(env, "PATH_TRANSLATED", translated);
	}

	// DOCUMENT_ROOT is not in RFC 3875 but common CGI runtimes want it.
	if (!server.root.empty()) {
		add(env, "DOCUMENT_ROOT", server.root);
	}

	// REDIRECT_STATUS = 200 satisfies php-cgi's built-in guard. Harmless
	// for any other interpreter.
	add(env, "REDIRECT_STATUS", "200");

	// REMOTE_ADDR / REMOTE_HOST -- we don't thread the client sockaddr
	// through yet. Emit safe placeholders so testers don't get an
	// undefined-variable warning.
	add(env, "REMOTE_ADDR",  "127.0.0.1");
	add(env, "REMOTE_HOST",  "");

	// -------- Request body descriptors --------
	if (req.contentLength > 0 || req.chunked) {
		add(env, "CONTENT_LENGTH",
		    strutil::toStr(static_cast<long>(req.body.size())));
	} else {
		add(env, "CONTENT_LENGTH", "0");
	}
	webserv::http::HeaderMap::const_iterator ctIt =
		req.headers.find("Content-Type");
	if (ctIt != req.headers.end()) {
		add(env, "CONTENT_TYPE", ctIt->second);
	}

	// -------- HTTP_* headers --------
	for (webserv::http::HeaderMap::const_iterator it = req.headers.begin();
	     it != req.headers.end(); ++it) {
		// Skip Content-* -- already emitted above.
		if (strutil::iequals(it->first, "Content-Type"))   continue;
		if (strutil::iequals(it->first, "Content-Length")) continue;
		// Skip Authorization intentionally? No, RFC 3875 forwards it.
		add(env, headerNameToCgi(it->first), it->second);
	}

	return env;
}

} // namespace cgi
} // namespace webserv
