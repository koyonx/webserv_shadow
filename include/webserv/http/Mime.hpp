#ifndef WEBSERV_HTTP_MIME_HPP
#define WEBSERV_HTTP_MIME_HPP

#include <string>

namespace webserv {
namespace http {

// Returns the MIME type for a filename by extension. Includes '.' in
// the lookup. Unknown extensions yield "application/octet-stream" so
// downstream code can always set Content-Type.
std::string mimeForFilename(const std::string &filename);

} // namespace http
} // namespace webserv

#endif
