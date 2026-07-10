#ifndef WEBSERV_LOG_HPP
#define WEBSERV_LOG_HPP

#include <ostream>
#include <sstream>
#include <string>

namespace webserv {

enum LogLevel {
	kLogDebug = 0,
	kLogInfo  = 1,
	kLogWarn  = 2,
	kLogError = 3,
	kLogFatal = 4
};

class Log {
public:
	static void     setLevel(LogLevel lvl);
	static LogLevel level();

	static void     setStream(std::ostream *out);

	static void     write(LogLevel lvl, const std::string &msg);

private:
	Log();
	Log(const Log &);
	Log &operator=(const Log &);

	static LogLevel      s_level;
	static std::ostream *s_out;

	static const char   *levelStr(LogLevel lvl);
	static std::string   timestamp();
};

} // namespace webserv

#define WS_LOG(lvl, expr)                                             \
	do {                                                              \
		if (::webserv::Log::level() <= (lvl)) {                       \
			std::ostringstream _ws_oss;                               \
			_ws_oss << expr;                                          \
			::webserv::Log::write((lvl), _ws_oss.str());              \
		}                                                             \
	} while (0)

#define LOG_DEBUG(expr) WS_LOG(::webserv::kLogDebug, expr)
#define LOG_INFO(expr)  WS_LOG(::webserv::kLogInfo,  expr)
#define LOG_WARN(expr)  WS_LOG(::webserv::kLogWarn,  expr)
#define LOG_ERROR(expr) WS_LOG(::webserv::kLogError, expr)
#define LOG_FATAL(expr) WS_LOG(::webserv::kLogFatal, expr)

#endif
