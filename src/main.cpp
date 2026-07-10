#include "webserv/Error.hpp"
#include "webserv/Fd.hpp"
#include "webserv/Log.hpp"
#include "webserv/StringUtil.hpp"
#include "webserv/config/Ast.hpp"
#include "webserv/config/Config.hpp"
#include "webserv/config/Parser.hpp"
#include "webserv/config/Validator.hpp"
#include "webserv/core/PollLoop.hpp"
#include "webserv/net/Connection.hpp"
#include "webserv/net/ConnectionSpawner.hpp"
#include "webserv/net/Listener.hpp"
#include "webserv/net/Router.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
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

// --------------------- --test-loop ---------------------

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
	webserv::PollLoop     loop;
	ShutdownAfterHandler  h;
	loop.add(&h);
	loop.setDeadline(&h, 300);
	loop.run(2000);
	LOG_INFO("--test-loop exit; timer_fired=" << (h.fired() ? "yes" : "no"));
	return h.fired() ? 0 : 1;
}

// --------------------- --test-listener ---------------------

class LogAcceptSink : public webserv::IAcceptSink {
public:
	LogAcceptSink() : m_count(0) {}
	int count() const { return m_count; }
	void onAccept(int cfd, const webserv::config::Listen &origin,
	              webserv::PollLoop &) {
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
	webserv::PollLoop       loop;
	webserv::config::Listen cfg("127.0.0.1", port);
	LogAcceptSink           sink;
	webserv::Listener       listener(cfg, sink);
	listener.bindAndListen();
	loop.add(&listener);
	LOG_INFO("--test-listener on 127.0.0.1:" << port
	         << " for " << runMs << "ms");
	loop.run(runMs);
	LOG_INFO("--test-listener exit; accepted=" << sink.count());
	return 0;
}

// --------------------- --test-connection ---------------------

int runTestConnection(int port, long runMs)
{
	webserv::PollLoop          loop;
	webserv::ConnectionSpawner spawner(5000, 100);
	spawner.arm(loop);

	webserv::config::Listen    cfg("127.0.0.1", port);
	webserv::Listener          listener(cfg, spawner);
	listener.bindAndListen();
	loop.add(&listener);

	LOG_INFO("--test-connection on 127.0.0.1:" << port
	         << " for " << runMs << "ms (skeleton HTTP response)");
	loop.run(runMs);
	LOG_INFO("--test-connection exit; live=" << spawner.liveCount()
	         << " pending_dead=" << spawner.deadPendingCount());
	return 0;
}

// --------------------- --test-router ---------------------

// Runs a small suite against the Router. No sockets: purely exercises
// path normalization, server_name matching, and location prefix logic.

static const char *kRouterSample =
	"http {\n"
	"    server {\n"
	"        listen 0.0.0.0:8080;\n"
	"        server_name a.example.com;\n"
	"        location / { allowed_methods GET; }\n"
	"        location /api { allowed_methods GET POST; }\n"
	"        location /api/v1 { allowed_methods GET; }\n"
	"    }\n"
	"    server {\n"
	"        listen 0.0.0.0:8080;\n"
	"        server_name b.example.com;\n"
	"        location / { allowed_methods GET; }\n"
	"    }\n"
	"    server {\n"
	"        listen 0.0.0.0:8080;\n"
	"        server_name *.wildcard.com;\n"
	"        location / { allowed_methods GET; }\n"
	"    }\n"
	"}\n";

struct RouterCase {
	const char *host;
	const char *path;
	const char *expectServer;
	const char *expectLocation;
	const char *expectNormalized;
	int         expectStatus;
};

static int runTestRouter()
{
	webserv::config::ConfigAst ast =
		webserv::config::parseString(kRouterSample, "<router-embed>");
	webserv::config::Config    cfg = webserv::config::validate(ast);
	webserv::Router            router(cfg);
	webserv::config::Listen    origin("0.0.0.0", 8080);

	RouterCase cases[] = {
		{ "a.example.com", "/",             "a.example.com",       "/",       "/",       0 },
		{ "a.example.com", "/api/foo",      "a.example.com",       "/api",    "/api/foo",0 },
		{ "a.example.com", "/api/v1/x",     "a.example.com",       "/api/v1", "/api/v1/x",0 },
		{ "b.example.com", "/",             "b.example.com",       "/",       "/",       0 },
		{ "sub.wildcard.com", "/hello",     "*.wildcard.com",      "/",       "/hello",  0 },
		{ "unknown.host",   "/",            "a.example.com",       "/",       "/",       0 },  // default = first server
		{ "a.example.com", "/../etc/passwd","",                    "",        "",        400 },
		{ "a.example.com", "/a/../b/",      "a.example.com",       "/",       "/b/",     0 },
		{ "a.example.com", "/./././x",      "a.example.com",       "/",       "/x",      0 },
		{ "a.example.com", "/api/./v1/../v1/y", "a.example.com",   "/api/v1", "/api/v1/y",0 }
	};
	const std::size_t n = sizeof(cases) / sizeof(cases[0]);
	int failures = 0;

	for (std::size_t i = 0; i < n; ++i) {
		const RouterCase &c = cases[i];
		webserv::http::Request req;
		req.method   = "GET";
		req.path     = c.path;
		req.authority = c.host;
		req.version  = webserv::http::Version(1, 1);

		webserv::RouteMatch m = router.match(origin, req);

		std::string actualServer = "(none)";
		if (m.server != NULL && !m.server->serverNames.empty()) {
			actualServer = m.server->serverNames.front();
		}
		std::string actualLoc = (m.location != NULL) ? m.location->path
		                                             : std::string("(none)");
		std::string actualNorm = m.normalizedPath;
		int         actualStat = m.errorStatus;

		bool ok = true;
		if (c.expectStatus != 0) {
			ok = (actualStat == c.expectStatus);
		} else {
			if (actualStat != 0)                                 ok = false;
			if (std::string(c.expectServer) != actualServer)     ok = false;
			if (std::string(c.expectLocation) != actualLoc)      ok = false;
			if (std::string(c.expectNormalized) != actualNorm)   ok = false;
		}
		LOG_INFO((ok ? "PASS " : "FAIL ") << "[" << i << "] "
		         << c.host << c.path
		         << " -> server=" << actualServer
		         << " loc=" << actualLoc
		         << " norm=" << actualNorm
		         << " status=" << actualStat);
		if (!ok) ++failures;
	}
	LOG_INFO("--test-router: " << (n - failures) << "/" << n << " passed");
	return failures == 0 ? 0 : 1;
}

// --------------------- --serve ---------------------

int runServe(const std::string &confPath, long runMs)
{
	webserv::config::ConfigAst ast = webserv::config::parseFile(confPath);
	webserv::config::Config    cfg = webserv::config::validate(ast);
	webserv::Router            router(cfg);

	webserv::PollLoop          loop;
	webserv::ConnectionSpawner spawner(30000, 100, &router);
	spawner.arm(loop);

	// One Listener per unique (host, port) across all servers.
	std::set<webserv::config::Listen>       listens;
	std::vector<webserv::Listener *>        listeners;
	for (std::size_t i = 0; i < cfg.servers.size(); ++i) {
		const webserv::config::ServerConfig &s = cfg.servers[i];
		for (std::size_t j = 0; j < s.listens.size(); ++j) {
			listens.insert(s.listens[j]);
		}
	}
	try {
		for (std::set<webserv::config::Listen>::iterator it = listens.begin();
		     it != listens.end(); ++it) {
			webserv::Listener *lst = new webserv::Listener(*it, spawner);
			try {
				lst->bindAndListen();
			} catch (...) {
				delete lst;
				throw;
			}
			loop.add(lst);
			listeners.push_back(lst);
		}
	} catch (...) {
		for (std::size_t i = 0; i < listeners.size(); ++i) delete listeners[i];
		throw;
	}

	LOG_INFO("--serve: " << listeners.size() << " listener(s) up; "
	         "SIGINT to stop"
	         << (runMs > 0 ? (std::string(" or ") + webserv::strutil::toStr(runMs) + "ms cap") : ""));
	loop.run(runMs > 0 ? runMs : -1);
	LOG_INFO("--serve: loop exited; cleaning up");

	for (std::size_t i = 0; i < listeners.size(); ++i) delete listeners[i];
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
		try { return runTestListener(port, runMs); }
		catch (const webserv::Exception &e) { LOG_ERROR(e.what()); return 1; }
	}
	if (argc >= 2 && std::strcmp(argv[1], "--test-connection") == 0) {
		int  port  = (argc >= 3) ? std::atoi(argv[2]) : 18080;
		long runMs = (argc >= 4) ? std::atol(argv[3]) : 2000;
		try { return runTestConnection(port, runMs); }
		catch (const webserv::Exception &e) { LOG_ERROR(e.what()); return 1; }
	}
	if (argc >= 2 && std::strcmp(argv[1], "--test-router") == 0) {
		try { return runTestRouter(); }
		catch (const webserv::Exception &e) { LOG_ERROR(e.what()); return 1; }
	}
	if (argc >= 2 && std::strcmp(argv[1], "--serve") == 0) {
		if (argc < 3) { LOG_ERROR("--serve requires a config file path"); return 1; }
		long runMs = (argc >= 4) ? std::atol(argv[3]) : 0;
		try { return runServe(argv[2], runMs); }
		catch (const webserv::Exception &e) { LOG_ERROR(e.what()); return 1; }
	}

	LOG_INFO("webserv: config validator smoke test");
	try {
		webserv::config::ConfigAst ast;
		if (argc >= 2) {
			LOG_INFO("parsing file: " << argv[1]);
			ast = webserv::config::parseFile(argv[1]);
		} else {
			LOG_INFO("parsing embedded sample "
			         "(pass a .conf path as argv[1], or one of "
			         "--test-loop / --test-listener / --test-connection)");
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
