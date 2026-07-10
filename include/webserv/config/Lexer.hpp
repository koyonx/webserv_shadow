#ifndef WEBSERV_CONFIG_LEXER_HPP
#define WEBSERV_CONFIG_LEXER_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace webserv {
namespace config {

enum TokenType {
	kTokWord,    // bare identifier / path / number
	kTokString,  // quoted "..." or '...' with escape handling
	kTokSemi,    // ;
	kTokLBrace,  // {
	kTokRBrace,  // }
	kTokEof
};

struct Token {
	TokenType   type;
	std::string value;
	std::size_t line;  // 1-indexed at token start
	std::size_t col;   // 1-indexed at token start

	Token();
	Token(TokenType t, const std::string &v, std::size_t l, std::size_t c);
};

const char *tokenTypeName(TokenType t);

// Batch-tokenize the whole source. `filename` is used only for
// ConfigError messages. Throws ConfigError on lexical errors
// (unterminated string, illegal escape).
std::vector<Token> tokenize(const std::string &source,
                            const std::string &filename);

} // namespace config
} // namespace webserv

#endif
