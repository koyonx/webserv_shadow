#include "webserv/handler/Dispatch.hpp"

#include "webserv/StringUtil.hpp"
#include "webserv/handler/DeleteHandler.hpp"
#include "webserv/handler/ErrorPage.hpp"
#include "webserv/handler/MethodPolicy.hpp"
#include "webserv/handler/PostHandler.hpp"
#include "webserv/handler/StaticHandler.hpp"

namespace webserv {
namespace handler {

void dispatch(const webserv::http::Request &req,
              const webserv::RouteMatch    &match,
              webserv::http::Response      &response)
{
	response.setKeepAlive(req.keepAlive);

	// Router-side failure surfaces first (e.g. 400 traversal, 404 no
	// server). error_page still applies via emitError.
	if (match.errorStatus != 0 || match.server == NULL) {
		emitError(match.errorStatus ? match.errorStatus : 500,
		          &match, response);
		return;
	}

	// Method policy split into two stages:
	//   1. methodIsImplemented — GET/HEAD/POST/DELETE. Anything else
	//      (PUT, PATCH, TRACE, OPTIONS, FROB, ...) is "not implemented"
	//      by webserv per RFC 7231 §6.6.2 -> 501.
	//   2. methodIsAllowed — the location's allow list. A known method
	//      that isn't permitted here gets 405 with an Allow header.
	if (!methodIsImplemented(req.method)) {
		emitError(501, &match, response);
		return;
	}
	std::vector<std::string>        fallback;
	const std::vector<std::string> &allowed =
		effectiveAllowedMethods(match, fallback);
	if (!methodIsAllowed(req.method, allowed)) {
		response.setHeader("Allow", allowHeader(allowed));
		emitError(405, &match, response);
		return;
	}

	// Location-specific client_max_body_size enforcement. Parser cap
	// is the largest across the config (so legitimate uploads reach
	// this point); the per-location cap is stricter.
	std::size_t bodyMax = (match.location != NULL)
	                    ? match.location->maxBodySize
	                    : match.server->maxBodySize;
	if (bodyMax > 0 && req.body.size() > bodyMax) {
		emitError(413, &match, response);
		return;
	}

	// `return CODE [URL];` short-circuits everything else.
	if (match.location != NULL && match.location->hasReturn) {
		int code = match.location->ret.code;
		response.setStatus(code);
		response.setContentType("text/plain; charset=utf-8");
		if (!match.location->ret.url.empty()) {
			response.setHeader("Location", match.location->ret.url);
		}
		std::string body =
			strutil::toStr(static_cast<long>(code)) + " " +
			webserv::http::reasonPhrase(code) + "\n";
		response.setBody(body);
		return;
	}

	// Method-specific handlers. methodIsImplemented above guarantees
	// we only reach one of these arms — the final emitError(501) is
	// defensive dead-code (kept because a future method addition
	// would otherwise return an empty response).
	if (req.method == "GET" || req.method == "HEAD") {
		serveStatic(req, match, response);
		return;
	}
	if (req.method == "DELETE") {
		serveDelete(req, match, response);
		return;
	}
	if (req.method == "POST") {
		servePost(req, match, response);
		return;
	}
	emitError(501, &match, response);
}

} // namespace handler
} // namespace webserv
