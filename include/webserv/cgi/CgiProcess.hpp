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

// Callback invoked once the CGI process has finished:
//   status = WEXITSTATUS(status) if the process exited normally,
//          = -1 if the process was killed or failed to launch.
// stdoutData is the raw bytes read from the child's stdout.
class ICgiCallback {
public:
	virtual ~ICgiCallback() {}
	virtual void onCgiComplete(int status, const std::string &stdoutData) = 0;
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
	           ICgiCallback                   &cb);
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
};

} // namespace cgi
} // namespace webserv

#endif
