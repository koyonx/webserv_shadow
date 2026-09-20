#include "webserv/Log.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>

namespace webserv {

LogLevel      Log::s_level = kLogInfo;
std::ostream *Log::s_out   = &std::cerr;

void Log::setLevel(LogLevel lvl)
{
	s_level = lvl;
}

LogLevel Log::level()
{
	return s_level;
}

void Log::setStream(std::ostream *out)
{
	s_out = (out != NULL) ? out : &std::cerr;
}

const char *Log::levelStr(LogLevel lvl)
{
	switch (lvl) {
		case kLogDebug: return "DEBUG";
		case kLogInfo:  return "INFO ";
		case kLogWarn:  return "WARN ";
		case kLogError: return "ERROR";
		case kLogFatal: return "FATAL";
	}
	return "?????";
}

std::string Log::timestamp()
{
	std::time_t  now = std::time(NULL);
	std::tm     *tm  = std::localtime(&now);
	std::ostringstream oss;
	oss << std::setfill('0')
	    << std::setw(4) << (tm->tm_year + 1900) << "-"
	    << std::setw(2) << (tm->tm_mon + 1)     << "-"
	    << std::setw(2) <<  tm->tm_mday          << " "
	    << std::setw(2) <<  tm->tm_hour          << ":"
	    << std::setw(2) <<  tm->tm_min           << ":"
	    << std::setw(2) <<  tm->tm_sec;
	return oss.str();
}

void Log::write(LogLevel lvl, const std::string &msg)
{
	if (s_out == NULL) {
		return;
	}
	(*s_out) << "[" << timestamp() << "][" << levelStr(lvl) << "] " << msg << "\n";
	s_out->flush();
}

} // namespace webserv
