#include "webserv/net/Router.hpp"

#include "webserv/StringUtil.hpp"

namespace webserv {

RouteMatch::RouteMatch()
	: server(NULL),
	  location(NULL),
	  normalizedPath(),
	  errorStatus(0),
	  errorMessage(NULL)
{}

Router::Router(const webserv::config::Config &cfg) : m_cfg(cfg) {}

// -------- path normalization --------

bool Router::normalizePath(const std::string &in, std::string &out)
{
	out.clear();
	if (in.empty()) {
		out = "/";
		return true;
	}
	bool leadingSlash  = (in[0] == '/');
	bool trailingSlash = (in.size() > 1) && (in[in.size() - 1] == '/');
	if (!leadingSlash) {
		// Origin-form always starts with '/'; anything else is treated
		// as if it did so we can still evaluate normalization safely.
		leadingSlash = true;
	}

	std::vector<std::string> segs;
	std::string              cur;
	for (std::size_t i = 0; i <= in.size(); ++i) {
		char c = (i < in.size()) ? in[i] : '/';
		if (c == '/') {
			if (cur.empty()) {
				// consecutive '/' or leading '/' — skip
			} else if (cur == ".") {
				// no-op
			} else if (cur == "..") {
				if (segs.empty()) {
					return false;   // escapes root
				}
				segs.pop_back();
			} else {
				segs.push_back(cur);
			}
			cur.clear();
		} else {
			cur += c;
		}
	}

	out = "/";
	for (std::size_t i = 0; i < segs.size(); ++i) {
		if (i > 0) out += '/';
		out += segs[i];
	}
	if (!segs.empty() && out.size() > 1 && trailingSlash) {
		out += '/';
	}
	return true;
}

// -------- prefix matching --------

bool Router::locationPrefixMatches(const std::string &prefix,
                                   const std::string &path)
{
	if (prefix.empty()) return false;
	if (prefix == "/")  return true;

	// Trailing-slash case: `/dir/` matches exact `/dir` too so the
	// static handler can emit a 301 redirect that adds the missing
	// slash (matches nginx).
	std::size_t plen           = prefix.size();
	bool        prefixHasTrail = (prefix[plen - 1] == '/');
	if (prefixHasTrail
	 && path.size() + 1 == plen
	 && path.compare(0, path.size(), prefix, 0, path.size()) == 0) {
		return true;
	}

	if (path.size() < plen)                       return false;
	if (path.compare(0, plen, prefix) != 0)       return false;
	if (path.size() == plen)                       return true;
	// Boundary check: prefix "/api" must not match "/apix".
	if (prefixHasTrail)                            return true;
	return path[plen] == '/';
}

std::string Router::stripLocationPrefix(const std::string &locationPath,
                                        const std::string &normalizedPath)
{
	if (locationPath.empty() || locationPath == "/") {
		return normalizedPath.empty() ? std::string("/") : normalizedPath;
	}

	std::size_t plen           = locationPath.size();
	bool        prefixHasTrail = (locationPath[plen - 1] == '/');

	// Exact match with the trailing-slash form: "/dir/" vs "/dir".
	if (prefixHasTrail
	 && normalizedPath.size() + 1 == plen
	 && normalizedPath.compare(0, normalizedPath.size(),
	                           locationPath, 0, normalizedPath.size()) == 0) {
		return "/";
	}

	// Common prefix: peel it off.
	if (normalizedPath.size() >= plen
	 && normalizedPath.compare(0, plen, locationPath) == 0) {
		std::string rest = normalizedPath.substr(plen);
		if (rest.empty())                     return "/";
		if (rest[0] != '/')                   rest = "/" + rest;
		return rest;
	}

	// Fell through — path didn't actually start with the prefix
	// (shouldn't happen if locationPrefixMatches was true earlier).
	return normalizedPath.empty() ? std::string("/") : normalizedPath;
}

// -------- server selection --------

std::string Router::hostHeaderName(const std::string &raw)
{
	// "example.com:8080" -> "example.com". IPv6 forms like "[::1]:80"
	// keep their brackets; we split on the last ':' only when what
	// follows is all-digit, to avoid corrupting IPv6 authorities.
	std::string s = strutil::trim(raw);
	std::string::size_type colon = s.rfind(':');
	if (colon != std::string::npos) {
		bool allDigits = colon + 1 < s.size();
		for (std::size_t i = colon + 1; allDigits && i < s.size(); ++i) {
			if (s[i] < '0' || s[i] > '9') { allDigits = false; break; }
		}
		if (allDigits) s = s.substr(0, colon);
	}
	return strutil::toLower(s);
}

static bool serverListenMatches(const webserv::config::ServerConfig &srv,
                                const webserv::config::Listen       &origin)
{
	for (std::size_t i = 0; i < srv.listens.size(); ++i) {
		const webserv::config::Listen &l = srv.listens[i];
		if (l.port != origin.port) continue;
		// Wildcard host on either side matches everything.
		if (l.host == "0.0.0.0" || origin.host == "0.0.0.0") return true;
		if (l.host == origin.host) return true;
	}
	return false;
}

static bool serverNameMatches(const std::string &pattern,
                              const std::string &host)
{
	if (pattern.empty() || host.empty()) return false;
	if (strutil::iequals(pattern, host)) return true;
	// Prefix wildcard: "*.example.com"
	if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.') {
		std::string suffix = pattern.substr(1); // ".example.com"
		if (host.size() >= suffix.size()) {
			std::string tail = host.substr(host.size() - suffix.size());
			if (strutil::iequals(tail, suffix)) return true;
		}
	}
	return false;
}

const webserv::config::ServerConfig *
Router::selectServer(const webserv::config::Listen &origin,
                     const std::string             &host) const
{
	const webserv::config::ServerConfig *firstOnListen = NULL;
	for (std::size_t i = 0; i < m_cfg.servers.size(); ++i) {
		const webserv::config::ServerConfig &s = m_cfg.servers[i];
		if (!serverListenMatches(s, origin)) continue;
		if (firstOnListen == NULL) firstOnListen = &s;
		// Try exact / wildcard server_name match.
		for (std::size_t j = 0; j < s.serverNames.size(); ++j) {
			if (serverNameMatches(s.serverNames[j], host)) {
				return &s;
			}
		}
	}
	return firstOnListen;    // default server on this listener
}

// -------- location selection --------

const webserv::config::LocationConfig *
Router::matchLocation(const std::vector<webserv::config::LocationConfig> &locs,
                      const std::string                                  &path) const
{
	const webserv::config::LocationConfig *best = NULL;
	for (std::size_t i = 0; i < locs.size(); ++i) {
		const webserv::config::LocationConfig &loc = locs[i];
		if (!locationPrefixMatches(loc.path, path)) continue;
		const webserv::config::LocationConfig *inner = NULL;
		if (!loc.locations.empty()) {
			inner = matchLocation(loc.locations, path);
		}
		const webserv::config::LocationConfig *candidate =
			(inner != NULL) ? inner : &loc;
		if (best == NULL || candidate->path.size() > best->path.size()) {
			best = candidate;
		}
	}
	return best;
}

// -------- entry point --------

static std::size_t maxInLocations(
	const std::vector<webserv::config::LocationConfig> &locs)
{
	std::size_t best = 0;
	for (std::size_t i = 0; i < locs.size(); ++i) {
		if (locs[i].maxBodySize > best) best = locs[i].maxBodySize;
		std::size_t nested = maxInLocations(locs[i].locations);
		if (nested > best) best = nested;
	}
	return best;
}

std::size_t Router::maxBodyCap() const
{
	std::size_t best = 0;
	for (std::size_t i = 0; i < m_cfg.servers.size(); ++i) {
		const webserv::config::ServerConfig &s = m_cfg.servers[i];
		if (s.maxBodySize > best) best = s.maxBodySize;
		std::size_t nested = maxInLocations(s.locations);
		if (nested > best) best = nested;
	}
	return (best > 0) ? best : (1024UL * 1024UL);
}

RouteMatch Router::match(const webserv::config::Listen &origin,
                         const webserv::http::Request  &req) const
{
	RouteMatch m;

	// Server selection first, so error responses that fail path
	// validation still know which server's error_page config applies.
	std::string host = hostHeaderName(req.authority);
	m.server = selectServer(origin, host);
	if (m.server == NULL) {
		m.errorStatus  = 404;
		m.errorMessage = "no server matches the connection listener";
		return m;
	}

	// Path normalization (with .. resolution).
	std::string src = req.path.empty() ? std::string("/") : req.path;
	if (!normalizePath(src, m.normalizedPath)) {
		m.errorStatus  = 400;
		m.errorMessage = "path escapes root after normalization";
		return m;
	}

	// Location selection (nested).
	m.location = matchLocation(m.server->locations, m.normalizedPath);
	return m;
}

} // namespace webserv
