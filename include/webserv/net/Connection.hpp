#ifndef WEBSERV_NET_CONNECTION_HPP
#define WEBSERV_NET_CONNECTION_HPP

#include "webserv/Fd.hpp"
#include "webserv/cgi/CgiProcess.hpp"
#include "webserv/config/Config.hpp"
#include "webserv/core/IHandler.hpp"
#include "webserv/http/RequestParser.hpp"

#include <cstddef>
#include <string>

namespace webserv {

class Connection;
class Router;

// Owner-side callback: called by the Connection when it is finished
// with its life cycle (response fully written, EOF, or timeout). The
// owner is responsible for actually deleting the Connection object
// once it is safe to do so (typically at end-of-tick).
class IConnectionOwner {
public:
	virtual ~IConnectionOwner() {}
	virtual void notifyDone(Connection *c) = 0;
};

// Per-client state machine.
//
// This branch ships the SKELETON only: after receiving CRLFCRLF (or
// LFLF) the connection generates a canned "200 OK / hello" response
// and finishes. Real HTTP parsing lands in feat/10+ but is drop-in
// against this same interface -- the state transitions and event
// plumbing here won't change.
class Connection : public IHandler, public webserv::cgi::ICgiCallback {
public:
	enum State {
		kReadingRequest,
		kRunningCgi,
		kWritingResponse,
		kClosing
	};

	Connection(int                            cfd,
	           const webserv::config::Listen &origin,
	           IConnectionOwner              &owner,
	           long                           idleTimeoutMs,
	           const Router                  *router);
	virtual ~Connection();

	// IHandler
	virtual int   fd()          const;
	virtual short wantEvents()  const;
	virtual void  onReadable(PollLoop &loop);
	virtual void  onWritable(PollLoop &loop);
	virtual void  onTimeout(PollLoop &loop);

	bool  isDone() const;
	State state()  const;

	// ICgiCallback (see webserv/cgi/CgiProcess.hpp for the contract).
	virtual void onCgiHeaders(const std::string &headerBlock);
	virtual void onCgiBodyChunk(const char *data, std::size_t len);
	virtual void onCgiEnd(int exitStatus);

private:
	Connection(const Connection &);
	Connection &operator=(const Connection &);

	void generateStubResponse();
	void generateErrorResponse(int status);
	void finish(PollLoop &loop);

	// Attempt to route this request into CGI. Returns true if we
	// started a CGI process and moved to kRunningCgi.
	bool tryStartCgi(PollLoop &loop);

	Fd                             m_fd;
	webserv::config::Listen        m_origin;
	IConnectionOwner              &m_owner;
	long                           m_idleMs;
	const Router                  *m_router;
	State                          m_state;
	std::string                    m_readBuf;
	std::string                    m_writeBuf;
	std::size_t                    m_writePos;
	bool                           m_done;

	webserv::http::RequestParser   m_parser;

	webserv::cgi::CgiProcess      *m_cgi;   // owned during kRunningCgi

	// "Graveyard" slot: onCgiComplete() runs from inside a PollLoop
	// dispatch tick (via StdoutFd::onReadable). Deleting m_cgi there
	// would free its inner StdinFd/StdoutFd handlers while PollLoop
	// still holds pointers to them in m_handlers (pending removal is
	// only drained at end-of-tick). To avoid the heap-use-after-free
	// in rebuildPfds() on the NEXT tick — and any handler
	// dereference after that — we stash the CgiProcess here and
	// delete it on entry to the next Connection callback (onWritable
	// or the destructor), by which time PollLoop has already dropped
	// the pointers.
	webserv::cgi::CgiProcess      *m_deadCgi;

	// Streaming CGI state. Set up at onCgiHeaders time and torn
	// down at onCgiEnd. S1 keeps behavior identical to the pre-
	// split code (buffer everything, apply at end); S5 will
	// replace this with real chunked-response streaming so a
	// 100 MB CGI body no longer sits in memory.
	std::string                    m_cgiHeaderBlock;
	std::string                    m_cgiBodyBuf;
	bool                           m_cgiHeadersSeen;
};

} // namespace webserv

#endif
