#ifndef WEBSERV_HANDLER_DISPATCH_HPP
#define WEBSERV_HANDLER_DISPATCH_HPP

#include "webserv/http/Request.hpp"
#include "webserv/http/Response.hpp"
#include "webserv/net/Router.hpp"

namespace webserv {
namespace handler {

// Central entry point after routing. Applies the method policy
// (allowed_methods), honors location `return CODE [URL];`, and then
// dispatches to the correct method handler:
//   GET  / HEAD -> serveStatic
//   DELETE      -> serveDelete
//   POST        -> servePost   (feat/19 stub)
//   others      -> 501 Not Implemented
//
// This is the ONE place that decides "who owns the request." Handlers
// downstream assume their method is what they were called for.
void dispatch(const webserv::http::Request &req,
              const webserv::RouteMatch    &match,
              webserv::http::Response      &response);

} // namespace handler
} // namespace webserv

#endif
