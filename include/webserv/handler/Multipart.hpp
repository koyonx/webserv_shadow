#ifndef WEBSERV_HANDLER_MULTIPART_HPP
#define WEBSERV_HANDLER_MULTIPART_HPP

#include <string>
#include <vector>

namespace webserv {
namespace handler {

// One part of a multipart/form-data message.
struct MultipartPart {
	std::string name;         // form field name from Content-Disposition
	std::string filename;     // original filename (empty for pure-value fields)
	std::string contentType;  // Content-Type of the part (may be empty)
	std::string body;         // raw payload bytes
};

// Parse a multipart/form-data body given the boundary from the outer
// Content-Type header. Returns true on success and fills `parts`.
//
// Follows RFC 7578 / 2046 boundary syntax:
//   --boundary CRLF header-lines CRLF CRLF body-bytes CRLF
//   --boundary CRLF header-lines CRLF CRLF body-bytes CRLF
//   --boundary--
// A preamble before the first boundary and epilogue after the closing
// boundary are ignored per RFC.
bool parseMultipart(const std::string          &body,
                    const std::string          &boundary,
                    std::vector<MultipartPart> &parts);

// Extract the boundary parameter from a Content-Type header value.
// Returns the boundary string (unquoted) or empty on failure.
std::string extractBoundary(const std::string &contentType);

// Case-insensitive parameter lookup. `key` is compared without case;
// value is returned unquoted. Empty on absence.
std::string headerParam(const std::string &headerLine, const std::string &key);

// Reduce a filename to its basename and reject unsafe forms
// (empty, ".", "..", control chars). Returns "" if unsafe.
std::string sanitizeUploadName(const std::string &raw);

} // namespace handler
} // namespace webserv

#endif
