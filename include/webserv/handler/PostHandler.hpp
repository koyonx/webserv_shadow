#ifndef WEBSERV_HANDLER_POST_HANDLER_HPP
#define WEBSERV_HANDLER_POST_HANDLER_HPP

#include "webserv/http/Request.hpp"
#include "webserv/http/Response.hpp"
#include "webserv/net/Router.hpp"

namespace webserv {
namespace handler {

// POST handler. This branch handles the simple cases: any Content-Type
// where the body is a single blob (raw, application/x-www-form-urlencoded,
// application/json, application/octet-stream, ...). multipart/form-data
// with per-part filenames is feat/20.
//
// Behavior:
//   location.uploadStore set -> body is written to a fresh file inside
//   that directory (0644, O_CREAT|O_EXCL). Response is 201 Created with
//   a Location header pointing at the file name and a small JSON summary.
//
//   location.uploadStore empty -> the body is acknowledged with 200 OK
//   plus a short summary. Useful for form submissions where the server
//   just needs to accept the payload.
//
// The dispatcher has already verified allowed_methods and applied any
// `return CODE`.
void servePost(const webserv::http::Request &req,
               const webserv::RouteMatch    &match,
               webserv::http::Response      &response);

} // namespace handler
} // namespace webserv

#endif
