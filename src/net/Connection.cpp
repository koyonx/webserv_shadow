#include "webserv/net/Connection.hpp"

#include "webserv/Log.hpp"
#include "webserv/core/PollLoop.hpp"
#include "webserv/handler/StaticHandler.hpp"
#include "webserv/http/Response.hpp"
#include "webserv/net/Router.hpp"

#include <poll.h>
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
	  m_parser()
{}

Connection::~Connection() {}

int Connection::fd() const { return m_fd.get(); }

short Connection::wantEvents() const
{
	switch (m_state) {
		case kReadingRequest:  return POLLIN;
		case kWritingResponse: return POLLOUT;
		case kClosing:         return 0;
	}
	return 0;
}

bool                 Connection::isDone() const { return m_done; }
Connection::State    Connection::state()  const { return m_state; }

void Connection::generateStubResponse()
{
	const webserv::http::Request &req = m_parser.request();
	webserv::http::Response       r;

	if (m_router == NULL) {
		// Legacy stub for --test-connection (no config loaded).
		r.setStatus(200);
		r.setKeepAlive(req.keepAlive);
		r.setBody("webserv connection FSM skeleton\n");
		m_writeBuf = r.serialize();
		m_writePos = 0;
		return;
	}

	RouteMatch m = m_router->match(m_origin, req);
	if (m.errorStatus != 0) {
		generateErrorResponse(m.errorStatus);
		return;
	}

	handler::serveStatic(req, m, r);
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
	char    buf[4096];
	ssize_t r = ::read(m_fd.get(), buf, sizeof(buf));
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
		generateStubResponse();
		m_state = kWritingResponse;
		return;
	}
	loop.setDeadline(this, m_idleMs);
}

void Connection::onWritable(PollLoop &loop)
{
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
		loop.setDeadline(this, m_idleMs);
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
	loop.setDeadline(this, m_idleMs);
}

void Connection::onTimeout(PollLoop &loop)
{
	LOG_INFO("Connection fd=" << m_fd.get() << " idle timeout");
	finish(loop);
}

} // namespace webserv
