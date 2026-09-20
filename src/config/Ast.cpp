#include "webserv/config/Ast.hpp"

namespace webserv {
namespace config {

Directive::Directive() : name(), args(), line(0) {}
Directive::Directive(const std::string &n, std::size_t l)
	: name(n), args(), line(l) {}

LocationNode::LocationNode() : path(), directives(), locations(), line(0) {}
ServerNode::ServerNode()     : directives(), locations(), line(0) {}
HttpNode::HttpNode()         : directives(), servers(),   line(0) {}

static void indent(std::ostream &os, std::size_t n)
{
	for (std::size_t i = 0; i < n; ++i) {
		os << "  ";
	}
}

static void dumpDirective(const Directive &d, std::ostream &os, std::size_t depth)
{
	indent(os, depth);
	os << d.name;
	for (std::size_t i = 0; i < d.args.size(); ++i) {
		os << ' ' << d.args[i];
	}
	os << ";  // line " << d.line << "\n";
}

static void dumpLocation(const LocationNode &loc,
                         std::ostream       &os,
                         std::size_t         depth)
{
	indent(os, depth);
	os << "location " << loc.path << " {  // line " << loc.line << "\n";
	for (std::size_t i = 0; i < loc.directives.size(); ++i) {
		dumpDirective(loc.directives[i], os, depth + 1);
	}
	for (std::size_t i = 0; i < loc.locations.size(); ++i) {
		dumpLocation(loc.locations[i], os, depth + 1);
	}
	indent(os, depth);
	os << "}\n";
}

static void dumpServer(const ServerNode &srv,
                       std::ostream     &os,
                       std::size_t       depth)
{
	indent(os, depth);
	os << "server {  // line " << srv.line << "\n";
	for (std::size_t i = 0; i < srv.directives.size(); ++i) {
		dumpDirective(srv.directives[i], os, depth + 1);
	}
	for (std::size_t i = 0; i < srv.locations.size(); ++i) {
		dumpLocation(srv.locations[i], os, depth + 1);
	}
	indent(os, depth);
	os << "}\n";
}

static void dumpHttp(const HttpNode &http,
                     std::ostream   &os,
                     std::size_t     depth)
{
	indent(os, depth);
	os << "http {  // line " << http.line << "\n";
	for (std::size_t i = 0; i < http.directives.size(); ++i) {
		dumpDirective(http.directives[i], os, depth + 1);
	}
	for (std::size_t i = 0; i < http.servers.size(); ++i) {
		dumpServer(http.servers[i], os, depth + 1);
	}
	indent(os, depth);
	os << "}\n";
}

void dumpAst(const ConfigAst &ast, std::ostream &os)
{
	os << "# " << ast.filename << "\n";
	for (std::size_t i = 0; i < ast.directives.size(); ++i) {
		dumpDirective(ast.directives[i], os, 0);
	}
	for (std::size_t i = 0; i < ast.httpBlocks.size(); ++i) {
		dumpHttp(ast.httpBlocks[i], os, 0);
	}
	for (std::size_t i = 0; i < ast.serverBlocks.size(); ++i) {
		dumpServer(ast.serverBlocks[i], os, 0);
	}
}

} // namespace config
} // namespace webserv
