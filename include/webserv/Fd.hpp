#ifndef WEBSERV_FD_HPP
#define WEBSERV_FD_HPP

namespace webserv {

// RAII wrapper for a POSIX file descriptor.
// - Owns exactly one fd (or -1 == "empty").
// - Non-copyable: transfer ownership via swap() or release().
// - close() and dtor tolerate the empty state.
class Fd {
public:
	Fd();
	explicit Fd(int fd);
	~Fd();

	int  get() const;
	bool valid() const;

	int  release();
	void reset(int fd = -1);
	void close();
	void swap(Fd &other);

private:
	Fd(const Fd &);
	Fd &operator=(const Fd &);

	int m_fd;
};

// Puts the fd into non-blocking mode via the single fcntl form allowed
// by the webserv subject: fcntl(fd, F_SETFL, O_NONBLOCK). Throws
// SystemError on failure.
void setNonBlocking(int fd);

} // namespace webserv

#endif
