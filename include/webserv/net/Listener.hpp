#ifndef WEBSERV_NET_LISTENER_HPP
#define WEBSERV_NET_LISTENER_HPP

#include "webserv/Fd.hpp"
#include "webserv/config/Config.hpp"
#include "webserv/core/IHandler.hpp"

namespace webserv {

// Callback for each accepted client connection.
class IAcceptSink {
public:
	virtual ~IAcceptSink() {}
	// Ownership of clientFd is transferred to the sink. The sink is
	// responsible for closing it (usually by wrapping it in an Fd and
	// registering a Connection handler).
	virtual void onAccept(int                         clientFd,
	                      const webserv::config::Listen &origin,
	                      PollLoop                   &loop) = 0;
};

// A single AF_INET listening socket. Registered with PollLoop; when
// poll signals readability, accepts up to a small burst of connections
// per tick and hands them to the IAcceptSink.
class Listener : public IHandler {
public:
	Listener(const webserv::config::Listen &cfg, IAcceptSink &sink);
	virtual ~Listener();

	// Creates the socket, sets SO_REUSEADDR, binds, listens, and marks
	// it non-blocking. Throws SystemError on any syscall failure.
	void bindAndListen(int backlog = 128);

	const webserv::config::Listen &config() const;

	// IHandler
	virtual int   fd()          const;
	virtual short wantEvents()  const;
	virtual void  onReadable(PollLoop &loop);
	virtual void  onWritable(PollLoop &loop);
	virtual void  onTimeout(PollLoop &loop);

private:
	Listener(const Listener &);
	Listener &operator=(const Listener &);

	Fd                              m_fd;
	webserv::config::Listen         m_cfg;
	IAcceptSink                    &m_sink;
};

} // namespace webserv

#endif
