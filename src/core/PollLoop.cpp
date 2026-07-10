#include "webserv/core/PollLoop.hpp"

#include "webserv/Error.hpp"
#include "webserv/Fd.hpp"
#include "webserv/Log.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace webserv {

// ------------------------- MonoTime -------------------------

MonoTime::MonoTime() : sec(0), nsec(0) {}

MonoTime MonoTime::now()
{
	struct timespec ts;
	ts.tv_sec  = 0;
	ts.tv_nsec = 0;
	if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		throw SystemError("clock_gettime(CLOCK_MONOTONIC) failed", errno);
	}
	MonoTime t;
	t.sec  = ts.tv_sec;
	t.nsec = ts.tv_nsec;
	return t;
}

MonoTime MonoTime::plusMs(long ms) const
{
	MonoTime t;
	t.sec  = sec  + ms / 1000;
	t.nsec = nsec + (ms % 1000) * 1000000L;
	if (t.nsec >= 1000000000L) {
		t.sec  += 1;
		t.nsec -= 1000000000L;
	} else if (t.nsec < 0) {
		t.sec  -= 1;
		t.nsec += 1000000000L;
	}
	return t;
}

long MonoTime::msUntil(const MonoTime &deadline) const
{
	long dsec  = static_cast<long>(deadline.sec  - sec);
	long dnsec = static_cast<long>(deadline.nsec - nsec);
	return dsec * 1000L + dnsec / 1000000L;
}

bool MonoTime::operator<(const MonoTime &o) const
{
	if (sec != o.sec) {
		return sec < o.sec;
	}
	return nsec < o.nsec;
}

bool MonoTime::operator==(const MonoTime &o) const
{
	return sec == o.sec && nsec == o.nsec;
}

// ------------------------- statics -------------------------

PollLoop                *PollLoop::s_instance      = NULL;
int                      PollLoop::s_selfPipeWrite = -1;
volatile sig_atomic_t    PollLoop::s_sigFlags      = 0;

// bit layout of s_sigFlags
static const sig_atomic_t kFlagStopFast     = 1;
static const sig_atomic_t kFlagStopGraceful = 2;

// ------------------------- ctor/dtor -------------------------

PollLoop::PollLoop()
	: m_prevSigint(NULL),
	  m_prevSigterm(NULL),
	  m_prevSigquit(NULL),
	  m_prevSigpipe(NULL),
	  m_selfPipeRead(-1),
	  m_handlers(),
	  m_pfds(),
	  m_indexOf(),
	  m_deadlines(),
	  m_timerHeap(),
	  m_timerSeq(0),
	  m_dispatching(false),
	  m_pendingRemove(),
	  m_shuttingDown(false),
	  m_graceful(false),
	  m_stopRequested(false)
{
	if (s_instance != NULL) {
		throw Exception("PollLoop: only one instance allowed per process");
	}

	int fds[2];
	if (::pipe(fds) != 0) {
		throw SystemError("PollLoop: pipe() failed", errno);
	}
	m_selfPipeRead   = fds[0];
	s_selfPipeWrite  = fds[1];

	try {
		setNonBlocking(m_selfPipeRead);
		setNonBlocking(s_selfPipeWrite);
	} catch (...) {
		::close(m_selfPipeRead);
		::close(s_selfPipeWrite);
		m_selfPipeRead  = -1;
		s_selfPipeWrite = -1;
		throw;
	}

	s_instance = this;
	installSignalHandlers();
}

PollLoop::~PollLoop()
{
	uninstallSignalHandlers();
	if (m_selfPipeRead >= 0)  { ::close(m_selfPipeRead);  m_selfPipeRead  = -1; }
	if (s_selfPipeWrite >= 0) { ::close(s_selfPipeWrite); s_selfPipeWrite = -1; }
	s_instance = NULL;
	s_sigFlags = 0;
}

// ------------------------- signals -------------------------

void PollLoop::signalHandler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		s_sigFlags |= kFlagStopFast;
	} else if (signum == SIGQUIT) {
		s_sigFlags |= kFlagStopGraceful;
	}
	// Wake up the loop. Ignore result: pipe full means a wake is
	// already pending, which is fine.
	if (s_selfPipeWrite >= 0) {
		char c = 'x';
		::write(s_selfPipeWrite, &c, 1);
	}
}

void PollLoop::installSignalHandlers()
{
	m_prevSigint  = ::signal(SIGINT,  &PollLoop::signalHandler);
	m_prevSigterm = ::signal(SIGTERM, &PollLoop::signalHandler);
	m_prevSigquit = ::signal(SIGQUIT, &PollLoop::signalHandler);
	m_prevSigpipe = ::signal(SIGPIPE, SIG_IGN);
}

void PollLoop::uninstallSignalHandlers()
{
	if (m_prevSigint  != NULL) { ::signal(SIGINT,  m_prevSigint);  }
	if (m_prevSigterm != NULL) { ::signal(SIGTERM, m_prevSigterm); }
	if (m_prevSigquit != NULL) { ::signal(SIGQUIT, m_prevSigquit); }
	if (m_prevSigpipe != NULL) { ::signal(SIGPIPE, m_prevSigpipe); }
}

void PollLoop::handleSelfPipe()
{
	char buf[64];
	while (true) {
		ssize_t r = ::read(m_selfPipeRead, buf, sizeof(buf));
		if (r <= 0) {
			// non-blocking: EAGAIN when drained. Do not check errno
			// per subject; the poll loop will re-fire if needed.
			break;
		}
	}
}

void PollLoop::processSignals()
{
	sig_atomic_t flags = s_sigFlags;
	if (flags == 0) {
		return;
	}
	s_sigFlags = 0;
	if (flags & kFlagStopFast) {
		LOG_INFO("PollLoop: SIGINT/SIGTERM received -> fast shutdown");
		m_shuttingDown  = true;
		m_graceful      = false;
		m_stopRequested = true;
	} else if (flags & kFlagStopGraceful) {
		LOG_INFO("PollLoop: SIGQUIT received -> graceful shutdown");
		m_shuttingDown  = true;
		m_graceful      = true;
		m_stopRequested = true;
	}
}

// ------------------------- registration -------------------------

void PollLoop::add(IHandler *h)
{
	if (m_indexOf.find(h) != m_indexOf.end()) {
		return;  // already registered
	}
	m_handlers.push_back(h);
	m_indexOf[h] = m_handlers.size() - 1;
}

void PollLoop::remove(IHandler *h)
{
	if (m_dispatching) {
		m_pendingRemove.insert(h);
		return;
	}
	std::map<IHandler*, std::size_t>::iterator it = m_indexOf.find(h);
	if (it == m_indexOf.end()) {
		return;
	}
	std::size_t idx  = it->second;
	std::size_t last = m_handlers.size() - 1;
	if (idx != last) {
		m_handlers[idx] = m_handlers[last];
		m_indexOf[m_handlers[idx]] = idx;
	}
	m_handlers.pop_back();
	m_indexOf.erase(it);
	m_deadlines.erase(h);
	// Heap entries for this handler will be lazily discarded.
}

void PollLoop::drainPendingRemoves()
{
	std::set<IHandler*> pending;
	pending.swap(m_pendingRemove);
	for (std::set<IHandler*>::iterator it = pending.begin();
	     it != pending.end(); ++it) {
		remove(*it);
	}
}

// ------------------------- timers -------------------------

bool PollLoop::timerGreater(const TimerEntry &a, const TimerEntry &b)
{
	if (a.deadline < b.deadline) return false;
	if (b.deadline < a.deadline) return true;
	return a.seq > b.seq;
}

void PollLoop::setDeadline(IHandler *h, long timeoutMs)
{
	MonoTime dl = MonoTime::now().plusMs(timeoutMs);
	m_deadlines[h] = dl;
	TimerEntry e;
	e.deadline = dl;
	e.handler  = h;
	e.seq      = ++m_timerSeq;
	m_timerHeap.push_back(e);
	std::push_heap(m_timerHeap.begin(), m_timerHeap.end(), &PollLoop::timerGreater);
}

void PollLoop::clearDeadline(IHandler *h)
{
	m_deadlines.erase(h);
	// heap entry becomes stale; discarded next tickHeap().
}

void PollLoop::tickHeap()
{
	while (!m_timerHeap.empty()) {
		const TimerEntry &top = m_timerHeap.front();
		std::map<IHandler*, MonoTime>::iterator it = m_deadlines.find(top.handler);
		if (it == m_deadlines.end() || !(it->second == top.deadline)) {
			std::pop_heap(m_timerHeap.begin(), m_timerHeap.end(),
			              &PollLoop::timerGreater);
			m_timerHeap.pop_back();
			continue;
		}
		break;
	}
}

void PollLoop::dispatchExpired(const MonoTime &now)
{
	tickHeap();
	while (!m_timerHeap.empty()) {
		const TimerEntry &top = m_timerHeap.front();
		if (now < top.deadline) {
			break;
		}
		IHandler *h = top.handler;
		m_deadlines.erase(h);
		std::pop_heap(m_timerHeap.begin(), m_timerHeap.end(),
		              &PollLoop::timerGreater);
		m_timerHeap.pop_back();
		if (m_pendingRemove.count(h) > 0) {
			continue;
		}
		if (m_indexOf.find(h) == m_indexOf.end()) {
			continue;  // handler already gone
		}
		h->onTimeout(*this);
		tickHeap();
	}
}

long PollLoop::computePollTimeout(const MonoTime &now, long budgetMs)
{
	long timeout = -1;
	tickHeap();
	if (!m_timerHeap.empty()) {
		long msUntil = now.msUntil(m_timerHeap.front().deadline);
		if (msUntil < 0) msUntil = 0;
		timeout = msUntil;
	}
	if (budgetMs >= 0) {
		if (timeout < 0 || budgetMs < timeout) {
			timeout = budgetMs;
		}
	}
	return timeout;
}

// ------------------------- main loop -------------------------

void PollLoop::rebuildPfds()
{
	m_pfds.clear();
	m_pfds.reserve(m_handlers.size() + 1);

	struct pollfd sp;
	sp.fd      = m_selfPipeRead;
	sp.events  = POLLIN;
	sp.revents = 0;
	m_pfds.push_back(sp);

	for (std::size_t i = 0; i < m_handlers.size(); ++i) {
		IHandler *h = m_handlers[i];
		struct pollfd p;
		p.fd      = h->fd();
		p.events  = h->wantEvents();
		p.revents = 0;
		m_pfds.push_back(p);
	}
}

void PollLoop::requestStop(bool graceful)
{
	m_stopRequested = true;
	m_shuttingDown  = true;
	m_graceful      = graceful;
}

void PollLoop::run(long stopAfterMs)
{
	MonoTime start = MonoTime::now();

	while (true) {
		processSignals();

		if (m_stopRequested) {
			if (!m_graceful) {
				break;
			}
			// Graceful: exit once only bookkeeping remains. Callers
			// (e.g. Listener) are expected to remove themselves on
			// stop request so remaining handlers are just the
			// in-flight connections. When they finish, they too
			// remove themselves.
			if (m_handlers.empty()) {
				break;
			}
		}

		MonoTime now = MonoTime::now();

		long budgetMs = -1;
		if (stopAfterMs >= 0) {
			long elapsed = start.msUntil(now);
			// elapsed here is now - start; a positive value means time
			// has passed. If elapsed >= stopAfterMs, we should exit.
			long remaining = stopAfterMs - elapsed;
			if (remaining <= 0) {
				break;
			}
			budgetMs = remaining;
		}

		long timeoutMs = computePollTimeout(now, budgetMs);

		rebuildPfds();
		int r = ::poll(&m_pfds[0], m_pfds.size(), static_cast<int>(timeoutMs));
		if (r < 0) {
			// EINTR or transient poll failure. Loop back; the signal
			// handler will have set flags we check on next iteration.
			continue;
		}

		MonoTime nowAfter = MonoTime::now();

		if (m_pfds[0].revents & POLLIN) {
			handleSelfPipe();
		}

		m_dispatching = true;
		for (std::size_t i = 0; i < m_handlers.size(); ++i) {
			IHandler *h = m_handlers[i];
			if (m_pendingRemove.count(h) > 0) {
				continue;
			}
			short revents = m_pfds[i + 1].revents;
			if (revents == 0) {
				continue;
			}
			if (revents & POLLNVAL) {
				LOG_ERROR("PollLoop: POLLNVAL on fd " << h->fd()
				          << " (handler removed?)");
				m_pendingRemove.insert(h);
				continue;
			}
			if (revents & (POLLIN | POLLHUP | POLLERR)) {
				h->onReadable(*this);
				if (m_pendingRemove.count(h) > 0) {
					continue;
				}
			}
			if (revents & POLLOUT) {
				h->onWritable(*this);
			}
		}
		dispatchExpired(nowAfter);
		m_dispatching = false;
		drainPendingRemoves();
	}
}

} // namespace webserv
