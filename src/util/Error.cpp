#include "webserv/Error.hpp"

#include <cstring>
#include <sstream>

namespace webserv {

Exception::Exception(const std::string &msg) : m_msg(msg) {}
Exception::~Exception() throw() {}
const char *Exception::what() const throw() { return m_msg.c_str(); }

static std::string fmtConfig(const std::string &msg,
                             const std::string &file,
                             std::size_t        line)
{
	std::ostringstream oss;
	oss << file << ":" << line << ": " << msg;
	return oss.str();
}

ConfigError::ConfigError(const std::string &msg,
                         const std::string &file,
                         std::size_t        line)
	: Exception(fmtConfig(msg, file, line)), m_file(file), m_line(line) {}
ConfigError::~ConfigError() throw() {}
const std::string &ConfigError::file() const { return m_file; }
std::size_t        ConfigError::line() const { return m_line; }

static std::string fmtSystem(const std::string &msg, int err)
{
	std::ostringstream oss;
	oss << msg << " (" << std::strerror(err) << ")";
	return oss.str();
}

SystemError::SystemError(const std::string &msg, int err)
	: Exception(fmtSystem(msg, err)), m_err(err) {}
SystemError::~SystemError() throw() {}
int SystemError::errnum() const { return m_err; }

ProtocolError::ProtocolError(const std::string &msg, int statusCode)
	: Exception(msg), m_status(statusCode) {}
ProtocolError::~ProtocolError() throw() {}
int ProtocolError::statusCode() const { return m_status; }

} // namespace webserv
