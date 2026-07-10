#include "webserv/Error.hpp"
#include "webserv/Fd.hpp"
#include "webserv/Log.hpp"
#include "webserv/StringUtil.hpp"
#include "webserv/config/Lexer.hpp"

#include <unistd.h>

static void demoUtilAndFd()
{
	LOG_DEBUG("trim('  hi  ') = '" << webserv::strutil::trim("  hi  ") << "'");
	int fds[2];
	if (::pipe(fds) == 0) {
		webserv::Fd rd(fds[0]);
		webserv::Fd wr(fds[1]);
		webserv::setNonBlocking(rd.get());
		LOG_DEBUG("pipe: rd=" << rd.get() << " wr=" << wr.get()
		          << " (O_NONBLOCK set on rd)");
	}
}

static void demoLexer()
{
	const std::string sample =
		"# a comment\n"
		"http {\n"
		"    server {\n"
		"        listen 0.0.0.0:8080;\n"
		"        server_name example.com \"example org\";\n"
		"        root /var/www;\n"
		"        location / {\n"
		"            allowed_methods GET POST;\n"
		"        }\n"
		"    }\n"
		"}\n";

	std::vector<webserv::config::Token> toks =
		webserv::config::tokenize(sample, "<embedded>");
	LOG_INFO("lexer: " << toks.size() << " tokens (incl. EOF)");
	for (std::size_t i = 0; i < toks.size(); ++i) {
		LOG_DEBUG("  [" << i << "] "
		          << webserv::config::tokenTypeName(toks[i].type)
		          << " '" << toks[i].value << "' @"
		          << toks[i].line << ":" << toks[i].col);
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	webserv::Log::setLevel(webserv::kLogDebug);
	LOG_INFO("webserv: config-lexer smoke test");

	demoUtilAndFd();

	try {
		demoLexer();
	} catch (const webserv::Exception &e) {
		LOG_ERROR(e.what());
		return 1;
	}
	return 0;
}
