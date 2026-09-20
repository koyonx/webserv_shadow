#ifndef WEBSERV_HANDLER_DELETE_HANDLER_HPP
#define WEBSERV_HANDLER_DELETE_HANDLER_HPP

#include "webserv/http/Request.hpp"
#include "webserv/http/Response.hpp"
#include "webserv/net/Router.hpp"

namespace webserv {
namespace handler {

// DELETE handler. Resolves the target file from
// (location.root || server.root) + normalizedPath, then:
//   regular file    -> unlink() -> 204 No Content
//   directory       -> 403 (dir removal not supported here)
//   missing         -> 404
//   perm error      -> 403
//   other syscall   -> 500
//
// The dispatcher has already applied allowed_methods.
void serveDelete(const webserv::http::Request &req,
                 const webserv::RouteMatch    &match,
                 webserv::http::Response      &response);

} // namespace handler
} // namespace webserv

#endif
