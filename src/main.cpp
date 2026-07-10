#include "webserv/Error.hpp"
#include "webserv/Fd.hpp"
#include "webserv/Log.hpp"
#include "webserv/config/Ast.hpp"
#include "webserv/config/Config.hpp"
#include "webserv/config/Parser.hpp"
#include "webserv/config/Validator.hpp"
#include "webserv/core/PollLoop.hpp"
#include "webserv/net/Listener.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

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

// --------------------- --test-loop shell ---------------------

class ShutdownAfterHandler : public webserv::IHandler {
public:
	ShutdownAfterHandler() : m_fired(false) {}
	int   fd()         const { return -1; }
	short wantEvents() const { return 0; }
	void  onReadable(webserv::PollLoop &) {}
	void  onWritable(webserv::PollLoop &) {}
	void  onTimeout(webserv::PollLoop &loop) {
		m_fired = true;
		LOG_INFO("timer fired -> requesting graceful stop");
		loop.requestStop(true);
		loop.remove(this);
	}
	bool fired() const { return m_fired; }
private:
	bool m_fired;
};

int runTestLoop()
{
	LOG_INFO("PollLoop --test-loop: constructing loop");
	webserv::PollLoop loop;
	ShutdownAfterHandler h;
	loop.add(&h);
	loop.setDeadline(&h, 300);
	LOG_INFO("running loop with 2s cap; expect timer at 300ms");
	loop.run(2000);
	LOG_INFO("loop exited; timer_fired=" << (h.fired() ? "yes" : "no"));
	return h.fired() ? 0 : 1;
}

// --------------------- --test-listener shell ---------------------

class LogAcceptSink : public webserv::IAcceptSink {
public:
	LogAcceptSink() : m_count(0) {}
	int count() const { return m_count; }

	void onAccept(int                                    cfd,
	              const webserv::config::Listen         &origin,
	              webserv::PollLoop                     & /*loop*/)
	{
		++m_count;
		LOG_INFO("accepted client fd=" << cfd
		         << " on " << origin.host << ":" << origin.port
		         << " (closing immediately for smoke test)");
		::close(cfd);
	}
private:
	int m_count;
};

int runTestListener(int port, long runMs)
{
	webserv::PollLoop      loop;
	webserv::config::Listen cfg("127.0.0.1", port);
	LogAcceptSink          sink;
	webserv::Listener      listener(cfg, sink);
	listener.bindAndListen();
	loop.add(&listener);
	LOG_INFO("listening on 127.0.0.1:" << port << " for " << runMs << "ms; "
	         << "SIGINT or timeout stops the loop");
	loop.run(runMs);
	LOG_INFO("--test-listener exit; accepted=" << sink.count());
	return 0;
}

} // namespace

int main(int argc, char **argv)
{
	webserv::Log::setLevel(webserv::kLogInfo);

	if (argc >= 2 && std::strcmp(argv[1], "--test-loop") == 0) {
		return runTestLoop();
	}
	if (argc >= 2 && std::strcmp(argv[1], "--test-listener") == 0) {
		int  port  = (argc >= 3) ? std::atoi(argv[2]) : 18080;
		long runMs = (argc >= 4) ? std::atol(argv[3]) : 2000;
		try {
			return runTestListener(port, runMs);
		} catch (const webserv::Exception &e) {
			LOG_ERROR("listener: " << e.what());
			return 1;
		}
	}

	LOG_INFO("webserv: config validator smoke test");
	try {
		webserv::config::ConfigAst ast;
		if (argc >= 2) {
			LOG_INFO("parsing file: " << argv[1]);
			ast = webserv::config::parseFile(argv[1]);
		} else {
			LOG_INFO("parsing embedded sample "
			         "(pass a .conf path as argv[1], "
			         "or --test-loop / --test-listener)");
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
