#ifndef WEBSERV_CGI_PROCESS_HPP
#define WEBSERV_CGI_PROCESS_HPP

#include "webserv/Fd.hpp"
#include "webserv/core/IHandler.hpp"

#include <cstddef>
#include <string>
#include <sys/types.h>
#include <vector>

namespace webserv {
namespace cgi {

// Streaming-friendly CGI callback interface (RFC 3875 §6).
//
// The three methods fire in order:
//   onCgiHeaders(header_block)  — exactly once, when the CGI has
//     produced its blank-line terminator (or a fatal early EOF /
//     header-cap error, in which case header_block is empty and
//     Connection must emit its own 502).
//   onCgiBodyChunk(data, len)   — zero-or-more times, as stdout
//     bytes arrive after the header block. Body may be empty.
//   onCgiEnd(exit_status)       — exactly once, when the child is
//     reaped. exit_status = WEXITSTATUS on normal exit, -1 on
//     kill/timeout/exec failure.
//
// The interface is designed so an implementation can commit to
// streaming (chunked response) at onCgiHeaders time and no longer
// need to buffer the CGI's body — critical for the 100 MB × N
// concurrent POST scenario. A trivially-buffering implementation is
// also fine (accumulate chunks, apply at end).
class ICgiCallback {
public:
	virtual ~ICgiCallback() {}
	virtual void onCgiHeaders(const std::string &headerBlock)         = 0;
	virtual void onCgiBodyChunk(const char *data, std::size_t len)    = 0;
	virtual void onCgiEnd(int exitStatus)                             = 0;
};

// A running CGI process:
//   - Forks the child, pipes stdin/stdout, execve()s the interpreter.
//   - Registers two IHandlers with PollLoop so writes to the child's
//     stdin and reads from its stdout both go through poll(2). No
//     blocking read/write on the parent side.
//   - When the request body is fully written we close stdin so the
//     child sees EOF. When stdout returns 0 (EOF) we waitpid + invoke
//     the callback.
//
// Non-copyable. Ownership sits with the initiator (Connection).
class CgiProcess {
public:
	CgiProcess(const std::string              &interpreter,
	           const std::string              &scriptPath,
	           const std::string              &scriptWorkDir,
	           const std::vector<std::string> &env,
	           const std::string              &body,
	           ICgiCallback                   &cb,
	           long                            totalTimeoutMs = 30000,
	           long                            killEscalationMs = 100);
	~CgiProcess();

	// Spawn the child and register the pipe fds with the loop.
	// Throws SystemError on pipe/fork failure. execve failure is
	// reported through the callback (status = 127).
	void spawn(PollLoop &loop);

	// Best-effort kill; safe to call whether the child is still
	// running or already reaped. Idempotent.
	void abort(PollLoop &loop);

	pid_t pid() const { return m_pid; }

	// Called by the internal pipe handlers.
	void  onStdinReady(PollLoop &loop);
	void  onStdoutReady(PollLoop &loop);
	void  onDeadlineExpired(PollLoop &loop);

private:
	CgiProcess(const CgiProcess &);
	CgiProcess &operator=(const CgiProcess &);

	// Two IHandler adapters — one per pipe end.
	class StdinFd : public IHandler {
	public:
		StdinFd(CgiProcess *o, int fd);
		virtual ~StdinFd();
		virtual int   fd()          const;
		virtual short wantEvents()  const;
		virtual void  onReadable(PollLoop &);
		virtual void  onWritable(PollLoop &);
		virtual void  onTimeout(PollLoop &);
		void          closeFd();
	private:
		CgiProcess *m_owner;
		Fd          m_fd;
	};
	class StdoutFd : public IHandler {
	public:
		StdoutFd(CgiProcess *o, int fd);
		virtual ~StdoutFd();
		virtual int   fd()          const;
		virtual short wantEvents()  const;
		virtual void  onReadable(PollLoop &);
		virtual void  onWritable(PollLoop &);
		virtual void  onTimeout(PollLoop &);
		void          closeFd();
	private:
		CgiProcess *m_owner;
		Fd          m_fd;
	};

	void          tryFinish(PollLoop &loop);
	void          reapAndCallback(int status);

	std::string               m_interp;
	std::string               m_script;
	std::string               m_workDir;
	std::vector<std::string>  m_env;
	std::string               m_body;
	std::size_t               m_bodyPos;
	std::string               m_output;

	pid_t                     m_pid;
	ICgiCallback             &m_cb;

	StdinFd                  *m_in;
	StdoutFd                 *m_out;
	bool                      m_stdinClosed;
	bool                      m_stdoutClosed;
	bool                      m_finished;

	long                      m_totalTimeoutMs;
	long                      m_killEscalationMs;
	int                       m_killState;    // 0=alive, 1=SIGTERM, 2=SIGKILL
};

} // namespace cgi
} // namespace webserv

#endif
