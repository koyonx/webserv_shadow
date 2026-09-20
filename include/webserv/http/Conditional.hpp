#ifndef WEBSERV_HTTP_CONDITIONAL_HPP
#define WEBSERV_HTTP_CONDITIONAL_HPP

#include "webserv/http/Request.hpp"

#include <cstddef>
#include <ctime>
#include <string>

namespace webserv {
namespace http {

// -------- Small time helpers --------

// Convert a time_t to RFC 7231 IMF-fixdate ("Sun, 06 Nov 1994 08:49:37 GMT").
// Always emits UTC, always in the English C locale — never uses strftime()
// so the output stays stable regardless of the process locale.
std::string httpDateFromTime(std::time_t t);

// Parse the same IMF-fixdate format back into a time_t.
// Returns true on success. Only IMF-fixdate is accepted (browsers always
// use this form for If-Modified-Since / If-Range).
bool parseHttpDate(const std::string &s, std::time_t &out);

// Build a strong ETag from a file's (size, mtime) pair in the nginx-style
// hex form "\"<hex-mtime>-<hex-size>\"". Cheap to compute, cheap to compare,
// works fine for conditional GET.
std::string buildETag(std::size_t size, std::time_t mtime);

// -------- Precondition evaluation --------

enum PreconditionResult {
	kProceedFull,        // Serve the full 200 response
	kProceedNotModified  // Client's cache is fresh -> 304
};

// Apply RFC 7232 preconditions to a GET/HEAD:
//   - If-None-Match wins over If-Modified-Since when both are present.
//   - "*" in If-None-Match always matches an existing resource.
//   - Comma-separated ETag list is walked; any match triggers 304.
//   - If-Modified-Since is compared to the resource mtime with second
//     granularity; equal is treated as "not modified".
PreconditionResult evaluatePreconditions(
	const HeaderMap &reqHeaders,
	std::time_t      mtime,
	const std::string &etag);

// -------- Range parsing --------

struct RangeSpec {
	std::size_t start;   // first byte, inclusive
	std::size_t end;     // last byte, inclusive
	std::size_t total;   // resource size
};

enum RangeResult {
	kRangeAbsent,          // No Range header -> serve 200
	kRangeValid,           // Serve 206 with body[start..end]
	kRangeUnsatisfiable,   // Serve 416 with Content-Range: bytes */total
	kRangeIgnored          // If-Range mismatch or malformed -> serve 200
};

// Parse a Range header and honor If-Range against the current
// representation. Supports the three single-range forms tester tools
// and browsers actually send:
//     bytes=X-Y      (X..Y inclusive)
//     bytes=X-       (X..size-1)
//     bytes=-N       (last N bytes)
// Multi-range (comma-separated) is treated as kRangeIgnored so we can
// fall back to a plain 200 without owning a multipart/byteranges
// encoder.
RangeResult parseRange(const HeaderMap  &reqHeaders,
                       std::size_t       fileSize,
                       std::time_t       mtime,
                       const std::string &etag,
                       RangeSpec        &out);

} // namespace http
} // namespace webserv

#endif
