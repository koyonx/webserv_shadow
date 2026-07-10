#include "webserv/net/Connection.hpp"

#include "webserv/Log.hpp"
#include "webserv/core/PollLoop.hpp"

#include <poll.h>
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
	  m_done(false)
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
	// Skeleton HTTP/1.1 response so we can end-to-end verify the plumb.
	// feat/10+ will replace this with a proper builder.
	const std::string body =
		"webserv connection FSM skeleton\n";

	std::string cl = "Content-Length: ";
	{
		std::size_t n = body.size();
		std::string digits;
		if (n == 0) {
			digits = "0";
		} else {
			while (n > 0) {
				digits.insert(digits.begin(),
				              static_cast<char>('0' + (n % 10)));
				n /= 10;
			}
		}
		cl += digits;
	}

	m_writeBuf =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/plain\r\n"
	+ cl + "\r\n"
		"Connection: close\r\n"
		"\r\n"
	+ body;
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
	char           buf[4096];
	ssize_t        r = ::read(m_fd.get(), buf, sizeof(buf));
	if (r <= 0) {
		// EOF or transient error. Per subject we do not check errno
		// after read/write, so we treat every non-positive return
		// the same: connection is done.
		finish(loop);
		return;
	}
	m_readBuf.append(buf, static_cast<std::size_t>(r));

	if (m_state == kReadingRequest) {
		if (m_readBuf.find("\r\n\r\n") != std::string::npos
		 || m_readBuf.find("\n\n")     != std::string::npos) {
			generateStubResponse();
			m_state = kWritingResponse;
		}
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
		// Closed or transient error; drop the connection.
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
