#ifndef WEBSERV_HANDLER_ERROR_PAGE_HPP
#define WEBSERV_HANDLER_ERROR_PAGE_HPP

#include "webserv/http/Response.hpp"
#include "webserv/net/Router.hpp"

namespace webserv {
namespace handler {

// Emits a status-code error response into `response`.
// If `match` is non-null and its server/location declares an
// error_page for this status, the corresponding file is read and used
// as the response body (Content-Type inferred from the file's MIME).
// The original status code is preserved on the wire so tester tools
// see e.g. "HTTP/1.1 404 Not Found" even though the body came from
// /404.html.
//
// Falls back to a plain-text "STATUS reason" body if there is no
// error_page directive OR if reading the file fails (so a broken
// error_page never causes a chained 500).
void emitError(int                        status,
               const webserv::RouteMatch *match,
               webserv::http::Response   &response);

} // namespace handler
} // namespace webserv

#endif
