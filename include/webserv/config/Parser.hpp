#ifndef WEBSERV_CONFIG_PARSER_HPP
#define WEBSERV_CONFIG_PARSER_HPP

#include "webserv/config/Ast.hpp"
#include "webserv/config/Lexer.hpp"

#include <string>
#include <vector>

namespace webserv {
namespace config {

// Parse an already-tokenized stream into an AST. The `filename`
// is embedded in ConfigError messages and stored on ConfigAst.
ConfigAst parse(const std::vector<Token> &tokens, const std::string &filename);

// Convenience: lex + parse from a source string.
ConfigAst parseString(const std::string &source, const std::string &filename);

// Convenience: read the file at `path`, lex, and parse. Throws
// ConfigError if the file cannot be opened.
ConfigAst parseFile(const std::string &path);

} // namespace config
} // namespace webserv

#endif
