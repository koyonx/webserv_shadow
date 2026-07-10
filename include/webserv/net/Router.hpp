#ifndef WEBSERV_NET_ROUTER_HPP
#define WEBSERV_NET_ROUTER_HPP

#include "webserv/config/Config.hpp"
#include "webserv/http/Request.hpp"

#include <string>

namespace webserv {

// Result of routing a request. server is null only when nothing matches
// the (host:port) listener at all (which should not happen if the loader
// bound every server correctly). location is null when no `location`
// block matched the normalized path (server-level catch-all).
struct RouteMatch {
	const webserv::config::ServerConfig   *server;
	const webserv::config::LocationConfig *location;
	std::string                            normalizedPath;
	int                                    errorStatus;   // 0 = OK, else set
	const char                            *errorMessage;

	RouteMatch();
};

class Router {
public:
	explicit Router(const webserv::config::Config &cfg);

	// Compute the RouteMatch for one request received on `origin`.
	// The Host header on the Request has already been captured into
	// req.authority by the parser.
	//
	// On path-traversal failure returns match with errorStatus=400.
	RouteMatch match(const webserv::config::Listen &origin,
	                 const webserv::http::Request  &req) const;

	// Public so it can be unit-tested.
	//
	// Percent-decoded path in `in` -> `out` with '.' and '..' resolved,
	// preserving a trailing slash. Returns false if the path would
	// escape the root ("..") more times than it descended.
	static bool normalizePath(const std::string &in, std::string &out);

	// Case-insensitive prefix match with a "/" boundary check.
	// prefix == "/" matches every path. Otherwise the request path
	// must equal prefix or start with prefix + "/".
	static bool locationPrefixMatches(const std::string &prefix,
	                                  const std::string &path);

private:
	const webserv::config::Config &m_cfg;

	// Strips :PORT off "host:port" (dumb split at the last ':' but only
	// when the tail is all digits). Lowercases the result.
	static std::string hostHeaderName(const std::string &raw);

	// Server matching by (listen, server_name).
	const webserv::config::ServerConfig *selectServer(
		const webserv::config::Listen &origin,
		const std::string             &host) const;

	// Recursive longest-prefix match over a location tree.
	const webserv::config::LocationConfig *matchLocation(
		const std::vector<webserv::config::LocationConfig> &locs,
		const std::string                                  &path) const;
};

} // namespace webserv

#endif
