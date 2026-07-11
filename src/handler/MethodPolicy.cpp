#include "webserv/handler/MethodPolicy.hpp"

namespace webserv {
namespace handler {

const std::vector<std::string> &effectiveAllowedMethods(
	const webserv::RouteMatch &match,
	std::vector<std::string>  &fallback)
{
	if (match.location != NULL && !match.location->allowedMethods.empty()) {
		return match.location->allowedMethods;
	}
	fallback.clear();
	fallback.push_back("GET");
	fallback.push_back("POST");
	fallback.push_back("DELETE");
	return fallback;
}

bool methodIsAllowed(const std::string              &method,
                     const std::vector<std::string> &allowed)
{
	for (std::size_t i = 0; i < allowed.size(); ++i) {
		if (allowed[i] == method)                    return true;
		if (method == "HEAD" && allowed[i] == "GET") return true;
	}
	return false;
}

bool methodIsImplemented(const std::string &method)
{
	return method == "GET"    || method == "HEAD" ||
	       method == "POST"   || method == "DELETE";
}

std::string allowHeader(const std::vector<std::string> &allowed)
{
	std::string out;
	for (std::size_t i = 0; i < allowed.size(); ++i) {
		if (i > 0) out += ", ";
		out += allowed[i];
	}
	return out;
}

} // namespace handler
} // namespace webserv
