#ifndef WEBSERV_HANDLER_METHOD_POLICY_HPP
#define WEBSERV_HANDLER_METHOD_POLICY_HPP

#include "webserv/net/Router.hpp"

#include <string>
#include <vector>

namespace webserv {
namespace handler {

// Returns the effective allowed_methods list for a routed match:
// location.allowedMethods if non-empty, otherwise the built-in
// fallback {GET, POST, DELETE}. `fallback` is written into when the
// fallback is used so the returned reference stays valid.
const std::vector<std::string> &effectiveAllowedMethods(
	const webserv::RouteMatch &match,
	std::vector<std::string>  &fallback);

// Does `method` appear in `allowed`? HEAD counts as GET so the caller
// gets HEAD parity for free.
bool methodIsAllowed(const std::string              &method,
                     const std::vector<std::string> &allowed);

// True only for methods webserv is willing to implement at all: GET,
// HEAD, POST, DELETE. Anything else (PUT, PATCH, TRACE, CONNECT,
// OPTIONS, FROB, ...) is "not implemented" and per RFC 7231 §6.6.2
// deserves 501 rather than 405 — even if the location's allow list
// omits it.
bool methodIsImplemented(const std::string &method);

// Format "GET, POST, DELETE" for the Allow response header.
std::string allowHeader(const std::vector<std::string> &allowed);

} // namespace handler
} // namespace webserv

#endif
