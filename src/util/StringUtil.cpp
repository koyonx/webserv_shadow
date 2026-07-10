#include "webserv/StringUtil.hpp"

#include <climits>
#include <sstream>

namespace webserv {
namespace strutil {

static bool isSpaceChar(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string ltrim(const std::string &s)
{
	std::size_t i = 0;
	while (i < s.size() && isSpaceChar(s[i])) {
		++i;
	}
	return s.substr(i);
}

std::string rtrim(const std::string &s)
{
	std::size_t i = s.size();
	while (i > 0 && isSpaceChar(s[i - 1])) {
		--i;
	}
	return s.substr(0, i);
}

std::string trim(const std::string &s)
{
	return rtrim(ltrim(s));
}

static char asciiLower(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

static char asciiUpper(char c)
{
	return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
}

std::string toLower(const std::string &s)
{
	std::string out(s.size(), '\0');
	for (std::size_t i = 0; i < s.size(); ++i) {
		out[i] = asciiLower(s[i]);
	}
	return out;
}

std::string toUpper(const std::string &s)
{
	std::string out(s.size(), '\0');
	for (std::size_t i = 0; i < s.size(); ++i) {
		out[i] = asciiUpper(s[i]);
	}
	return out;
}

bool iequals(const std::string &a, const std::string &b)
{
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (asciiLower(a[i]) != asciiLower(b[i])) {
			return false;
		}
	}
	return true;
}

bool startsWith(const std::string &s, const std::string &prefix)
{
	if (prefix.size() > s.size()) {
		return false;
	}
	return s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string &s, const std::string &suffix)
{
	if (suffix.size() > s.size()) {
		return false;
	}
	return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string &s, char delim)
{
	std::vector<std::string> out;
	std::string              cur;
	for (std::size_t i = 0; i < s.size(); ++i) {
		if (s[i] == delim) {
			out.push_back(cur);
			cur.clear();
		} else {
			cur += s[i];
		}
	}
	out.push_back(cur);
	return out;
}

std::string join(const std::vector<std::string> &parts, const std::string &sep)
{
	std::string out;
	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i > 0) {
			out += sep;
		}
		out += parts[i];
	}
	return out;
}

std::string toStr(long v)
{
	std::ostringstream oss;
	oss << v;
	return oss.str();
}

std::string toStr(unsigned long v)
{
	std::ostringstream oss;
	oss << v;
	return oss.str();
}

// Manual overflow-checked parse. Rejects LONG_MIN itself (input "-<LONG_MAX+1>")
// as unrepresentable through negate, which we accept as a documented limit.
bool parseLong(const std::string &s, long &out)
{
	if (s.empty()) {
		return false;
	}
	std::size_t i   = 0;
	bool        neg = false;
	if (s[0] == '+' || s[0] == '-') {
		neg = (s[0] == '-');
		if (++i == s.size()) {
			return false;
		}
	}
	long v = 0;
	for (; i < s.size(); ++i) {
		char c = s[i];
		if (c < '0' || c > '9') {
			return false;
		}
		int d = c - '0';
		if (v > LONG_MAX / 10) {
			return false;
		}
		v *= 10;
		if (v > LONG_MAX - d) {
			return false;
		}
		v += d;
	}
	out = neg ? -v : v;
	return true;
}

// nginx-style size suffixes: "", k/K (KiB), m/M (MiB), g/G (GiB).
bool parseSize(const std::string &s, std::size_t &out)
{
	if (s.empty()) {
		return false;
	}
	std::size_t i = 0;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
		++i;
	}
	if (i == 0) {
		return false;
	}
	std::string num = s.substr(0, i);
	std::string suf = s.substr(i);

	long n = 0;
	if (!parseLong(num, n) || n < 0) {
		return false;
	}

	std::size_t mult = 1;
	if (suf.empty()) {
		mult = 1;
	} else if (suf == "k" || suf == "K") {
		mult = 1024;
	} else if (suf == "m" || suf == "M") {
		mult = static_cast<std::size_t>(1024) * 1024;
	} else if (suf == "g" || suf == "G") {
		mult = static_cast<std::size_t>(1024) * 1024 * 1024;
	} else {
		return false;
	}

	std::size_t un = static_cast<std::size_t>(n);
	if (mult != 0 && un > static_cast<std::size_t>(-1) / mult) {
		return false;
	}
	out = un * mult;
	return true;
}

} // namespace strutil
} // namespace webserv
