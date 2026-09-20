#ifndef WEBSERV_SESSION_MANAGER_HPP
#define WEBSERV_SESSION_MANAGER_HPP

#include <cstddef>
#include <ctime>
#include <map>
#include <string>

namespace webserv {
namespace session {

// Simple in-memory session store. Session ids are opaque strings the
// server hands out via Set-Cookie; per-session data is a case-sensitive
// key-value map.
//
// This is deliberately not a security-hardened store: session ids are
// derived from time + pid + a monotonically-increasing counter, which
// is enough to be unpredictable within a single tester run but not
// cryptographically random.
class SessionManager {
public:
	struct Session {
		std::string                        id;
		std::map<std::string, std::string> data;
		std::time_t                        createdAt;
		std::time_t                        lastAccessAt;

		Session();
	};

	// ttlSeconds: entries idle for longer than this get swept.
	explicit SessionManager(long ttlSeconds = 3600);

	// Look up an existing session; NULL if not found or expired.
	Session       *find(const std::string &sid);
	const Session *find(const std::string &sid) const;

	// Allocate a fresh session and return it (also stored).
	Session       &create();

	// Explicitly drop a session (e.g. after logout).
	void erase(const std::string &sid);

	// Remove expired entries; called by sweep() but exposed for tests.
	void sweep();

	long        ttl()   const;
	std::size_t size()  const;

private:
	std::string generateId();

	long                              m_ttlSec;
	unsigned long                     m_counter;
	std::map<std::string, Session>    m_sessions;
};

} // namespace session
} // namespace webserv

#endif
