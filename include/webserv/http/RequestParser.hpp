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

// Streaming HTTP/1.x request parser.
//
//   feed(data, len, consumed) -> kParseNeedMore | kParseComplete | kParseError
//
// After kParseComplete: request() returns the fully populated Request
// through headers (feat/11); body handling arrives in feat/12.
class RequestParser {
public:
	RequestParser();

	void reset();

	ParseResult feed(const char *data,
	                 std::size_t len,
	                 std::size_t &consumed);

	const Request &request()      const;
	int            errorStatus()  const;
	const char    *errorMessage() const;

	// Limits (defaults are sensible; overridden from Config later).
	void setMaxRequestLine(std::size_t bytes);
	void setMaxHeaderBytes(std::size_t bytes);
	void setMaxHeaderCount(std::size_t n);
	void setMaxBodySize(std::size_t bytes);

private:
	RequestParser(const RequestParser &);
	RequestParser &operator=(const RequestParser &);

	enum Phase {
		kPhaseRequestLine,
		kPhaseHeaders,
		kPhaseBodyCL,          // reading exactly Content-Length bytes
		kPhaseBodyChunkSize,   // reading "hex-size [ext] CRLF"
		kPhaseBodyChunkData,   // reading chunk data
		kPhaseBodyChunkCRLF,   // reading CRLF after chunk data
		kPhaseBodyTrailer,     // reading trailer field-lines (discarded)
		kPhaseDone,
		kPhaseError
	};

	ParseResult feedRequestLine(const char *data,
	                            std::size_t len,
	                            std::size_t &consumed);
	ParseResult feedHeaders(const char *data,
	                        std::size_t len,
	                        std::size_t &consumed);
	ParseResult feedBody(const char *data,
	                     std::size_t len,
	                     std::size_t &consumed);

	bool  parseAccumulatedRequestLine();
	bool  parseRequestTarget();
	bool  parseVersion(const std::string &ver);

	bool  parseHeaderLine(const std::string &line);
	bool  finalizeHeaders();

	bool  parseChunkSizeLine();

	void  setError(int status, const char *msg);

	Phase       m_phase;
	Request     m_req;

	std::string m_line;   // accumulator for request-line, headers, chunk lines

	std::size_t m_maxLine;
	std::size_t m_maxHeaderBytes;
	std::size_t m_maxHeaderCount;
	std::size_t m_maxBody;
	std::size_t m_headerBytesSoFar;
	std::size_t m_headerCountSoFar;

	std::size_t m_bodyBytesRead;
	std::size_t m_chunkRemaining;

	bool        m_seenHost;
	bool        m_seenContentLength;
	bool        m_seenTransferEncoding;

	int         m_status;
	const char *m_errmsg;
};

} // namespace http
} // namespace webserv

#endif
