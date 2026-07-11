#ifndef WEBSERV_HANDLER_CGI_HANDLER_HPP
#define WEBSERV_HANDLER_CGI_HANDLER_HPP

#include "webserv/http/Request.hpp"
#include "webserv/http/Response.hpp"
#include "webserv/net/Router.hpp"

#include <string>

namespace webserv {
namespace handler {

// Given a routed match, decide whether the request should be handled
// as CGI. Returns true and fills the outputs when the request's path
// extension appears in the location's cgi_pass map.
bool cgiMatch(const webserv::RouteMatch &match,
              const std::string         &method,
              std::string               &interpreterOut,
              std::string               &scriptPathOut,
              std::string               &scriptUriOut,
              std::string               &pathInfoOut,
              std::string               &workDirOut);

// Parse a CGI child's stdout into (status, headers, body) and merge
// into `response`. Handles both "NPH" style (starts with "HTTP/") and
// regular CGI (headers ended by CRLFCRLF).
void applyCgiOutput(const std::string          &raw,
                    webserv::http::Response    &response);

} // namespace handler
} // namespace webserv

#endif
