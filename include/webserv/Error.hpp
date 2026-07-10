#ifndef WEBSERV_ERROR_HPP
#define WEBSERV_ERROR_HPP

#include <cstddef>
#include <exception>
#include <string>

namespace webserv {

class Exception : public std::exception {
public:
	explicit Exception(const std::string &msg);
	virtual ~Exception() throw();
	virtual const char *what() const throw();

protected:
	std::string m_msg;
};

class ConfigError : public Exception {
public:
	ConfigError(const std::string &msg,
	            const std::string &file,
	            std::size_t        line);
	virtual ~ConfigError() throw();

	const std::string &file() const;
	std::size_t        line() const;

private:
	std::string m_file;
	std::size_t m_line;
};

class SystemError : public Exception {
public:
	SystemError(const std::string &msg, int err);
	virtual ~SystemError() throw();

	int errnum() const;

private:
	int m_err;
};

class ProtocolError : public Exception {
public:
	ProtocolError(const std::string &msg, int statusCode);
	virtual ~ProtocolError() throw();

	int statusCode() const;

private:
	int m_status;
};

} // namespace webserv

#endif
