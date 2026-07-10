#include "webserv/session/SessionManager.hpp"

#include "webserv/StringUtil.hpp"

#include <sstream>
#include <sys/types.h>
#include <unistd.h>

namespace webserv {
namespace session {

SessionManager::Session::Session()
	: id(), data(), createdAt(0), lastAccessAt(0) {}

SessionManager::SessionManager(long ttlSeconds)
	: m_ttlSec(ttlSeconds), m_counter(0), m_sessions() {}

std::string SessionManager::generateId()
{
	++m_counter;
	std::ostringstream oss;
	oss << static_cast<long>(std::time(NULL))
	    << "-" << static_cast<long>(::getpid())
	    << "-" << m_counter;
	return oss.str();
}

SessionManager::Session *SessionManager::find(const std::string &sid)
{
	std::map<std::string, Session>::iterator it = m_sessions.find(sid);
	if (it == m_sessions.end()) return NULL;
	std::time_t now = std::time(NULL);
	if (m_ttlSec > 0
	 && it->second.lastAccessAt + m_ttlSec < now) {
		m_sessions.erase(it);
		return NULL;
	}
	it->second.lastAccessAt = now;
	return &it->second;
}

const SessionManager::Session *SessionManager::find(const std::string &sid) const
{
	std::map<std::string, Session>::const_iterator it = m_sessions.find(sid);
	if (it == m_sessions.end()) return NULL;
	std::time_t now = std::time(NULL);
	if (m_ttlSec > 0
	 && it->second.lastAccessAt + m_ttlSec < now) {
		return NULL;
	}
	return &it->second;
}

SessionManager::Session &SessionManager::create()
{
	Session s;
	s.id           = generateId();
	s.createdAt    = std::time(NULL);
	s.lastAccessAt = s.createdAt;
	m_sessions[s.id] = s;
	return m_sessions[s.id];
}

void SessionManager::erase(const std::string &sid)
{
	m_sessions.erase(sid);
}

void SessionManager::sweep()
{
	if (m_ttlSec <= 0) return;
	std::time_t now = std::time(NULL);
	for (std::map<std::string, Session>::iterator it = m_sessions.begin();
	     it != m_sessions.end();) {
		if (it->second.lastAccessAt + m_ttlSec < now) {
			m_sessions.erase(it++);
		} else {
			++it;
		}
	}
}

long        SessionManager::ttl()  const { return m_ttlSec; }
std::size_t SessionManager::size() const { return m_sessions.size(); }

} // namespace session
} // namespace webserv
