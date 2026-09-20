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
// regular CGI (headers ended by CRLFCRLF). Buffered path — kept for
// tests / non-streaming callers.
void applyCgiOutput(const std::string          &raw,
                    webserv::http::Response    &response);

// Streaming primitive used by the S5 CGI streaming path: given ONLY
// the header block (bytes before the CRLFCRLF terminator; may include
// a leading "HTTP/... status\r\n" NPH prefix), apply Status:, Location:
// and user headers onto `response`. The body is NOT touched — the
// streaming caller feeds body bytes through Response::chunkFrame()
// after Response::serializeHeaders() has been sent.
//
// Returns:
//   0  on success — response now carries the parsed status/reason and
//                   user headers.
//   502 if the header block is empty or contains no name: value pair
//       (the L1 malformed-CGI case). `response` is left untouched;
//       the caller emits its own 502.
int parseCgiHeaders(const std::string        &headerBlock,
                    webserv::http::Response  &response);

} // namespace handler
} // namespace webserv

#endif
