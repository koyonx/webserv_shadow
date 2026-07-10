#include "webserv/cgi/CgiProcess.hpp"

#include "webserv/Error.hpp"
#include "webserv/Log.hpp"
#include "webserv/core/PollLoop.hpp"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace webserv {
namespace cgi {

CgiProcess::CgiProcess(const std::string              &interpreter,
                       const std::string              &scriptPath,
                       const std::string              &scriptWorkDir,
                       const std::vector<std::string> &env,
                       const std::string              &body,
                       ICgiCallback                   &cb,
                       long                            totalTimeoutMs,
                       long                            killEscalationMs)
	: m_interp(interpreter),
	  m_script(scriptPath),
	  m_workDir(scriptWorkDir),
	  m_env(env),
	  m_body(body),
	  m_bodyPos(0),
	  m_output(),
	  m_pid(-1),
	  m_cb(cb),
	  m_in(NULL),
	  m_out(NULL),
	  m_stdinClosed(false),
	  m_stdoutClosed(false),
	  m_finished(false),
	  m_totalTimeoutMs(totalTimeoutMs),
	  m_killEscalationMs(killEscalationMs),
	  m_killState(0)
{}

CgiProcess::~CgiProcess()
{
	delete m_in;
	delete m_out;
	if (m_pid > 0) {
		::kill(m_pid, SIGKILL);
		int status = 0;
		::waitpid(m_pid, &status, 0);
	}
}

void CgiProcess::spawn(PollLoop &loop)
{
	int in_pipe[2]  = { -1, -1 };
	int out_pipe[2] = { -1, -1 };
	if (::pipe(in_pipe) < 0) {
		throw SystemError("cgi: pipe(stdin)", errno);
	}
	if (::pipe(out_pipe) < 0) {
		int e = errno;
		::close(in_pipe[0]);
		::close(in_pipe[1]);
		throw SystemError("cgi: pipe(stdout)", e);
	}

	pid_t pid = ::fork();
	if (pid < 0) {
		int e = errno;
		::close(in_pipe[0]);  ::close(in_pipe[1]);
		::close(out_pipe[0]); ::close(out_pipe[1]);
		throw SystemError("cgi: fork", e);
	}

	if (pid == 0) {
		// -------- Child --------
		::dup2(in_pipe[0],  STDIN_FILENO);
		::dup2(out_pipe[1], STDOUT_FILENO);
		::close(in_pipe[0]);
		::close(in_pipe[1]);
		::close(out_pipe[0]);
		::close(out_pipe[1]);
		// Close every inherited fd above stderr. Brute-force loop is
		// portable (BSD closefrom(3) isn't universally available).
		for (int fd = 3; fd < 256; ++fd) {
			::close(fd);
		}
		if (!m_workDir.empty()) {
			::chdir(m_workDir.c_str());
		}
		char *argv[3];
		argv[0] = const_cast<char *>(m_interp.c_str());
		argv[1] = const_cast<char *>(m_script.c_str());
		argv[2] = NULL;
		std::vector<char *> envp;
		envp.reserve(m_env.size() + 1);
		for (std::size_t i = 0; i < m_env.size(); ++i) {
			envp.push_back(const_cast<char *>(m_env[i].c_str()));
		}
		envp.push_back(NULL);
		::execve(m_interp.c_str(), argv, &envp[0]);
		// execve failed
		::_exit(127);
	}

	// -------- Parent --------
	::close(in_pipe[0]);
	::close(out_pipe[1]);
	m_pid = pid;

	int  stdinWrite = in_pipe[1];
	int  stdoutRead = out_pipe[0];
	try {
		setNonBlocking(stdinWrite);
		setNonBlocking(stdoutRead);
	} catch (...) {
		::close(stdinWrite);
		::close(stdoutRead);
		::kill(m_pid, SIGKILL);
		int st = 0;
		::waitpid(m_pid, &st, 0);
		m_pid = -1;
		throw;
	}

	m_in  = new StdinFd(this,  stdinWrite);
	m_out = new StdoutFd(this, stdoutRead);
	loop.add(m_in);
	loop.add(m_out);

	if (m_body.empty()) {
		// Nothing to send — close stdin now so the child doesn't wait.
		m_in->closeFd();
		m_stdinClosed = true;
		loop.remove(m_in);
	}

	// Arm the total-runtime deadline on m_out; onDeadlineExpired
	// escalates SIGTERM -> SIGKILL -> forced completion.
	if (m_totalTimeoutMs > 0) {
		loop.setDeadline(m_out, m_totalTimeoutMs);
	}

	LOG_INFO("cgi: spawned pid=" << m_pid
	         << " " << m_interp << " " << m_script
	         << " timeout=" << m_totalTimeoutMs << "ms");
}

void CgiProcess::abort(PollLoop &loop)
{
	if (m_in  != NULL) { loop.remove(m_in);  }
	if (m_out != NULL) { loop.remove(m_out); }
	if (m_pid > 0) {
		::kill(m_pid, SIGKILL);
		int st = 0;
		::waitpid(m_pid, &st, 0);
		m_pid = -1;
	}
}

void CgiProcess::onStdinReady(PollLoop &loop)
{
	if (m_stdinClosed || m_in == NULL) return;
	if (m_bodyPos >= m_body.size()) {
		m_in->closeFd();
		m_stdinClosed = true;
		loop.remove(m_in);
		tryFinish(loop);
		return;
	}
	const char *data      = m_body.data() + m_bodyPos;
	std::size_t remaining = m_body.size() - m_bodyPos;
	ssize_t w = ::write(m_in->fd(), data, remaining);
	if (w <= 0) {
		m_in->closeFd();
		m_stdinClosed = true;
		loop.remove(m_in);
		return;
	}
	m_bodyPos += static_cast<std::size_t>(w);
	if (m_bodyPos >= m_body.size()) {
		m_in->closeFd();
		m_stdinClosed = true;
		loop.remove(m_in);
		tryFinish(loop);
	}
}

void CgiProcess::onStdoutReady(PollLoop &loop)
{
	if (m_stdoutClosed || m_out == NULL) return;

	char    buf[4096];
	ssize_t r = ::read(m_out->fd(), buf, sizeof(buf));
	if (r > 0) {
		m_output.append(buf, static_cast<std::size_t>(r));
		return;
	}
	// EOF or transient error — treat as end-of-child-output.
	m_stdoutClosed = true;
	m_out->closeFd();
	loop.remove(m_out);
	tryFinish(loop);
}

void CgiProcess::tryFinish(PollLoop &loop)
{
	(void)loop;
	if (m_finished)         return;
	if (!m_stdoutClosed)    return;   // still receiving

	// Reap the child. Prefer WNOHANG so a badly-behaved CGI can't wedge
	// the event loop. If it hasn't exited, escalate SIGTERM -> SIGKILL.
	int status = 0;
	if (m_pid > 0) {
		pid_t r = ::waitpid(m_pid, &status, WNOHANG);
		if (r == 0) {
			::kill(m_pid, SIGTERM);
			// Give it a brief window; feat/23 replaces this with a
			// timer-driven escalation.
			r = ::waitpid(m_pid, &status, WNOHANG);
			if (r == 0) {
				::kill(m_pid, SIGKILL);
				::waitpid(m_pid, &status, 0);
			}
		}
		m_pid = -1;
	}
	int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	reapAndCallback(exitCode);
}

void CgiProcess::reapAndCallback(int status)
{
	if (m_finished) return;
	m_finished = true;
	LOG_INFO("cgi: exit status=" << status
	         << " output=" << m_output.size() << "B");
	m_cb.onCgiComplete(status, m_output);
}

void CgiProcess::onDeadlineExpired(PollLoop &loop)
{
	if (m_finished || m_pid <= 0) return;
	if (m_killState == 0) {
		LOG_WARN("cgi: pid=" << m_pid << " runtime deadline hit -> SIGTERM");
		::kill(m_pid, SIGTERM);
		m_killState = 1;
		loop.setDeadline(m_out, m_killEscalationMs);
		return;
	}
	if (m_killState == 1) {
		LOG_WARN("cgi: pid=" << m_pid << " still alive -> SIGKILL");
		::kill(m_pid, SIGKILL);
		m_killState = 2;
		loop.setDeadline(m_out, m_killEscalationMs);
		return;
	}
	// SIGKILL sent and still not done -> force finish; report as
	// killed (status = -1) so Connection returns 504.
	LOG_ERROR("cgi: pid=" << m_pid << " unresponsive after SIGKILL, forcing finish");
	m_stdoutClosed = true;
	if (m_out != NULL) {
		m_out->closeFd();
		loop.remove(m_out);
	}
	int st = 0;
	::waitpid(m_pid, &st, WNOHANG);
	m_pid = -1;
	reapAndCallback(-1);
}

// ---------- StdinFd ----------

CgiProcess::StdinFd::StdinFd(CgiProcess *o, int fd) : m_owner(o), m_fd(fd) {}
CgiProcess::StdinFd::~StdinFd() {}

int   CgiProcess::StdinFd::fd()         const { return m_fd.get(); }
short CgiProcess::StdinFd::wantEvents() const
{
	if (m_fd.get() < 0)                  return 0;
	if (m_owner->m_stdinClosed)          return 0;
	return POLLOUT;
}
void CgiProcess::StdinFd::onReadable(PollLoop &) {}
void CgiProcess::StdinFd::onWritable(PollLoop &loop) { m_owner->onStdinReady(loop); }
void CgiProcess::StdinFd::onTimeout(PollLoop &) {}
void CgiProcess::StdinFd::closeFd() { m_fd.close(); }

// ---------- StdoutFd ----------

CgiProcess::StdoutFd::StdoutFd(CgiProcess *o, int fd) : m_owner(o), m_fd(fd) {}
CgiProcess::StdoutFd::~StdoutFd() {}

int   CgiProcess::StdoutFd::fd()         const { return m_fd.get(); }
short CgiProcess::StdoutFd::wantEvents() const
{
	if (m_fd.get() < 0)                  return 0;
	if (m_owner->m_stdoutClosed)         return 0;
	return POLLIN;
}
void CgiProcess::StdoutFd::onReadable(PollLoop &loop) { m_owner->onStdoutReady(loop); }
void CgiProcess::StdoutFd::onWritable(PollLoop &) {}
void CgiProcess::StdoutFd::onTimeout(PollLoop &loop)  { m_owner->onDeadlineExpired(loop); }
void CgiProcess::StdoutFd::closeFd() { m_fd.close(); }

} // namespace cgi
} // namespace webserv
