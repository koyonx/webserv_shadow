#ifndef WEBSERV_HTTP_REQUEST_PARSER_HPP
#define WEBSERV_HTTP_REQUEST_PARSER_HPP

#include "webserv/http/Request.hpp"

#include <cstddef>
#include <string>

namespace webserv {
namespace http {

enum ParseResult {
	kParseNeedMore = 0,
	kParseComplete = 1,
	kParseError    = 2
};

// Streaming HTTP/1.x parser. feed() takes bytes as they arrive from
// the socket and returns:
//   kParseNeedMore -- keep reading, no bytes were rejected
//   kParseComplete -- everything up through the message body is done
//   kParseError    -- protocol violation; errorStatus() gives the
//                     HTTP status the caller should return
//
// This branch (feat/10) implements only the request-line phase and
// then delegates the headers phase to a temporary CRLFCRLF sniff.
// feat/11 will replace the sniff with real header parsing.
class RequestParser {
public:
	RequestParser();

	// Resets state for the next request (keep-alive reuse).
	void reset();

	// Feed some bytes; 'consumed' is set to the number of bytes the
	// parser accepted (whether accepted-and-buffered or accepted-and-
	// interpreted). Callers should erase [0, consumed) from their read
	// buffer.
	ParseResult feed(const char *data,
	                 std::size_t len,
	                 std::size_t &consumed);

	const Request &request()      const;
	int            errorStatus()  const;
	const char    *errorMessage() const;

	// Limits (defaults are sensible; overridden from Config later).
	void setMaxRequestLine(std::size_t bytes);

private:
	RequestParser(const RequestParser &);
	RequestParser &operator=(const RequestParser &);

	enum Phase {
		kPhaseRequestLine,
		kPhaseHeadersStub,    // temporary: feat/11 replaces with real headers
		kPhaseDone,
		kPhaseError
	};

	ParseResult feedRequestLine(const char *data,
	                            std::size_t len,
	                            std::size_t &consumed);
	ParseResult feedHeadersStub(const char *data,
	                            std::size_t len,
	                            std::size_t &consumed);

	bool  parseAccumulatedLine();
	bool  parseRequestTarget();
	bool  parseVersion(const std::string &ver);

	void  setError(int status, const char *msg);

	Phase       m_phase;
	Request     m_req;
	std::string m_line;        // accumulated request-line bytes
	std::string m_headerBuf;   // temporary buffer for stub CRLFCRLF sniff
	std::size_t m_maxLine;
	int         m_status;
	const char *m_errmsg;
};

} // namespace http
} // namespace webserv

#endif
