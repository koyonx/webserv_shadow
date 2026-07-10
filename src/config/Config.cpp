#include "webserv/config/Config.hpp"

#include "webserv/StringUtil.hpp"

namespace webserv {
namespace config {

Listen::Listen()                        : host("0.0.0.0"), port(0) {}
Listen::Listen(const std::string &h, int p) : host(h),      port(p) {}

bool Listen::operator==(const Listen &o) const
{
	return host == o.host && port == o.port;
}

bool Listen::operator<(const Listen &o) const
{
	if (port != o.port) {
		return port < o.port;
	}
	return host < o.host;
}

Return::Return() : code(0), url() {}

LocationConfig::LocationConfig()
	: path(),
	  allowedMethods(),
	  autoindex(false),
	  root(),
	  indexes(),
	  maxBodySize(1024UL * 1024UL),
	  errorPages(),
	  uploadStore(),
	  cgiPass(),
	  hasReturn(false),
	  ret(),
	  locations()
{}

ServerConfig::ServerConfig()
	: listens(),
	  serverNames(),
	  autoindex(false),
	  root(),
	  indexes(),
	  maxBodySize(1024UL * 1024UL),
	  errorPages(),
	  keepaliveTimeout(65),
	  clientBodyTimeout(60),
	  sendTimeout(60),
	  locations()
{}

Config::Config() : servers() {}

static void indent(std::ostream &os, std::size_t n)
{
	for (std::size_t i = 0; i < n; ++i) {
		os << "  ";
	}
}

static void dumpErrorPages(const std::map<int, std::string> &pages,
                           std::ostream                     &os,
                           std::size_t                       depth)
{
	for (std::map<int, std::string>::const_iterator it = pages.begin();
	     it != pages.end(); ++it) {
		indent(os, depth);
		os << "error_page " << it->first << " " << it->second << ";\n";
	}
}

static void dumpLocation(const LocationConfig &loc,
                         std::ostream         &os,
                         std::size_t           depth)
{
	indent(os, depth);
	os << "location " << loc.path << " {\n";

	if (!loc.allowedMethods.empty()) {
		indent(os, depth + 1);
		os << "allowed_methods";
		for (std::size_t i = 0; i < loc.allowedMethods.size(); ++i) {
			os << ' ' << loc.allowedMethods[i];
		}
		os << ";\n";
	}
	indent(os, depth + 1); os << "autoindex " << (loc.autoindex ? "on" : "off") << ";\n";
	if (!loc.root.empty()) {
		indent(os, depth + 1); os << "root " << loc.root << ";\n";
	}
	if (!loc.indexes.empty()) {
		indent(os, depth + 1); os << "index";
		for (std::size_t i = 0; i < loc.indexes.size(); ++i) {
			os << ' ' << loc.indexes[i];
		}
		os << ";\n";
	}
	indent(os, depth + 1);
	os << "client_max_body_size " << loc.maxBodySize << ";\n";

	dumpErrorPages(loc.errorPages, os, depth + 1);

	if (!loc.uploadStore.empty()) {
		indent(os, depth + 1);
		os << "upload_store " << loc.uploadStore << ";\n";
	}
	for (std::map<std::string, std::string>::const_iterator it = loc.cgiPass.begin();
	     it != loc.cgiPass.end(); ++it) {
		indent(os, depth + 1);
		os << "cgi_pass " << it->first << " " << it->second << ";\n";
	}
	if (loc.hasReturn) {
		indent(os, depth + 1);
		os << "return " << loc.ret.code;
		if (!loc.ret.url.empty()) {
			os << " " << loc.ret.url;
		}
		os << ";\n";
	}
	for (std::size_t i = 0; i < loc.locations.size(); ++i) {
		dumpLocation(loc.locations[i], os, depth + 1);
	}
	indent(os, depth);
	os << "}\n";
}

static void dumpServer(const ServerConfig &srv,
                       std::ostream       &os,
                       std::size_t         depth)
{
	indent(os, depth);
	os << "server {\n";
	for (std::size_t i = 0; i < srv.listens.size(); ++i) {
		indent(os, depth + 1);
		os << "listen " << srv.listens[i].host
		   << ":" << srv.listens[i].port << ";\n";
	}
	if (!srv.serverNames.empty()) {
		indent(os, depth + 1); os << "server_name";
		for (std::size_t i = 0; i < srv.serverNames.size(); ++i) {
			os << ' ' << srv.serverNames[i];
		}
		os << ";\n";
	}
	indent(os, depth + 1); os << "autoindex " << (srv.autoindex ? "on" : "off") << ";\n";
	if (!srv.root.empty()) {
		indent(os, depth + 1); os << "root " << srv.root << ";\n";
	}
	if (!srv.indexes.empty()) {
		indent(os, depth + 1); os << "index";
		for (std::size_t i = 0; i < srv.indexes.size(); ++i) {
			os << ' ' << srv.indexes[i];
		}
		os << ";\n";
	}
	indent(os, depth + 1);
	os << "client_max_body_size " << srv.maxBodySize << ";\n";

	indent(os, depth + 1);
	os << "keepalive_timeout "    << srv.keepaliveTimeout   << ";\n";
	indent(os, depth + 1);
	os << "client_body_timeout "  << srv.clientBodyTimeout  << ";\n";
	indent(os, depth + 1);
	os << "send_timeout "         << srv.sendTimeout        << ";\n";

	dumpErrorPages(srv.errorPages, os, depth + 1);

	for (std::size_t i = 0; i < srv.locations.size(); ++i) {
		dumpLocation(srv.locations[i], os, depth + 1);
	}
	indent(os, depth);
	os << "}\n";
}

void dumpConfig(const Config &cfg, std::ostream &os)
{
	os << "# resolved config: " << cfg.servers.size() << " server(s)\n";
	for (std::size_t i = 0; i < cfg.servers.size(); ++i) {
		dumpServer(cfg.servers[i], os, 0);
	}
}

} // namespace config
} // namespace webserv
