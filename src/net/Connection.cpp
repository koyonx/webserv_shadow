#include "webserv/net/Connection.hpp"

#include "webserv/Log.hpp"
#include "webserv/StringUtil.hpp"
#include "webserv/core/PollLoop.hpp"

#include <poll.h>
#include <sstream>
#include <unistd.h>

namespace webserv {

Connection::Connection(int                            cfd,
                       const webserv::config::Listen &origin,
                       IConnectionOwner              &owner,
                       long                           idleTimeoutMs)
	: m_fd(cfd),
	  m_origin(origin),
	  m_owner(owner),
	  m_idleMs(idleTimeoutMs),
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

static const char *reasonPhrase(int status)
{
	switch (status) {
		case 200: return "OK";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 400: return "Bad Request";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 408: return "Request Timeout";
		case 411: return "Length Required";
		case 413: return "Payload Too Large";
		case 414: return "URI Too Long";
		case 415: return "Unsupported Media Type";
		case 431: return "Request Header Fields Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 503: return "Service Unavailable";
		case 504: return "Gateway Timeout";
		case 505: return "HTTP Version Not Supported";
	}
	return "Error";
}

void Connection::generateStubResponse()
{
	const std::string body = "webserv connection FSM skeleton\n";
	std::ostringstream oss;
	oss << "HTTP/1.1 200 OK\r\n"
	    << "Content-Type: text/plain\r\n"
	    << "Content-Length: " << body.size() << "\r\n"
	    << "Connection: close\r\n"
	    << "\r\n"
	    << body;
	m_writeBuf = oss.str();
	m_writePos = 0;
}

void Connection::generateErrorResponse(int status)
{
	std::ostringstream bodyOss;
	bodyOss << status << " " << reasonPhrase(status) << "\n";
	std::string body = bodyOss.str();

	std::ostringstream oss;
	oss << "HTTP/1.1 " << status << " " << reasonPhrase(status) << "\r\n"
	    << "Content-Type: text/plain; charset=utf-8\r\n"
	    << "Content-Length: " << body.size() << "\r\n"
	    << "Connection: close\r\n"
	    << "\r\n"
	    << body;
	m_writeBuf = oss.str();
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

void Connection::onReadable(PollLoop &loop)
{
	char    buf[4096];
	ssize_t r = ::read(m_fd.get(), buf, sizeof(buf));
	if (r <= 0) {
		finish(loop);
		return;
	}
	m_readBuf.append(buf, static_cast<std::size_t>(r));

	// Drain the read buffer into the parser as far as it will go.
	while (m_state == kReadingRequest && !m_readBuf.empty()) {
		std::size_t             consumed = 0;
		webserv::http::ParseResult res =
			m_parser.feed(m_readBuf.data(), m_readBuf.size(), consumed);
		if (consumed > 0) {
			m_readBuf.erase(0, consumed);
		}
		if (res == webserv::http::kParseError) {
			int status = m_parser.errorStatus();
			LOG_WARN("Connection fd=" << m_fd.get()
			         << " parse error " << status << ": "
			         << (m_parser.errorMessage() ? m_parser.errorMessage() : ""));
			generateErrorResponse(status);
			m_state = kWritingResponse;
			break;
		}
		if (res == webserv::http::kParseNeedMore) {
			break;
		}
		// kParseComplete: full request (line + headers + body) consumed.
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
		break;
	}
	loop.setDeadline(this, m_idleMs);
}

void Connection::onWritable(PollLoop &loop)
{
	if (m_state != kWritingResponse) {
		return;
	}
	std::size_t remaining = m_writeBuf.size() - m_writePos;
	if (remaining == 0) {
		finish(loop);
		return;
	}
	ssize_t w = ::write(m_fd.get(),
	                    m_writeBuf.data() + m_writePos,
	                    remaining);
	if (w <= 0) {
		finish(loop);
		return;
	}
	m_writePos += static_cast<std::size_t>(w);
	if (m_writePos >= m_writeBuf.size()) {
		finish(loop);
		return;
	}
	loop.setDeadline(this, m_idleMs);
}

void Connection::onTimeout(PollLoop &loop)
{
	LOG_INFO("Connection fd=" << m_fd.get() << " idle timeout");
	finish(loop);
}

} // namespace webserv
