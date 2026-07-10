#include "webserv/Error.hpp"
#include "webserv/Log.hpp"
#include "webserv/config/Ast.hpp"
#include "webserv/config/Config.hpp"
#include "webserv/config/Parser.hpp"
#include "webserv/config/Validator.hpp"

#include <iostream>

static const char *kEmbeddedSample =
	"http {\n"
	"    client_max_body_size 1m;\n"
	"    server {\n"
	"        listen 0.0.0.0:8080;\n"
	"        server_name embedded.local;\n"
	"        location / {\n"
	"            allowed_methods GET POST;\n"
	"        }\n"
	"    }\n"
	"}\n";

int main(int argc, char **argv)
{
	webserv::Log::setLevel(webserv::kLogInfo);
	LOG_INFO("webserv: config validator smoke test");

	try {
		webserv::config::ConfigAst ast;
		if (argc >= 2) {
			LOG_INFO("parsing file: " << argv[1]);
			ast = webserv::config::parseFile(argv[1]);
		} else {
			LOG_INFO("parsing embedded sample "
			         "(pass a .conf path as argv[1] to parse a file)");
			ast = webserv::config::parseString(kEmbeddedSample, "<embedded>");
		}
		webserv::config::Config cfg = webserv::config::validate(ast);
		std::cout << "--- resolved config ---\n";
		webserv::config::dumpConfig(cfg, std::cout);
		std::cout << "--- end ---\n";
	} catch (const webserv::ConfigError &e) {
		LOG_ERROR("config error: " << e.what());
		return 1;
	} catch (const webserv::Exception &e) {
		LOG_ERROR("error: " << e.what());
		return 1;
	}
	return 0;
}
