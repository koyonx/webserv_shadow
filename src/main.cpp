#include "webserv/Error.hpp"
#include "webserv/Fd.hpp"
#include "webserv/Log.hpp"
#include "webserv/StringUtil.hpp"

#include <unistd.h>

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	webserv::Log::setLevel(webserv::kLogDebug);
	LOG_INFO("webserv: util+fd smoke test");
	LOG_DEBUG("trim('  hi  ') = '" << webserv::strutil::trim("  hi  ") << "'");

	{
		webserv::Fd empty;
		LOG_DEBUG("empty Fd: valid=" << (empty.valid() ? "yes" : "no")
		          << " get=" << empty.get());
	}

	int fds[2];
	if (::pipe(fds) == 0) {
		webserv::Fd rd(fds[0]);
		webserv::Fd wr(fds[1]);
		try {
			webserv::setNonBlocking(rd.get());
			LOG_DEBUG("pipe: rd=" << rd.get() << " wr=" << wr.get()
			          << " (O_NONBLOCK set on rd)");
		} catch (const webserv::Exception &e) {
			LOG_ERROR(e.what());
		}
	} else {
		LOG_ERROR("pipe() failed");
	}

	try {
		throw webserv::ConfigError("demo error", "example.conf", 42);
	} catch (const webserv::Exception &e) {
		LOG_WARN(e.what());
	}
	return 0;
}
