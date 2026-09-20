#ifndef WEBSERV_CORE_POLL_LOOP_HPP
#define WEBSERV_CORE_POLL_LOOP_HPP

#include "webserv/core/IHandler.hpp"

#include <csignal>
#include <cstddef>
#include <ctime>
#include <map>
#include <poll.h>
#include <set>
#include <vector>

namespace webserv {

struct MonoTime {
	std::time_t sec;
	long        nsec;

	MonoTime();
	static MonoTime now();

	MonoTime plusMs(long ms) const;
	long     msUntil(const MonoTime &deadline) const;

	bool operator<(const MonoTime &o) const;
	bool operator==(const MonoTime &o) const;
};

// Single-threaded event loop built around poll(2). One IHandler owns
// one fd. Handlers register with add(); their fd + wantEvents are
// re-read every tick so state changes take effect the next iteration
// without an explicit "modify" call.
//
// Signals: PollLoop installs handlers for SIGINT/SIGTERM (fast stop)
// and SIGQUIT (graceful stop) at construction and undoes them at
// destruction. SIGPIPE is set to SIG_IGN. Only one PollLoop can exist
// at a time.
class PollLoop {
public:
	PollLoop();
	~PollLoop();

	void add(IHandler *h);
	void remove(IHandler *h);

	// Absolute deadline = now + timeoutMs. Overwrites any prior
	// deadline for the same handler.
	void setDeadline(IHandler *h, long timeoutMs);
	void clearDeadline(IHandler *h);

	// Run until requestStop() is called (or a signal arrives). If
	// stopAfterMs >= 0, the loop also exits once that much wall-time
	// has passed since run() was entered (used by tests).
	void run(long stopAfterMs = -1);

	// From any handler: request a shutdown.
	//   graceful=false -> break out of the loop immediately
	//   graceful=true  -> keep running until only the self-pipe remains
	void requestStop(bool graceful);

	bool isShuttingDown() const { return m_shuttingDown; }
	bool isGraceful()     const { return m_graceful; }

private:
	PollLoop(const PollLoop &);
	PollLoop &operator=(const PollLoop &);

	struct TimerEntry {
		MonoTime      deadline;
		IHandler     *handler;
		unsigned long seq;
	};

	static bool timerGreater(const TimerEntry &a, const TimerEntry &b);

	void rebuildPfds();
	void handleSelfPipe();
	void processSignals();
	void dispatchExpired(const MonoTime &now);
	void tickHeap();
	long computePollTimeout(const MonoTime &now, long budgetMs);
	void drainPendingRemoves();

	void installSignalHandlers();
	void uninstallSignalHandlers();

	static void signalHandler(int signum);

	// Static for the signal handler's benefit. Only one instance at
	// a time (enforced by constructor).
	static PollLoop                    *s_instance;
	static int                          s_selfPipeWrite;
	static volatile sig_atomic_t        s_sigFlags;

	// Saved dispositions so we can restore on ~PollLoop.
	void (*m_prevSigint)(int);
	void (*m_prevSigterm)(int);
	void (*m_prevSigquit)(int);
	void (*m_prevSigpipe)(int);

	int                                 m_selfPipeRead;

	std::vector<IHandler*>              m_handlers;
	std::vector<struct pollfd>          m_pfds;   // index 0 = self-pipe, rest = handlers[i-1]
	std::map<IHandler*, std::size_t>    m_indexOf;

	std::map<IHandler*, MonoTime>       m_deadlines;
	std::vector<TimerEntry>             m_timerHeap;
	unsigned long                       m_timerSeq;

	bool                                m_dispatching;
	std::set<IHandler*>                 m_pendingRemove;

	bool                                m_shuttingDown;
	bool                                m_graceful;
	bool                                m_stopRequested;
};

} // namespace webserv

#endif
