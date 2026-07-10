#include "webserv/config/Validator.hpp"

#include "webserv/Error.hpp"
#include "webserv/StringUtil.hpp"

#include <climits>
#include <sstream>

namespace webserv {
namespace config {

namespace {

// ---------- small helpers ----------

std::string quote(const std::string &s)
{
	std::string out = "'";
	out += s;
	out += "'";
	return out;
}

void expectArgs(const Directive &d,
                std::size_t      min,
                std::size_t      max,
                const std::string &file)
{
	if (d.args.size() < min || d.args.size() > max) {
		std::ostringstream oss;
		oss << "directive " << quote(d.name) << " expects ";
		if (min == max) {
			oss << min << " arg" << (min == 1 ? "" : "s");
		} else {
			oss << "between " << min << " and " << max << " args";
		}
		oss << ", got " << d.args.size();
		throw ConfigError(oss.str(), file, d.line);
	}
}

void expectArgsAtLeast(const Directive &d,
                       std::size_t      min,
                       const std::string &file)
{
	if (d.args.size() < min) {
		std::ostringstream oss;
		oss << "directive " << quote(d.name) << " expects at least "
		    << min << " arg" << (min == 1 ? "" : "s")
		    << ", got " << d.args.size();
		throw ConfigError(oss.str(), file, d.line);
	}
}

int parseIntArg(const std::string &s,
                long               min,
                long               max,
                const Directive   &d,
                std::size_t        idx,
                const std::string &file)
{
	long v = 0;
	if (!strutil::parseLong(s, v) || v < min || v > max) {
		std::ostringstream oss;
		oss << "invalid integer for " << quote(d.name)
		    << " arg[" << idx << "] " << quote(s)
		    << " (expected " << min << ".." << max << ")";
		throw ConfigError(oss.str(), file, d.line);
	}
	return static_cast<int>(v);
}

std::size_t parseSizeArg(const std::string &s,
                         const Directive   &d,
                         std::size_t        idx,
                         const std::string &file)
{
	std::size_t v = 0;
	if (!strutil::parseSize(s, v)) {
		std::ostringstream oss;
		oss << "invalid size for " << quote(d.name)
		    << " arg[" << idx << "] " << quote(s);
		throw ConfigError(oss.str(), file, d.line);
	}
	return v;
}

bool parseOnOff(const std::string &s, bool &out)
{
	if (s == "on")  { out = true;  return true; }
	if (s == "off") { out = false; return true; }
	return false;
}

Listen parseListen(const std::string &arg,
                   const Directive   &d,
                   const std::string &file)
{
	Listen l;
	std::string::size_type colon = arg.rfind(':');
	std::string            host;
	std::string            portStr;
	if (colon != std::string::npos) {
		host    = arg.substr(0, colon);
		portStr = arg.substr(colon + 1);
	} else {
		long p = 0;
		if (strutil::parseLong(arg, p)) {
			host    = "0.0.0.0";
			portStr = arg;
		} else {
			// Host only. Default to port 80.
			host    = arg;
			portStr = "80";
		}
	}
	if (host.empty()) {
		host = "0.0.0.0";
	}
	long p = 0;
	if (!strutil::parseLong(portStr, p) || p < 1 || p > 65535) {
		std::ostringstream oss;
		oss << "invalid listen port " << quote(portStr);
		throw ConfigError(oss.str(), file, d.line);
	}
	l.host = host;
	l.port = static_cast<int>(p);
	return l;
}

void mergeErrorPages(const Directive                &d,
                     std::map<int, std::string>     &target,
                     const std::string              &file)
{
	// syntax: error_page CODE [CODE...] PATH;
	expectArgsAtLeast(d, 2, file);
	std::size_t last = d.args.size() - 1;
	const std::string &path = d.args[last];
	for (std::size_t i = 0; i < last; ++i) {
		int code = parseIntArg(d.args[i], 100, 599, d, i, file);
		target[code] = path;
	}
}

std::vector<std::string> upperAll(const std::vector<std::string> &args)
{
	std::vector<std::string> out;
	out.reserve(args.size());
	for (std::size_t i = 0; i < args.size(); ++i) {
		out.push_back(strutil::toUpper(args[i]));
	}
	return out;
}

// ---------- directive dispatchers ----------
//
// applyCommon() handles directives valid in http/server/location scope.
// applyLocation() layers location-only directives on top.
// applyServer()   layers server-only directives on top.
// Http-only currently equals "common", so we reuse applyCommon().

struct CommonSettings {
	bool                        autoindexSet;   bool autoindex;
	bool                        rootSet;        std::string root;
	bool                        indexesSet;     std::vector<std::string> indexes;
	bool                        maxBodySet;     std::size_t maxBodySize;
	std::map<int, std::string>  errorPages;     // merged additively

	// timeouts (server/http)
	bool keepaliveSet;    int keepaliveTimeout;
	bool clientBodySet;   int clientBodyTimeout;
	bool sendTimeoutSet;  int sendTimeout;

	CommonSettings()
		: autoindexSet(false),  autoindex(false),
		  rootSet(false),       root(),
		  indexesSet(false),    indexes(),
		  maxBodySet(false),    maxBodySize(0),
		  errorPages(),
		  keepaliveSet(false),  keepaliveTimeout(0),
		  clientBodySet(false), clientBodyTimeout(0),
		  sendTimeoutSet(false),sendTimeout(0)
	{}
};

// Returns true if the directive was consumed at "common" scope, false
// if it belongs to a more-specific scope (or is unknown).
bool applyCommon(const Directive   &d,
                 CommonSettings    &out,
                 const std::string &file)
{
	if (d.name == "autoindex") {
		expectArgs(d, 1, 1, file);
		if (!parseOnOff(d.args[0], out.autoindex)) {
			throw ConfigError("autoindex expects 'on' or 'off'", file, d.line);
		}
		out.autoindexSet = true;
		return true;
	}
	if (d.name == "root") {
		expectArgs(d, 1, 1, file);
		out.root    = d.args[0];
		out.rootSet = true;
		return true;
	}
	if (d.name == "index") {
		expectArgsAtLeast(d, 1, file);
		out.indexes    = d.args;
		out.indexesSet = true;
		return true;
	}
	if (d.name == "client_max_body_size") {
		expectArgs(d, 1, 1, file);
		out.maxBodySize = parseSizeArg(d.args[0], d, 0, file);
		out.maxBodySet  = true;
		return true;
	}
	if (d.name == "error_page") {
		mergeErrorPages(d, out.errorPages, file);
		return true;
	}
	if (d.name == "keepalive_timeout") {
		expectArgs(d, 1, 1, file);
		out.keepaliveTimeout = parseIntArg(d.args[0], 0, 3600, d, 0, file);
		out.keepaliveSet     = true;
		return true;
	}
	if (d.name == "client_body_timeout") {
		expectArgs(d, 1, 1, file);
		out.clientBodyTimeout = parseIntArg(d.args[0], 1, 3600, d, 0, file);
		out.clientBodySet     = true;
		return true;
	}
	if (d.name == "send_timeout") {
		expectArgs(d, 1, 1, file);
		out.sendTimeout    = parseIntArg(d.args[0], 1, 3600, d, 0, file);
		out.sendTimeoutSet = true;
		return true;
	}
	return false;
}

// ---------- inheritance helpers ----------

void seedFromServer(LocationConfig &dst, const ServerConfig &src)
{
	dst.autoindex   = src.autoindex;
	dst.root        = src.root;
	dst.indexes     = src.indexes;
	dst.maxBodySize = src.maxBodySize;
	dst.errorPages  = src.errorPages;
}

void seedFromParent(LocationConfig &dst, const LocationConfig &parent)
{
	dst.autoindex      = parent.autoindex;
	dst.root           = parent.root;
	dst.indexes        = parent.indexes;
	dst.maxBodySize    = parent.maxBodySize;
	dst.errorPages     = parent.errorPages;
	dst.allowedMethods = parent.allowedMethods;
	dst.uploadStore    = parent.uploadStore;
	dst.cgiPass        = parent.cgiPass;
	// Do NOT inherit hasReturn/ret — a return is local.
}

// ---------- location visitor ----------

void applyLocationDirective(const Directive   &d,
                            LocationConfig    &loc,
                            const std::string &file)
{
	CommonSettings tmp;
	tmp.autoindex   = loc.autoindex;
	tmp.root        = loc.root;
	tmp.indexes     = loc.indexes;
	tmp.maxBodySize = loc.maxBodySize;
	tmp.errorPages  = loc.errorPages;

	if (applyCommon(d, tmp, file)) {
		if (tmp.autoindexSet) { loc.autoindex   = tmp.autoindex; }
		if (tmp.rootSet)      { loc.root        = tmp.root; }
		if (tmp.indexesSet)   { loc.indexes     = tmp.indexes; }
		if (tmp.maxBodySet)   { loc.maxBodySize = tmp.maxBodySize; }
		for (std::map<int, std::string>::const_iterator it = tmp.errorPages.begin();
		     it != tmp.errorPages.end(); ++it) {
			loc.errorPages[it->first] = it->second;
		}
		if (tmp.keepaliveSet || tmp.clientBodySet || tmp.sendTimeoutSet) {
			throw ConfigError("timeout directives are not allowed inside location",
			                  file, d.line);
		}
		return;
	}

	if (d.name == "allowed_methods") {
		expectArgsAtLeast(d, 1, file);
		std::vector<std::string> methods = upperAll(d.args);
		for (std::size_t i = 0; i < methods.size(); ++i) {
			const std::string &m = methods[i];
			if (m != "GET" && m != "POST" && m != "DELETE") {
				throw ConfigError("allowed_methods: unsupported method " + quote(m),
				                  file, d.line);
			}
		}
		loc.allowedMethods = methods;
		return;
	}
	if (d.name == "upload_store") {
		expectArgs(d, 1, 1, file);
		loc.uploadStore = d.args[0];
		return;
	}
	if (d.name == "cgi_pass") {
		expectArgs(d, 2, 2, file);
		const std::string &ext  = d.args[0];
		const std::string &prog = d.args[1];
		if (ext.empty() || ext[0] != '.') {
			throw ConfigError("cgi_pass: extension must start with '.'",
			                  file, d.line);
		}
		loc.cgiPass[ext] = prog;
		return;
	}
	if (d.name == "return") {
		expectArgs(d, 1, 2, file);
		loc.ret.code = parseIntArg(d.args[0], 100, 599, d, 0, file);
		loc.ret.url  = (d.args.size() >= 2) ? d.args[1] : std::string();
		loc.hasReturn = true;
		return;
	}
	throw ConfigError("unknown directive " + quote(d.name)
	                  + " inside location", file, d.line);
}

void buildLocation(const LocationNode  &node,
                   LocationConfig      &out,
                   const LocationConfig &seed,
                   const std::string   &file)
{
	out = seed;
	out.path = node.path;
	// allowedMethods & upload/cgi/return start from parent's — see seedFromParent.
	for (std::size_t i = 0; i < node.directives.size(); ++i) {
		applyLocationDirective(node.directives[i], out, file);
	}
	// Nested locations inherit from THIS location's resolved state.
	for (std::size_t i = 0; i < node.locations.size(); ++i) {
		LocationConfig child;
		LocationConfig parentSeed;
		seedFromParent(parentSeed, out);
		buildLocation(node.locations[i], child, parentSeed, file);
		out.locations.push_back(child);
	}
}

// ---------- server visitor ----------

void applyServerDirective(const Directive   &d,
                          ServerConfig      &srv,
                          const std::string &file)
{
	CommonSettings tmp;
	tmp.autoindex        = srv.autoindex;
	tmp.root             = srv.root;
	tmp.indexes          = srv.indexes;
	tmp.maxBodySize      = srv.maxBodySize;
	tmp.errorPages       = srv.errorPages;
	tmp.keepaliveTimeout = srv.keepaliveTimeout;
	tmp.clientBodyTimeout= srv.clientBodyTimeout;
	tmp.sendTimeout      = srv.sendTimeout;

	if (applyCommon(d, tmp, file)) {
		if (tmp.autoindexSet)  { srv.autoindex         = tmp.autoindex; }
		if (tmp.rootSet)       { srv.root              = tmp.root; }
		if (tmp.indexesSet)    { srv.indexes           = tmp.indexes; }
		if (tmp.maxBodySet)    { srv.maxBodySize       = tmp.maxBodySize; }
		if (tmp.keepaliveSet)  { srv.keepaliveTimeout  = tmp.keepaliveTimeout; }
		if (tmp.clientBodySet) { srv.clientBodyTimeout = tmp.clientBodyTimeout; }
		if (tmp.sendTimeoutSet){ srv.sendTimeout       = tmp.sendTimeout; }
		for (std::map<int, std::string>::const_iterator it = tmp.errorPages.begin();
		     it != tmp.errorPages.end(); ++it) {
			srv.errorPages[it->first] = it->second;
		}
		return;
	}
	if (d.name == "listen") {
		expectArgs(d, 1, 1, file);
		Listen l = parseListen(d.args[0], d, file);
		for (std::size_t i = 0; i < srv.listens.size(); ++i) {
			if (srv.listens[i] == l) {
				std::ostringstream oss;
				oss << "duplicate 'listen " << l.host << ":" << l.port
				    << "' in this server block";
				throw ConfigError(oss.str(), file, d.line);
			}
		}
		srv.listens.push_back(l);
		return;
	}
	if (d.name == "server_name") {
		expectArgsAtLeast(d, 1, file);
		for (std::size_t i = 0; i < d.args.size(); ++i) {
			srv.serverNames.push_back(strutil::toLower(d.args[i]));
		}
		return;
	}
	throw ConfigError("unknown directive " + quote(d.name)
	                  + " inside server", file, d.line);
}

void buildServer(const ServerNode  &node,
                 ServerConfig      &out,
                 const ServerConfig &seed,
                 const std::string &file)
{
	out = seed;
	// Apply server-scope directives.
	for (std::size_t i = 0; i < node.directives.size(); ++i) {
		applyServerDirective(node.directives[i], out, file);
	}
	// Build locations.
	for (std::size_t i = 0; i < node.locations.size(); ++i) {
		LocationConfig loc;
		LocationConfig seedLoc;
		seedFromServer(seedLoc, out);
		buildLocation(node.locations[i], loc, seedLoc, file);
		out.locations.push_back(loc);
	}
	// Final validation.
	if (out.listens.empty()) {
		throw ConfigError("server block has no 'listen' directive",
		                  file, node.line);
	}
}

// ---------- http visitor ----------

void applyHttpDirective(const Directive  &d,
                        ServerConfig     &defaults,
                        const std::string &file)
{
	// http-scope currently supports only common directives.
	CommonSettings tmp;
	tmp.autoindex        = defaults.autoindex;
	tmp.root             = defaults.root;
	tmp.indexes          = defaults.indexes;
	tmp.maxBodySize      = defaults.maxBodySize;
	tmp.errorPages       = defaults.errorPages;
	tmp.keepaliveTimeout = defaults.keepaliveTimeout;
	tmp.clientBodyTimeout= defaults.clientBodyTimeout;
	tmp.sendTimeout      = defaults.sendTimeout;

	if (applyCommon(d, tmp, file)) {
		if (tmp.autoindexSet)  { defaults.autoindex         = tmp.autoindex; }
		if (tmp.rootSet)       { defaults.root              = tmp.root; }
		if (tmp.indexesSet)    { defaults.indexes           = tmp.indexes; }
		if (tmp.maxBodySet)    { defaults.maxBodySize       = tmp.maxBodySize; }
		if (tmp.keepaliveSet)  { defaults.keepaliveTimeout  = tmp.keepaliveTimeout; }
		if (tmp.clientBodySet) { defaults.clientBodyTimeout = tmp.clientBodyTimeout; }
		if (tmp.sendTimeoutSet){ defaults.sendTimeout       = tmp.sendTimeout; }
		for (std::map<int, std::string>::const_iterator it = tmp.errorPages.begin();
		     it != tmp.errorPages.end(); ++it) {
			defaults.errorPages[it->first] = it->second;
		}
		return;
	}
	throw ConfigError("unknown directive " + quote(d.name) + " inside http",
	                  file, d.line);
}

} // anonymous namespace

Config validate(const ConfigAst &ast)
{
	const std::string &file = ast.filename;
	Config             cfg;

	// Reject any stray top-level directives (we do not currently model them).
	if (!ast.directives.empty()) {
		const Directive &d = ast.directives[0];
		throw ConfigError("top-level directive " + std::string("'") + d.name +
		                  "' outside http/server is not supported",
		                  file, d.line);
	}

	// Enforce at-most-one http { } block.
	if (ast.httpBlocks.size() > 1) {
		throw ConfigError("multiple 'http' blocks are not allowed",
		                  file, ast.httpBlocks[1].line);
	}

	// Compute http-scope defaults (may be empty if no http { } was used).
	ServerConfig httpDefaults; // starts from ServerConfig() defaults
	if (!ast.httpBlocks.empty()) {
		const HttpNode &http = ast.httpBlocks[0];
		for (std::size_t i = 0; i < http.directives.size(); ++i) {
			applyHttpDirective(http.directives[i], httpDefaults, file);
		}
		// Build servers inside http.
		for (std::size_t i = 0; i < http.servers.size(); ++i) {
			ServerConfig srv;
			buildServer(http.servers[i], srv, httpDefaults, file);
			cfg.servers.push_back(srv);
		}
	}

	// Bare top-level server blocks inherit from the same defaults
	// (or from ServerConfig() defaults if no http { } was present).
	for (std::size_t i = 0; i < ast.serverBlocks.size(); ++i) {
		ServerConfig srv;
		buildServer(ast.serverBlocks[i], srv, httpDefaults, file);
		cfg.servers.push_back(srv);
	}

	if (cfg.servers.empty()) {
		throw ConfigError("no 'server' block found", file, 0);
	}

	// Cross-server validation: ensure no two servers share the same
	// listen + server_name pair.
	for (std::size_t i = 0; i < cfg.servers.size(); ++i) {
		const ServerConfig &a = cfg.servers[i];
		for (std::size_t j = i + 1; j < cfg.servers.size(); ++j) {
			const ServerConfig &b = cfg.servers[j];
			for (std::size_t li = 0; li < a.listens.size(); ++li) {
				for (std::size_t lj = 0; lj < b.listens.size(); ++lj) {
					if (!(a.listens[li] == b.listens[lj])) {
						continue;
					}
					// Same listen — if server_names overlap, complain.
					for (std::size_t na = 0; na < a.serverNames.size(); ++na) {
						for (std::size_t nb = 0; nb < b.serverNames.size(); ++nb) {
							if (a.serverNames[na] == b.serverNames[nb]) {
								std::ostringstream oss;
								oss << "duplicate server_name '"
								    << a.serverNames[na] << "' on "
								    << a.listens[li].host << ":"
								    << a.listens[li].port;
								throw ConfigError(oss.str(), file, 0);
							}
						}
					}
				}
			}
		}
	}

	return cfg;
}

} // namespace config
} // namespace webserv
