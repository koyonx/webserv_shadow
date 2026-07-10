#include "webserv/net/Listener.hpp"

#include "webserv/Error.hpp"
#include "webserv/Log.hpp"
#include "webserv/StringUtil.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace webserv {

Listener::Listener(const webserv::config::Listen &cfg, IAcceptSink &sink)
	: m_fd(), m_cfg(cfg), m_sink(sink) {}

Listener::~Listener() {}

const webserv::config::Listen &Listener::config() const { return m_cfg; }

static void resolveIPv4(const std::string &host, struct in_addr &out)
{
	if (host.empty() || host == "0.0.0.0" || host == "*") {
		out.s_addr = htonl(INADDR_ANY);
		return;
	}

	// Try dotted-quad first via inet_pton. If that fails, fall through
	// to getaddrinfo for hostnames (e.g. "localhost").
	if (::inet_pton(AF_INET, host.c_str(), &out) == 1) {
		return;
	}

	struct addrinfo  hints;
	struct addrinfo *res = NULL;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	int gai = ::getaddrinfo(host.c_str(), NULL, &hints, &res);
	if (gai != 0) {
		throw SystemError(std::string("getaddrinfo('") + host + "'): "
		                  + ::gai_strerror(gai), 0);
	}
	if (res == NULL) {
		throw SystemError(std::string("getaddrinfo('") + host
		                  + "'): no address returned", 0);
	}
	struct sockaddr_in *sin = reinterpret_cast<struct sockaddr_in *>(res->ai_addr);
	out = sin->sin_addr;
	::freeaddrinfo(res);
}

void Listener::bindAndListen(int backlog)
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		throw SystemError("socket(AF_INET, SOCK_STREAM)", errno);
	}
	m_fd.reset(fd);

	int one = 1;
	if (::setsockopt(m_fd.get(), SOL_SOCKET, SO_REUSEADDR,
	                 &one, sizeof(one)) < 0) {
		throw SystemError("setsockopt(SO_REUSEADDR)", errno);
	}

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port   = htons(static_cast<uint16_t>(m_cfg.port));
	resolveIPv4(m_cfg.host, addr.sin_addr);

	if (::bind(m_fd.get(),
	           reinterpret_cast<struct sockaddr *>(&addr),
	           sizeof(addr)) < 0) {
		int err = errno;
		std::string msg = "bind(" + m_cfg.host + ":"
		                + strutil::toStr(static_cast<long>(m_cfg.port)) + ")";
		throw SystemError(msg, err);
	}

	if (::listen(m_fd.get(), backlog) < 0) {
		throw SystemError("listen()", errno);
	}

	setNonBlocking(m_fd.get());

	LOG_INFO("Listener bound: " << m_cfg.host << ":" << m_cfg.port
	         << " (fd=" << m_fd.get() << ", backlog=" << backlog << ")");
}

int   Listener::fd()         const { return m_fd.get(); }
short Listener::wantEvents() const { return POLLIN; }

void Listener::onReadable(PollLoop &loop)
{
	// Accept a small burst per tick to avoid starving other fds. If
	// the queue holds more, we come back next iteration.
	for (int i = 0; i < 32; ++i) {
		struct sockaddr_storage sa;
		socklen_t               salen = sizeof(sa);
		int cfd = ::accept(m_fd.get(),
		                   reinterpret_cast<struct sockaddr *>(&sa), &salen);
		if (cfd < 0) {
			// Non-blocking accept: EAGAIN/EWOULDBLOCK means "no more
			// pending". We must not check errno explicitly per subject,
			// so we treat every failure the same: stop for this tick.
			break;
		}
		try {
			setNonBlocking(cfd);
		} catch (const Exception &e) {
			LOG_WARN("Listener: failed to set O_NONBLOCK on cfd=" << cfd
			         << " (" << e.what() << "), closing");
			::close(cfd);
			continue;
		}
		m_sink.onAccept(cfd, m_cfg, loop);
	}
}

void Listener::onWritable(PollLoop & /*loop*/) {}
void Listener::onTimeout(PollLoop  & /*loop*/) {}

} // namespace webserv
