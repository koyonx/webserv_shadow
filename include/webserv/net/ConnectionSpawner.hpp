#ifndef WEBSERV_NET_CONNECTION_SPAWNER_HPP
#define WEBSERV_NET_CONNECTION_SPAWNER_HPP

#include "webserv/core/IHandler.hpp"
#include "webserv/net/Connection.hpp"
#include "webserv/net/Listener.hpp"

#include <set>
#include <vector>

namespace webserv {

class Router;

// Combines: IAcceptSink (creates Connections for each accepted fd)
//         + IConnectionOwner (holds ownership of Connection objects)
//         + IHandler (periodic sweep to reap finished connections)
//
// Register with the PollLoop via arm(loop). Every m_sweepMs the
// spawner reaps any Connection whose notifyDone() was called and
// deletes it. Live connections are deleted on destruction.
//
// This is the piece that decides "the accept sink from feat/08 is
// no longer a stub, it is a real spawner." feat/10+ can replace the
// generateStubResponse() inside Connection without touching this
// coordinator.
class ConnectionSpawner
	: public IAcceptSink,
	  public IConnectionOwner,
	  public IHandler
{
public:
	ConnectionSpawner(long          idleTimeoutMs,
	                  long          sweepIntervalMs,
	                  const Router *router = NULL);
	virtual ~ConnectionSpawner();

	// Register the spawner as a timer-only handler and kick off the
	// periodic sweep. Idempotent; safe to call once.
	void arm(PollLoop &loop);

	// IAcceptSink
	virtual void onAccept(int                            cfd,
	                      const webserv::config::Listen &origin,
	                      PollLoop                      &loop);

	// IConnectionOwner
	virtual void notifyDone(Connection *c);

	// IHandler (timer-only)
	virtual int   fd()          const { return -1; }
	virtual short wantEvents()  const { return 0; }
	virtual void  onReadable(PollLoop &) {}
	virtual void  onWritable(PollLoop &) {}
	virtual void  onTimeout(PollLoop &loop);

	std::size_t liveCount() const;
	std::size_t deadPendingCount() const;

private:
	ConnectionSpawner(const ConnectionSpawner &);
	ConnectionSpawner &operator=(const ConnectionSpawner &);

	long                       m_idleMs;
	long                       m_sweepMs;
	const Router              *m_router;
	bool                       m_armed;
	std::set<Connection *>     m_live;
	std::vector<Connection *>  m_dead;
};

} // namespace webserv

#endif
