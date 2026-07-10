#ifndef WEBSERV_CORE_IHANDLER_HPP
#define WEBSERV_CORE_IHANDLER_HPP

namespace webserv {

class PollLoop;

// Every fd registered with PollLoop is owned by an IHandler. The
// handler decides what events it wants (POLLIN / POLLOUT / both) and
// reacts to them. All callbacks receive the loop so they can register
// or unregister other handlers (deferred to the end of the tick when
// dispatch is in flight).
//
// POLLHUP / POLLERR / POLLNVAL are folded into onReadable so a single
// call handles "peer closed" via read() returning 0. onWritable is
// called only when POLLOUT is signaled AND the handler asked for it.
class IHandler {
public:
	virtual ~IHandler() {}

	virtual int   fd() const                 = 0;
	virtual short wantEvents() const         = 0;

	virtual void  onReadable(PollLoop &loop) = 0;
	virtual void  onWritable(PollLoop &loop) = 0;
	virtual void  onTimeout(PollLoop &loop)  = 0;
};

} // namespace webserv

#endif
