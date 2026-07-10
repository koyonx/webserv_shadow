#include "webserv/config/Lexer.hpp"

#include "webserv/Error.hpp"

namespace webserv {
namespace config {

Token::Token() : type(kTokEof), value(), line(0), col(0) {}

Token::Token(TokenType t, const std::string &v, std::size_t l, std::size_t c)
	: type(t), value(v), line(l), col(c) {}

const char *tokenTypeName(TokenType t)
{
	switch (t) {
		case kTokWord:   return "word";
		case kTokString: return "string";
		case kTokSemi:   return ";";
		case kTokLBrace: return "{";
		case kTokRBrace: return "}";
		case kTokEof:    return "EOF";
	}
	return "???";
}

static bool isWs(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Characters that terminate an unquoted word or start a distinct token.
static bool isSpecial(char c)
{
	return c == ';' || c == '{' || c == '}' || c == '#'
	    || c == '"' || c == '\'';
}

namespace {

class Cursor {
public:
	Cursor(const std::string &src, const std::string &file)
		: m_src(src), m_file(file), m_pos(0), m_line(1), m_col(1) {}

	bool          eof() const   { return m_pos >= m_src.size(); }
	char          peek() const  { return m_src[m_pos]; }
	std::size_t   line() const  { return m_line; }
	std::size_t   col()  const  { return m_col; }
	const std::string &file() const { return m_file; }

	char advance()
	{
		char c = m_src[m_pos++];
		if (c == '\n') { m_line++; m_col = 1; }
		else           { m_col++; }
		return c;
	}

private:
	const std::string &m_src;
	const std::string &m_file;
	std::size_t        m_pos;
	std::size_t        m_line;
	std::size_t        m_col;
};

Token readQuoted(Cursor &cur)
{
	std::size_t startLine = cur.line();
	std::size_t startCol  = cur.col();
	char        quote     = cur.advance();

	std::string val;
	while (!cur.eof() && cur.peek() != quote) {
		char c = cur.peek();
		if (c == '\\') {
			cur.advance();  // consume backslash
			if (cur.eof()) {
				throw ConfigError("dangling backslash in string literal",
				                  cur.file(), startLine);
			}
			char n = cur.advance();
			switch (n) {
				case 'n':  val += '\n'; break;
				case 't':  val += '\t'; break;
				case 'r':  val += '\r'; break;
				case '\\': val += '\\'; break;
				case '"':  val += '"';  break;
				case '\'': val += '\''; break;
				default:   val += n;    break;  // pass-through unknown escape
			}
			continue;
		}
		val += cur.advance();
	}
	if (cur.eof()) {
		throw ConfigError("unterminated string literal",
		                  cur.file(), startLine);
	}
	cur.advance();  // consume closing quote
	return Token(kTokString, val, startLine, startCol);
}

Token readWord(Cursor &cur)
{
	std::size_t startLine = cur.line();
	std::size_t startCol  = cur.col();
	std::string val;
	while (!cur.eof() && !isWs(cur.peek()) && !isSpecial(cur.peek())) {
		val += cur.advance();
	}
	return Token(kTokWord, val, startLine, startCol);
}

} // anonymous namespace

std::vector<Token> tokenize(const std::string &source,
                            const std::string &filename)
{
	std::vector<Token> tokens;
	Cursor             cur(source, filename);

	while (!cur.eof()) {
		char c = cur.peek();

		if (isWs(c)) {
			cur.advance();
			continue;
		}

		if (c == '#') {
			while (!cur.eof() && cur.peek() != '\n') {
				cur.advance();
			}
			continue;
		}

		std::size_t line = cur.line();
		std::size_t col  = cur.col();

		if (c == ';') { cur.advance(); tokens.push_back(Token(kTokSemi,   ";", line, col)); continue; }
		if (c == '{') { cur.advance(); tokens.push_back(Token(kTokLBrace, "{", line, col)); continue; }
		if (c == '}') { cur.advance(); tokens.push_back(Token(kTokRBrace, "}", line, col)); continue; }

		if (c == '"' || c == '\'') {
			tokens.push_back(readQuoted(cur));
			continue;
		}

		tokens.push_back(readWord(cur));
	}

	tokens.push_back(Token(kTokEof, "", cur.line(), cur.col()));
	return tokens;
}

} // namespace config
} // namespace webserv
