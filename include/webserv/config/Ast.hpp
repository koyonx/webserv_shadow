#ifndef WEBSERV_CONFIG_AST_HPP
#define WEBSERV_CONFIG_AST_HPP

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace webserv {
namespace config {

struct Directive {
	std::string              name;
	std::vector<std::string> args;
	std::size_t              line;

	Directive();
	Directive(const std::string &n, std::size_t l);
};

struct LocationNode {
	std::string               path;
	std::vector<Directive>    directives;
	std::vector<LocationNode> locations;   // nested locations
	std::size_t               line;

	LocationNode();
};

struct ServerNode {
	std::vector<Directive>    directives;
	std::vector<LocationNode> locations;
	std::size_t               line;

	ServerNode();
};

struct HttpNode {
	std::vector<Directive>    directives;
	std::vector<ServerNode>   servers;
	std::size_t               line;

	HttpNode();
};

struct ConfigAst {
	std::vector<Directive>  directives;    // top-level directives (rare)
	std::vector<HttpNode>   httpBlocks;    // wrapped: http { server { ... } }
	std::vector<ServerNode> serverBlocks;  // direct top-level servers
	std::string             filename;
};

// Pretty-print the AST for debugging.
void dumpAst(const ConfigAst &ast, std::ostream &os);

} // namespace config
} // namespace webserv

#endif
