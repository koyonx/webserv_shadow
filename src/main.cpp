#include "webserv/Error.hpp"
#include "webserv/Log.hpp"
#include "webserv/StringUtil.hpp"

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	webserv::Log::setLevel(webserv::kLogDebug);
	LOG_INFO("webserv: util layer smoke test");
	LOG_DEBUG("trim('  hi  ') = '" << webserv::strutil::trim("  hi  ") << "'");

	try {
		throw webserv::ConfigError("demo error", "example.conf", 42);
	} catch (const webserv::Exception &e) {
		LOG_WARN(e.what());
	}
	return 0;
}
