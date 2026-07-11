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

// Sink for streaming body bytes as the parser consumes them, so a
// consumer (e.g. CgiProcess) can start processing before the whole
// body has arrived. When a sink is set, the parser does NOT grow
// req.body — it forwards every chunk to onBodyChunk() and calls
// onBodyEnd() once the last byte of the declared / chunked body has
// been consumed. Body-size cap enforcement still runs on the raw
// byte count (413 fires the same way).
//
// Callbacks are invoked inline from feed(), so any allocations they
// do count against the same tick's dispatch budget. Must not throw.
class IBodyChunkSink {
public:
	virtual ~IBodyChunkSink() {}
	virtual void onBodyChunk(const char *data, std::size_t len) = 0;
	virtual void onBodyEnd()                                    = 0;
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

	// Attach / detach a streaming body sink. Passing NULL restores the
	// legacy "grow req.body in place" behaviour. Sinks may be set at
	// any point BEFORE the body starts (i.e. before feed() returns
	// from the header phase); attaching mid-body will silently miss
	// the bytes already parsed into req.body.
	void setBodySink(IBodyChunkSink *sink);

	// True once the request-line + headers have been fully consumed
	// (parser is in a body phase or already done). Connection uses
	// this to spawn a CGI early and hook up the body sink before the
	// upload arrives.
	bool headersReady() const;

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

	IBodyChunkSink *m_bodySink;
	bool            m_bodyEndFired;
};

} // namespace http
} // namespace webserv

#endif
