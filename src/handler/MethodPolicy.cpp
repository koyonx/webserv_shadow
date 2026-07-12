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
	// Default fallback when the location omits allowed_methods.
	// HEAD is included so a bare "no allowed_methods set" location
	// still honors HEAD like nginx does; when the operator DOES
	// declare `allowed_methods GET;` explicitly, we treat "GET" as
	// literally "GET only" — no implicit HEAD — so tester spec
	// "/ must answer to GET request ONLY" (`./testers/tester`) is
	// satisfied.
	fallback.clear();
	fallback.push_back("GET");
	fallback.push_back("HEAD");
	fallback.push_back("POST");
	fallback.push_back("DELETE");
	return fallback;
}

bool methodIsAllowed(const std::string              &method,
                     const std::vector<std::string> &allowed)
{
	// Strict membership check. HEAD used to auto-map to GET; we now
	// require the operator to opt in with `allowed_methods GET HEAD;`
	// when a location wants both. This matches the 42 tester's
	// literal reading of "GET request ONLY".
	for (std::size_t i = 0; i < allowed.size(); ++i) {
		if (allowed[i] == method) return true;
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
