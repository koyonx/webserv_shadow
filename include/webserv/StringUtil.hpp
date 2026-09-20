#ifndef WEBSERV_STRING_UTIL_HPP
#define WEBSERV_STRING_UTIL_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace webserv {
namespace strutil {

std::string ltrim(const std::string &s);
std::string rtrim(const std::string &s);
std::string trim(const std::string &s);

std::string toLower(const std::string &s);
std::string toUpper(const std::string &s);

bool iequals(const std::string &a, const std::string &b);
bool startsWith(const std::string &s, const std::string &prefix);
bool endsWith(const std::string &s, const std::string &suffix);

std::vector<std::string> split(const std::string &s, char delim);
std::string              join(const std::vector<std::string> &parts,
                              const std::string &sep);

std::string toStr(long v);
std::string toStr(unsigned long v);

bool parseLong(const std::string &s, long &out);
bool parseSize(const std::string &s, std::size_t &out);

} // namespace strutil
} // namespace webserv

#endif
