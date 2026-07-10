#include "webserv/Error.hpp"
#include "webserv/Log.hpp"
#include "webserv/config/Ast.hpp"
#include "webserv/config/Config.hpp"
#include "webserv/config/Parser.hpp"
#include "webserv/config/Validator.hpp"
#include "webserv/core/PollLoop.hpp"

#include <cstring>
#include <iostream>

static const char *kEmbeddedSample =
	"http {\n"
	"    server {\n"
	"        listen 0.0.0.0:8080;\n"
	"        server_name embedded.local;\n"
	"        location / {\n"
	"            allowed_methods GET POST;\n"
	"        }\n"
	"    }\n"
	"}\n";

namespace {

// A trivial IHandler that fires a one-shot timer and then stops the
// loop. Used by --test-loop to prove the poll loop wakes on timers
// as well as on signals.
class ShutdownAfterHandler : public webserv::IHandler {
public:
	ShutdownAfterHandler() : m_fired(false) {}
	int   fd()          const { return -1; }         // no fd; timer-only
	short wantEvents()  const { return 0; }
	void  onReadable(webserv::PollLoop &) {}
	void  onWritable(webserv::PollLoop &) {}
	void  onTimeout(webserv::PollLoop &loop) {
		m_fired = true;
		LOG_INFO("timer fired -> requesting graceful stop");
		loop.requestStop(true);
		loop.remove(this);
	}
	bool  fired() const { return m_fired; }
private:
	bool m_fired;
};

int runTestLoop()
{
	LOG_INFO("PollLoop --test-loop: constructing loop");
	webserv::PollLoop loop;

	// A timer-only handler cannot be added via add() (which needs a
	// valid fd). We register it here just so its onTimeout fires when
	// the deadline expires — dispatchExpired() does an indexOf check
	// but a valid handler is required, so we add() first.
	ShutdownAfterHandler h;
	loop.add(&h);
	loop.setDeadline(&h, 300);

	LOG_INFO("running loop with 2s cap; expect timer at 300ms");
	loop.run(2000);
	LOG_INFO("loop exited; timer_fired=" << (h.fired() ? "yes" : "no"));
	return h.fired() ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
	webserv::Log::setLevel(webserv::kLogInfo);

	if (argc >= 2 && std::strcmp(argv[1], "--test-loop") == 0) {
		return runTestLoop();
	}

	LOG_INFO("webserv: config validator smoke test");
	try {
		webserv::config::ConfigAst ast;
		if (argc >= 2) {
			LOG_INFO("parsing file: " << argv[1]);
			ast = webserv::config::parseFile(argv[1]);
		} else {
			LOG_INFO("parsing embedded sample "
			         "(pass a .conf path as argv[1] to parse a file, "
			         "or --test-loop for the PollLoop smoke test)");
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
