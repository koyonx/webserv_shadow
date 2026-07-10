#ifndef WEBSERV_HANDLER_AUTOINDEX_HPP
#define WEBSERV_HANDLER_AUTOINDEX_HPP

#include <string>

namespace webserv {
namespace handler {

// Render a directory as an HTML index page.
//   fsDir       : filesystem directory path (used for readdir)
//   requestPath : user-facing path (shown in <title> and in href
//                 attributes); must end with '/'
//
// Returns 0 and fills `outHtml` on success, or an HTTP status code on
// failure (typically 403 if the directory can't be opened).
int renderAutoindex(const std::string &fsDir,
                    const std::string &requestPath,
                    std::string       &outHtml);

} // namespace handler
} // namespace webserv

#endif
