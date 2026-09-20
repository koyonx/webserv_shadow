#include "webserv/net/ConnectionSpawner.hpp"

#include "webserv/Log.hpp"
#include "webserv/core/PollLoop.hpp"

#include <unistd.h>

namespace webserv {

ConnectionSpawner::ConnectionSpawner(long          idleTimeoutMs,
                                     long          sweepIntervalMs,
                                     const Router *router)
	: m_idleMs(idleTimeoutMs),
	  m_sweepMs(sweepIntervalMs),
	  m_router(router),
	  m_armed(false),
	  m_live(),
	  m_dead()
{}

ConnectionSpawner::~ConnectionSpawner()
{
	for (std::size_t i = 0; i < m_dead.size(); ++i) {
		delete m_dead[i];
	}
	m_dead.clear();
	for (std::set<Connection *>::iterator it = m_live.begin();
	     it != m_live.end(); ++it) {
		delete *it;
	}
	m_live.clear();
}

void ConnectionSpawner::arm(PollLoop &loop)
{
	if (m_armed) {
		return;
	}
	m_armed = true;
	loop.add(this);
	loop.setDeadline(this, m_sweepMs);
}

void ConnectionSpawner::onAccept(int                            cfd,
                                 const webserv::config::Listen &origin,
                                 PollLoop                      &loop)
{
	Connection *c = new Connection(cfd, origin, *this, m_idleMs, m_router);
	loop.add(c);
	loop.setDeadline(c, m_idleMs);
	m_live.insert(c);
	LOG_INFO("ConnectionSpawner: new connection fd=" << cfd
	         << " (live=" << m_live.size() << ")");
}

void ConnectionSpawner::notifyDone(Connection *c)
{
	std::set<Connection *>::iterator it = m_live.find(c);
	if (it != m_live.end()) {
		m_live.erase(it);
	}
	m_dead.push_back(c);
}

void ConnectionSpawner::onTimeout(PollLoop &loop)
{
	if (!m_dead.empty()) {
		LOG_INFO("ConnectionSpawner: reaping " << m_dead.size()
		         << " finished connection(s); live=" << m_live.size());
		for (std::size_t i = 0; i < m_dead.size(); ++i) {
			delete m_dead[i];
		}
		m_dead.clear();
	}
	loop.setDeadline(this, m_sweepMs);
}

std::size_t ConnectionSpawner::liveCount()        const { return m_live.size(); }
std::size_t ConnectionSpawner::deadPendingCount() const { return m_dead.size(); }

} // namespace webserv
