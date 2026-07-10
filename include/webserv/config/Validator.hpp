#ifndef WEBSERV_CONFIG_VALIDATOR_HPP
#define WEBSERV_CONFIG_VALIDATOR_HPP

#include "webserv/config/Ast.hpp"
#include "webserv/config/Config.hpp"

namespace webserv {
namespace config {

// Turn a parsed AST into a fully-resolved Config.
// - Inherits http-scope directives into every server
// - Inherits server-scope directives into every location (and nested)
// - Rejects unknown directives, bad arg counts, invalid values
// - Merges error_page maps (child overrides parent per-code)
// - Merges cgi_pass maps (child overrides parent per-extension)
// Throws ConfigError on any semantic problem.
Config validate(const ConfigAst &ast);

} // namespace config
} // namespace webserv

#endif
