#include "webserv/Fd.hpp"

#include "webserv/Error.hpp"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace webserv {

Fd::Fd()          : m_fd(-1) {}
Fd::Fd(int fd)    : m_fd(fd) {}
Fd::~Fd()         { close(); }

int  Fd::get() const   { return m_fd; }
bool Fd::valid() const { return m_fd >= 0; }

int Fd::release()
{
	int r = m_fd;
	m_fd  = -1;
	return r;
}

void Fd::reset(int fd)
{
	if (m_fd >= 0 && m_fd != fd) {
		::close(m_fd);
	}
	m_fd = fd;
}

void Fd::close()
{
	if (m_fd >= 0) {
		::close(m_fd);
		m_fd = -1;
	}
}

void Fd::swap(Fd &other)
{
	int tmp    = m_fd;
	m_fd       = other.m_fd;
	other.m_fd = tmp;
}

void setNonBlocking(int fd)
{
	if (::fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		throw SystemError("fcntl(F_SETFL, O_NONBLOCK) failed", errno);
	}
}

} // namespace webserv
