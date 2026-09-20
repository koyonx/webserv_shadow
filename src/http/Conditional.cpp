#include "webserv/http/Conditional.hpp"

#include "webserv/StringUtil.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace webserv {
namespace http {

// ---------- date formatting ----------

static const char *kDoW[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *kMonth[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};

std::string httpDateFromTime(std::time_t t)
{
	std::tm *tm = std::gmtime(&t);
	if (tm == NULL) {
		return "Thu, 01 Jan 1970 00:00:00 GMT";
	}
	std::ostringstream oss;
	oss << kDoW[tm->tm_wday] << ", "
	    << std::setfill('0') << std::setw(2) << tm->tm_mday << ' '
	    << kMonth[tm->tm_mon] << ' '
	    << (tm->tm_year + 1900) << ' '
	    << std::setw(2) << tm->tm_hour << ':'
	    << std::setw(2) << tm->tm_min  << ':'
	    << std::setw(2) << tm->tm_sec  << " GMT";
	return oss.str();
}

// Convert Gregorian date + time to a POSIX time_t in UTC without
// relying on timegm() or setenv("TZ", ...). Both are outside the
// External Function whitelist for this project.
static std::time_t makeGmt(int year, int month, int day,
                           int hour, int minute, int second)
{
	static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
	std::time_t days = 0;
	for (int y = 1970; y < year; ++y) {
		bool leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
		days += leap ? 366 : 365;
	}
	for (int mo = 0; mo + 1 < month; ++mo) {
		days += mdays[mo];
		bool leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
		if (mo == 1 && leap) ++days;
	}
	days += day - 1;
	return static_cast<std::time_t>(days) * 86400
	     + static_cast<std::time_t>(hour) * 3600
	     + static_cast<std::time_t>(minute) * 60
	     + static_cast<std::time_t>(second);
}

// Accepts strictly the RFC 7231 IMF-fixdate form:
//   "Xxx, DD Mmm YYYY HH:MM:SS GMT"   (29 characters)
// Browsers always emit this form for If-Modified-Since / If-Range,
// which is the practical universe we care about.
bool parseHttpDate(const std::string &s, std::time_t &out)
{
	if (s.size() < 29) return false;
	if (s[3] != ',' || s[4] != ' ') return false;
	if (s[7] != ' ' || s[11] != ' ' || s[16] != ' ') return false;
	if (s[19] != ':' || s[22] != ':') return false;
	if (s[25] != ' ' || s[26] != 'G' || s[27] != 'M' || s[28] != 'T') return false;

	for (int i = 0; i < 2; ++i) {
		if (!std::isdigit(static_cast<unsigned char>(s[5 + i])))  return false;
		if (!std::isdigit(static_cast<unsigned char>(s[17 + i]))) return false;
		if (!std::isdigit(static_cast<unsigned char>(s[20 + i]))) return false;
		if (!std::isdigit(static_cast<unsigned char>(s[23 + i]))) return false;
	}
	for (int i = 0; i < 4; ++i) {
		if (!std::isdigit(static_cast<unsigned char>(s[12 + i]))) return false;
	}

	int day    = (s[5]  - '0') * 10 + (s[6]  - '0');
	int year   = (s[12] - '0') * 1000 + (s[13] - '0') * 100
	           + (s[14] - '0') * 10   + (s[15] - '0');
	int hour   = (s[17] - '0') * 10 + (s[18] - '0');
	int minute = (s[20] - '0') * 10 + (s[21] - '0');
	int second = (s[23] - '0') * 10 + (s[24] - '0');

	int month = -1;
	for (int i = 0; i < 12; ++i) {
		if (s[8] == kMonth[i][0]
		 && s[9] == kMonth[i][1]
		 && s[10] == kMonth[i][2]) {
			month = i + 1;
			break;
		}
	}
	if (month < 0)                  return false;
	if (day < 1 || day > 31)        return false;
	if (hour > 23 || minute > 59 || second > 59) return false;
	if (year < 1970 || year > 9999) return false;

	out = makeGmt(year, month, day, hour, minute, second);
	return true;
}

// ---------- ETag ----------

std::string buildETag(std::size_t size, std::time_t mtime)
{
	std::ostringstream oss;
	oss << '"' << std::hex
	    << static_cast<unsigned long>(mtime) << '-'
	    << static_cast<unsigned long>(size)
	    << '"';
	return oss.str();
}

// ---------- preconditions ----------

// Return the ETag stripped of any leading "W/" so weak vs strong
// comparison collapses to a single equality check per RFC 7232 §2.3.2.
static std::string stripWeakPrefix(const std::string &s)
{
	if (s.size() > 2 && s[0] == 'W' && s[1] == '/') {
		return s.substr(2);
	}
	return s;
}

PreconditionResult evaluatePreconditions(
	const HeaderMap  &reqHeaders,
	std::time_t       mtime,
	const std::string &etag)
{
	HeaderMap::const_iterator inm = reqHeaders.find("If-None-Match");
	if (inm != reqHeaders.end()) {
		std::string trimmed = strutil::trim(inm->second);
		if (trimmed == "*") {
			return kProceedNotModified;
		}
		std::string ourETag = stripWeakPrefix(etag);
		std::vector<std::string> tokens = strutil::split(inm->second, ',');
		for (std::size_t i = 0; i < tokens.size(); ++i) {
			std::string t = stripWeakPrefix(strutil::trim(tokens[i]));
			if (t == ourETag) {
				return kProceedNotModified;
			}
		}
		// If-None-Match was present but nothing matched. Per RFC 7232
		// §6, we must ignore If-Modified-Since in this case and serve
		// the full response.
		return kProceedFull;
	}

	HeaderMap::const_iterator ims = reqHeaders.find("If-Modified-Since");
	if (ims != reqHeaders.end()) {
		std::time_t clientTime = 0;
		if (parseHttpDate(strutil::trim(ims->second), clientTime)) {
			if (mtime <= clientTime) {
				return kProceedNotModified;
			}
		}
	}
	return kProceedFull;
}

// ---------- Range ----------

// If-Range: opaque either as an ETag ("...\"..." or W/"...") or as a
// date. If it matches the current representation, honor Range; else
// ignore Range and serve the full response.
static bool ifRangeMatches(const std::string &value,
                           std::time_t         mtime,
                           const std::string  &etag)
{
	if (value.empty()) return true;
	std::string v = strutil::trim(value);
	if (v.empty()) return true;
	// ETag form starts with '"' or 'W'
	if (v[0] == '"' || (v.size() > 2 && v[0] == 'W' && v[1] == '/')) {
		return stripWeakPrefix(v) == stripWeakPrefix(etag);
	}
	// Date form
	std::time_t clientTime = 0;
	if (!parseHttpDate(v, clientTime)) return false;
	// If-Range with date: match iff resource mtime == client date
	// (RFC 7233 §3.2 uses "unchanged", not "not modified since").
	return mtime == clientTime;
}

RangeResult parseRange(const HeaderMap  &reqHeaders,
                       std::size_t       fileSize,
                       std::time_t       mtime,
                       const std::string &etag,
                       RangeSpec        &out)
{
	HeaderMap::const_iterator ir = reqHeaders.find("If-Range");
	if (ir != reqHeaders.end() && !ifRangeMatches(ir->second, mtime, etag)) {
		return kRangeIgnored;
	}

	HeaderMap::const_iterator rh = reqHeaders.find("Range");
	if (rh == reqHeaders.end()) return kRangeAbsent;

	std::string v = strutil::trim(rh->second);
	if (v.size() < 7 || v.compare(0, 6, "bytes=") != 0) {
		return kRangeIgnored;
	}
	std::string spec = strutil::trim(v.substr(6));
	if (spec.find(',') != std::string::npos) {
		// Multi-range; we do not encode multipart/byteranges.
		return kRangeIgnored;
	}

	std::string::size_type dash = spec.find('-');
	if (dash == std::string::npos) return kRangeIgnored;

	std::string startS = strutil::trim(spec.substr(0, dash));
	std::string endS   = strutil::trim(spec.substr(dash + 1));

	std::size_t start = 0;
	std::size_t end   = 0;

	if (startS.empty()) {
		// "bytes=-N" — last N bytes.
		if (endS.empty()) return kRangeIgnored;
		long n = 0;
		if (!strutil::parseLong(endS, n) || n <= 0) return kRangeIgnored;
		if (fileSize == 0) return kRangeUnsatisfiable;
		std::size_t nn = static_cast<std::size_t>(n);
		start = (nn >= fileSize) ? 0 : (fileSize - nn);
		end   = fileSize - 1;
	} else if (endS.empty()) {
		// "bytes=X-" — from X to end of file.
		long x = 0;
		if (!strutil::parseLong(startS, x) || x < 0) return kRangeIgnored;
		std::size_t xx = static_cast<std::size_t>(x);
		if (xx >= fileSize) return kRangeUnsatisfiable;
		start = xx;
		end   = fileSize - 1;
	} else {
		// "bytes=X-Y" — X..Y inclusive.
		long x = 0, y = 0;
		if (!strutil::parseLong(startS, x) || x < 0) return kRangeIgnored;
		if (!strutil::parseLong(endS,   y) || y < 0) return kRangeIgnored;
		std::size_t xx = static_cast<std::size_t>(x);
		std::size_t yy = static_cast<std::size_t>(y);
		if (xx >= fileSize) return kRangeUnsatisfiable;
		if (yy >= fileSize) yy = fileSize - 1;
		if (xx > yy)        return kRangeIgnored;
		start = xx;
		end   = yy;
	}

	out.start = start;
	out.end   = end;
	out.total = fileSize;
	return kRangeValid;
}

} // namespace http
} // namespace webserv
