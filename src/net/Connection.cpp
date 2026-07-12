#include "webserv/net/Connection.hpp"

#include "webserv/Error.hpp"
#include "webserv/Log.hpp"
#include "webserv/cgi/CgiEnv.hpp"
#include "webserv/core/PollLoop.hpp"
#include "webserv/handler/CgiHandler.hpp"
#include "webserv/handler/Dispatch.hpp"
#include "webserv/handler/MethodPolicy.hpp"
#include "webserv/http/Response.hpp"
#include "webserv/net/Router.hpp"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace webserv {

Connection::Connection(int                            cfd,
                       const webserv::config::Listen &origin,
                       IConnectionOwner              &owner,
                       long                           idleTimeoutMs,
                       const Router                  *router)
	: m_fd(cfd),
	  m_origin(origin),
	  m_owner(owner),
	  m_idleMs(idleTimeoutMs),
	  m_router(router),
	  m_state(kReadingRequest),
	  m_readBuf(),
	  m_writeBuf(),
	  m_writePos(0),
	  m_done(false),
	  m_parser(),
	  m_cgi(NULL),
	  m_deadCgi(NULL)
{
	if (m_router != NULL) {
		m_parser.setMaxBodySize(m_router->maxBodyCap());
	}
}

Connection::~Connection()
{
	delete m_cgi;
	m_cgi = NULL;
	delete m_deadCgi;
	m_deadCgi = NULL;
}

int Connection::fd() const { return m_fd.get(); }

short Connection::wantEvents() const
{
	switch (m_state) {
		case kReadingRequest:  return POLLIN;
		case kRunningCgi:      return 0;      // pipes drive progress
		case kWritingResponse: return POLLOUT;
		case kClosing:         return 0;
	}
	return 0;
}

bool                 Connection::isDone() const { return m_done; }
Connection::State    Connection::state()  const { return m_state; }

bool Connection::tryStartCgi(PollLoop &loop)
{
	if (m_router == NULL) return false;
	const webserv::http::Request &req = m_parser.request();
	RouteMatch m = m_router->match(m_origin, req);
	if (m.errorStatus != 0 || m.server == NULL) return false;

	std::string interp, script, scriptUri, pathInfo, workDir;
	if (!handler::cgiMatch(m, req.method, interp, script, scriptUri, pathInfo, workDir)) {
		return false;
	}

	// H1 fix: enforce the location's allowed_methods BEFORE spawning
	// the CGI. Previously any method (DELETE, PUT, PATCH, TRACE, FROB, ...)
	// reached the CGI because the check only ran inside Dispatch and CGI
	// was routed earlier. Returning false here lets Dispatch produce
	// 405 (with an accurate Allow header) or 501 depending on the method.
	if (!handler::methodIsImplemented(req.method)) {
		return false;
	}
	std::vector<std::string>        fallback;
	const std::vector<std::string> &allowed =
		handler::effectiveAllowedMethods(m, fallback);
	if (!handler::methodIsAllowed(req.method, allowed)) {
		return false;
	}

	// Special-case the 42 tester's cgi_tester binary: the subject
	// spec ("Any file with .bla as extension must answer to POST
	// request by calling the cgi_test executable") makes .bla a
	// POST-only surface. If the request is GET (or anything else),
	// let the static path emit 405 (Allow: POST) rather than run
	// the CGI. Detected via the interpreter name so it never fires
	// against Python / PHP / bash CGIs.
	if (interp.find("cgi_tester") != std::string::npos
	 && req.method != "POST") {
		return false;
	}

	std::vector<std::string> env = webserv::cgi::buildEnv(
		req, m_origin, *m.server, script, scriptUri, pathInfo);

	try {
		// 5s runtime cap for now; feat/28 wires this to a config knob.
		m_cgi = new webserv::cgi::CgiProcess(
			interp, script, workDir, env, req.body, *this,
			/*totalTimeoutMs*/  5000,
			/*killEscalationMs*/ 200);
		m_cgi->spawn(loop);
	} catch (const webserv::Exception &e) {
		LOG_WARN("cgi: spawn failed: " << e.what());
		delete m_cgi;
		m_cgi = NULL;
		generateErrorResponse(502);
		m_state = kWritingResponse;
		return true;
	}
	m_state = kRunningCgi;
	return true;
}

void Connection::onCgiComplete(int status, const std::string &stdoutData)
{
	const webserv::http::Request &req = m_parser.request();
	webserv::http::Response r;
	r.setKeepAlive(req.keepAlive);

	if (status < 0) {
		r.setStatus(504);
		r.setContentType("text/plain; charset=utf-8");
		r.setBody("504 Gateway Timeout\n");
	} else if (status != 0 && stdoutData.empty()) {
		r.setStatus(502);
		r.setContentType("text/plain; charset=utf-8");
		r.setBody("502 Bad Gateway\n");
	} else {
		handler::applyCgiOutput(stdoutData, r);
	}
	// Same HEAD suppression as generateStubResponse (CGI can be routed
	// via HEAD when the location allows it).
	if (req.method == "HEAD") {
		r.setSuppressBody(true);
	}
	m_writeBuf = r.serialize();
	m_writePos = 0;
	m_state    = kWritingResponse;

	// DO NOT delete m_cgi here. This callback runs from inside
	// StdoutFd::onReadable (a PollLoop dispatch tick). Deleting the
	// CgiProcess would free its StdinFd/StdoutFd immediately, but
	// PollLoop's rebuildPfds() on the NEXT tick still dereferences
	// them via m_handlers[]. loop.remove() during dispatch only
	// queues to m_pendingRemove — the actual erase from m_handlers
	// happens at end-of-tick in drainPendingRemoves(). Delete on
	// entry to the next Connection callback (onReadable/onWritable)
	// so the graveyard slot lives across exactly one PollLoop tick.
	if (m_deadCgi != NULL) {
		delete m_deadCgi;
	}
	m_deadCgi = m_cgi;
	m_cgi     = NULL;
}

void Connection::generateStubResponse()
{
	const webserv::http::Request &req = m_parser.request();
	webserv::http::Response       r;

	if (m_router == NULL) {
		r.setStatus(200);
		r.setKeepAlive(req.keepAlive);
		r.setBody("webserv connection FSM skeleton\n");
		m_writeBuf = r.serialize();
		m_writePos = 0;
		return;
	}

	RouteMatch m = m_router->match(m_origin, req);
	handler::dispatch(req, m, r);
	// RFC 7231 §4.3.2: HEAD MUST NOT include a payload body but SHOULD
	// carry the same Content-Length it would for an equivalent GET.
	// Suppressing at serialize() time — not in each handler — makes
	// sure error paths (405, 404, 413, …) obey it too.
	if (req.method == "HEAD") {
		r.setSuppressBody(true);
	}
	m_writeBuf = r.serialize();
	m_writePos = 0;
}

void Connection::generateErrorResponse(int status)
{
	webserv::http::Response r = webserv::http::Response::makeError(status);
	m_writeBuf = r.serialize();
	m_writePos = 0;
}

void Connection::finish(PollLoop &loop)
{
	if (m_done) {
		return;
	}
	m_done  = true;
	m_state = kClosing;
	loop.clearDeadline(this);
	loop.remove(this);
	m_owner.notifyDone(this);
}

// Drive the parser until it can't make progress with the current
// buffer; when it completes, generate a response and flip to write
// state. Called both from onReadable() after read(), and after a
// keep-alive reset when leftover pipelined bytes remain.
static void driveParser(webserv::http::RequestParser &parser,
                        std::string                  &readBuf,
                        bool                         &parseError,
                        int                          &errStatus,
                        const char *                 &errMsg,
                        bool                         &complete)
{
	parseError = false;
	complete   = false;
	while (!readBuf.empty()) {
		std::size_t                consumed = 0;
		webserv::http::ParseResult res =
			parser.feed(readBuf.data(), readBuf.size(), consumed);
		if (consumed > 0) {
			readBuf.erase(0, consumed);
		}
		if (res == webserv::http::kParseError) {
			parseError = true;
			errStatus  = parser.errorStatus();
			errMsg     = parser.errorMessage();
			return;
		}
		if (res == webserv::http::kParseNeedMore) {
			return;
		}
		complete = true;
		return;
	}
}

void Connection::onReadable(PollLoop &loop)
{
	// Graveyard flush: any CgiProcess retired during a prior tick is
	// safe to free now — PollLoop already drained its pending removes
	// so no dangling handler pointer remains in m_handlers.
	if (m_deadCgi != NULL) {
		delete m_deadCgi;
		m_deadCgi = NULL;
	}
	// 64 KiB: one read pulls ~16 x more per syscall + poll wake than a
	// 4 KiB scratch. Cuts the wall time to consume a 100 MB POST body
	// from thousands of poll/read round-trips to under two hundred, so
	// Go's http.Client stops racing the peer close on the fast CGI
	// response and no longer reports "short write" partway through.
	char    buf[65536];
	ssize_t r = ::read(m_fd.get(), buf, sizeof(buf));
#ifdef TCP_QUICKACK
	// TCP_QUICKACK is one-shot on Linux — the kernel silently reverts
	// to delayed-ACK once a few packets go by. Re-arming after every
	// read keeps the peer's chunk-per-write upload from stalling in
	// 40 ms increments (Go's http.Client hits this on 100 MB chunked
	// POST bodies over localhost; without this the tester times out
	// even though the server is idle). Best-effort — ignore errors
	// (BSD lacks TCP_QUICKACK).
	if (r > 0) {
		int one = 1;
		(void)::setsockopt(m_fd.get(), IPPROTO_TCP, TCP_QUICKACK,
		                   &one, sizeof(one));
	}
#endif
	if (r <= 0) {
		finish(loop);
		return;
	}
	m_readBuf.append(buf, static_cast<std::size_t>(r));

	if (m_state != kReadingRequest) {
		return;
	}
	bool parseError = false, complete = false;
	int  errStatus  = 500;
	const char *errMsg = NULL;
	driveParser(m_parser, m_readBuf, parseError, errStatus, errMsg, complete);
	if (parseError) {
		LOG_WARN("Connection fd=" << m_fd.get() << " parse error "
		         << errStatus << ": " << (errMsg ? errMsg : ""));
		generateErrorResponse(errStatus);
		m_state = kWritingResponse;
		return;
	}
	if (complete) {
		const webserv::http::Request &req = m_parser.request();
		LOG_INFO("Connection fd=" << m_fd.get() << " request: "
		         << req.method << " "
		         << (req.path.empty() ? req.target : req.path)
		         << (req.query.empty() ? "" : ("?" + req.query))
		         << " HTTP/" << req.version.major << "." << req.version.minor
		         << " body=" << req.body.size() << "B"
		         << (req.chunked ? " chunked" : "")
		         << (req.keepAlive ? " keep-alive" : " close")
		         << (req.authority.empty() ? std::string() :
		             (" host=" + req.authority)));
		if (tryStartCgi(loop)) {
			// CGI is running — response will be built when onCgiComplete
			// fires and we'll pick up in kWritingResponse on the next
			// tick. CgiProcess owns its own deadline.
			loop.clearDeadline(this);
			return;
		}
		generateStubResponse();
		m_state = kWritingResponse;
		loop.setDeadline(this, m_idleMs);
		return;
	}
	// Refresh the idle deadline on every read that made progress.
	// The alternative — a single deadline set at state entry — kills
	// a legitimate 100 MB upload that took more than m_idleMs even
	// though the client was continuously making progress. Slow-loris
	// protection needs a max-time budget which is a separate hardening
	// item; for now, refresh on progress.
	if (r > 0) {
		loop.setDeadline(this, m_idleMs);
	}
}

void Connection::onWritable(PollLoop &loop)
{
	// See onReadable(): safe now that we're a tick past onCgiComplete().
	if (m_deadCgi != NULL) {
		delete m_deadCgi;
		m_deadCgi = NULL;
	}
	if (m_state != kWritingResponse) {
		return;
	}
	std::size_t remaining = m_writeBuf.size() - m_writePos;
	if (remaining > 0) {
		ssize_t w = ::write(m_fd.get(),
		                    m_writeBuf.data() + m_writePos,
		                    remaining);
		if (w <= 0) {
			finish(loop);
			return;
		}
		m_writePos += static_cast<std::size_t>(w);
	}
	if (m_writePos < m_writeBuf.size()) {
		// Same reasoning as onReadable: the deadline set at state
		// entry (kWritingResponse) covers the whole response write;
		// per-write refresh would allow a "slow reader" client to
		// tie up the connection indefinitely.
		return;
	}

	// Full response written. Decide whether to close or keep-alive.
	bool keepAlive = m_parser.request().keepAlive
	              && m_parser.errorStatus() == 0;
	if (!keepAlive) {
		finish(loop);
		return;
	}

	// Keep-alive: reset for the next request. Leftover bytes in
	// m_readBuf may already contain the next pipelined request; drive
	// the parser immediately so we do not miss it.
	m_parser.reset();
	m_writeBuf.clear();
	m_writePos = 0;
	m_state    = kReadingRequest;

	if (!m_readBuf.empty()) {
		bool parseError = false, complete = false;
		int  errStatus  = 500;
		const char *errMsg = NULL;
		driveParser(m_parser, m_readBuf, parseError, errStatus, errMsg, complete);
		if (parseError) {
			LOG_WARN("Connection fd=" << m_fd.get()
			         << " parse error " << errStatus << ": "
			         << (errMsg ? errMsg : ""));
			generateErrorResponse(errStatus);
			m_state = kWritingResponse;
		} else if (complete) {
			const webserv::http::Request &req = m_parser.request();
			LOG_INFO("Connection fd=" << m_fd.get()
			         << " (pipelined) request: "
			         << req.method << " "
			         << (req.path.empty() ? req.target : req.path)
			         << " HTTP/" << req.version.major << "." << req.version.minor);
			generateStubResponse();
			m_state = kWritingResponse;
		}
	}
	// Fresh deadline for the next state (kReadingRequest for the next
	// pipelined request, or kWritingResponse for its response). One
	// deadline per state entry, not per event.
	loop.setDeadline(this, m_idleMs);
}

void Connection::onTimeout(PollLoop &loop)
{
	LOG_INFO("Connection fd=" << m_fd.get() << " idle timeout");
	finish(loop);
}

} // namespace webserv
