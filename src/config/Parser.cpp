#include "webserv/config/Parser.hpp"

#include "webserv/Error.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace webserv {
namespace config {

namespace {

class Parser {
public:
	Parser(const std::vector<Token> &tokens, const std::string &filename)
		: m_toks(tokens), m_file(filename), m_pos(0) {}

	ConfigAst run();

private:
	const Token       &peek() const   { return m_toks[m_pos]; }
	const Token       &consume()      { return m_toks[m_pos++]; }

	void  expectSemi(const Token &head);
	void  expectLBrace(const Token &head);
	void  expectRBrace(std::size_t openLine, const std::string &kind);

	void         parseTop(ConfigAst &out);
	HttpNode     parseHttp(const Token &head);
	ServerNode   parseServer(const Token &head);
	LocationNode parseLocation(const Token &head);
	Directive    parseDirective(const Token &nameTok);

	void err(const std::string &msg, std::size_t line) const;

	const std::vector<Token> &m_toks;
	std::string               m_file;
	std::size_t               m_pos;
};

void Parser::err(const std::string &msg, std::size_t line) const
{
	throw ConfigError(msg, m_file, line);
}

void Parser::expectSemi(const Token &head)
{
	if (peek().type != kTokSemi) {
		std::ostringstream oss;
		oss << "expected ';' after directive '" << head.value
		    << "', got " << tokenTypeName(peek().type);
		err(oss.str(), peek().line);
	}
	consume();
}

void Parser::expectLBrace(const Token &head)
{
	if (peek().type != kTokLBrace) {
		std::ostringstream oss;
		oss << "expected '{' after '" << head.value
		    << "', got " << tokenTypeName(peek().type);
		err(oss.str(), peek().line);
	}
	consume();
}

void Parser::expectRBrace(std::size_t openLine, const std::string &kind)
{
	if (peek().type != kTokRBrace) {
		std::ostringstream oss;
		oss << "unclosed " << kind << " block (opened at line "
		    << openLine << "); got " << tokenTypeName(peek().type);
		err(oss.str(), peek().line);
	}
	consume();
}

Directive Parser::parseDirective(const Token &nameTok)
{
	Directive d(nameTok.value, nameTok.line);
	while (peek().type == kTokWord || peek().type == kTokString) {
		d.args.push_back(consume().value);
	}
	expectSemi(nameTok);
	return d;
}

LocationNode Parser::parseLocation(const Token &head)
{
	LocationNode loc;
	loc.line = head.line;

	if (peek().type != kTokWord && peek().type != kTokString) {
		err("'location' requires a path argument", head.line);
	}
	loc.path = consume().value;

	expectLBrace(head);

	while (peek().type != kTokRBrace && peek().type != kTokEof) {
		if (peek().type != kTokWord) {
			std::ostringstream oss;
			oss << "expected directive or nested 'location', got "
			    << tokenTypeName(peek().type);
			err(oss.str(), peek().line);
		}
		Token head2 = consume();
		if (head2.value == "location") {
			loc.locations.push_back(parseLocation(head2));
		} else {
			loc.directives.push_back(parseDirective(head2));
		}
	}
	expectRBrace(loc.line, "location");
	return loc;
}

ServerNode Parser::parseServer(const Token &head)
{
	ServerNode srv;
	srv.line = head.line;
	expectLBrace(head);

	while (peek().type != kTokRBrace && peek().type != kTokEof) {
		if (peek().type != kTokWord) {
			std::ostringstream oss;
			oss << "expected directive or 'location', got "
			    << tokenTypeName(peek().type);
			err(oss.str(), peek().line);
		}
		Token head2 = consume();
		if (head2.value == "location") {
			srv.locations.push_back(parseLocation(head2));
		} else if (head2.value == "server" || head2.value == "http") {
			std::ostringstream oss;
			oss << "unexpected '" << head2.value
			    << "' block inside server";
			err(oss.str(), head2.line);
		} else {
			srv.directives.push_back(parseDirective(head2));
		}
	}
	expectRBrace(srv.line, "server");
	return srv;
}

HttpNode Parser::parseHttp(const Token &head)
{
	HttpNode http;
	http.line = head.line;
	expectLBrace(head);

	while (peek().type != kTokRBrace && peek().type != kTokEof) {
		if (peek().type != kTokWord) {
			std::ostringstream oss;
			oss << "expected directive or 'server', got "
			    << tokenTypeName(peek().type);
			err(oss.str(), peek().line);
		}
		Token head2 = consume();
		if (head2.value == "server") {
			http.servers.push_back(parseServer(head2));
		} else if (head2.value == "http" || head2.value == "location") {
			std::ostringstream oss;
			oss << "unexpected '" << head2.value
			    << "' block inside http";
			err(oss.str(), head2.line);
		} else {
			http.directives.push_back(parseDirective(head2));
		}
	}
	expectRBrace(http.line, "http");
	return http;
}

void Parser::parseTop(ConfigAst &out)
{
	while (peek().type != kTokEof) {
		if (peek().type != kTokWord) {
			std::ostringstream oss;
			oss << "expected directive, 'http', or 'server' at top level; got "
			    << tokenTypeName(peek().type);
			err(oss.str(), peek().line);
		}
		Token head = consume();
		if (head.value == "http") {
			out.httpBlocks.push_back(parseHttp(head));
		} else if (head.value == "server") {
			out.serverBlocks.push_back(parseServer(head));
		} else if (head.value == "location") {
			err("'location' is only valid inside a 'server' block", head.line);
		} else {
			out.directives.push_back(parseDirective(head));
		}
	}
}

ConfigAst Parser::run()
{
	ConfigAst ast;
	ast.filename = m_file;
	parseTop(ast);
	return ast;
}

} // anonymous namespace

ConfigAst parse(const std::vector<Token> &tokens, const std::string &filename)
{
	Parser p(tokens, filename);
	return p.run();
}

ConfigAst parseString(const std::string &source, const std::string &filename)
{
	std::vector<Token> toks = tokenize(source, filename);
	return parse(toks, filename);
}

ConfigAst parseFile(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in.is_open()) {
		std::ostringstream oss;
		oss << "cannot open config file '" << path << "': "
		    << std::strerror(errno);
		throw ConfigError(oss.str(), path, 0);
	}
	std::ostringstream buf;
	buf << in.rdbuf();
	return parseString(buf.str(), path);
}

} // namespace config
} // namespace webserv
