#ifndef WEBSERV_HANDLER_STATIC_HANDLER_HPP
#define WEBSERV_HANDLER_STATIC_HANDLER_HPP

#include "webserv/http/Request.hpp"
#include "webserv/http/Response.hpp"
#include "webserv/net/Router.hpp"

namespace webserv {
namespace handler {

// Serve a GET or HEAD request against the routed match.
// - Enforces allowed_methods (405 with an Allow header if applicable)
// - Resolves the filesystem path from root + normalized path
// - stat() to distinguish file vs directory
// - Directory: tries each `index` file; if none matches and autoindex
//   is off, returns 403 (autoindex is feat/16)
// - File: reads the whole file into the response body and sets
//   Content-Type via the MIME table
// - 404 on missing, 403 on permission failures, 500 on unexpected I/O
//
// The response's keep-alive flag is copied from the request.
void serveStatic(const webserv::http::Request &req,
                 const webserv::RouteMatch    &match,
                 webserv::http::Response      &response);

} // namespace handler
} // namespace webserv

#endif
