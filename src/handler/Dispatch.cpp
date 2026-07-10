#include "webserv/handler/Dispatch.hpp"

#include "webserv/StringUtil.hpp"
#include "webserv/handler/DeleteHandler.hpp"
#include "webserv/handler/ErrorPage.hpp"
#include "webserv/handler/StaticHandler.hpp"

namespace webserv {
namespace handler {

namespace {

const std::vector<std::string> &effectiveAllowedMethods(
	const webserv::RouteMatch &match,
	std::vector<std::string>  &fallback)
{
	if (match.location != NULL && !match.location->allowedMethods.empty()) {
		return match.location->allowedMethods;
	}
	fallback.clear();
	fallback.push_back("GET");
	fallback.push_back("POST");
	fallback.push_back("DELETE");
	return fallback;
}

bool methodIsAllowed(const std::string              &method,
                    const std::vector<std::string> &allowed)
{
	for (std::size_t i = 0; i < allowed.size(); ++i) {
		if (allowed[i] == method)                  return true;
		if (method == "HEAD" && allowed[i] == "GET") return true;
	}
	return false;
}

std::string allowHeader(const std::vector<std::string> &allowed)
{
	std::string out;
	for (std::size_t i = 0; i < allowed.size(); ++i) {
		if (i > 0) out += ", ";
		out += allowed[i];
	}
	return out;
}

} // anonymous

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

	// Method policy: location wins, otherwise default {GET, POST, DELETE}.
	std::vector<std::string>        fallback;
	const std::vector<std::string> &allowed = effectiveAllowedMethods(match, fallback);
	if (!methodIsAllowed(req.method, allowed)) {
		response.setHeader("Allow", allowHeader(allowed));
		emitError(405, &match, response);
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

	// Method-specific handlers.
	if (req.method == "GET" || req.method == "HEAD") {
		serveStatic(req, match, response);
		return;
	}
	if (req.method == "DELETE") {
		serveDelete(req, match, response);
		return;
	}
	if (req.method == "POST") {
		// feat/19 lands the real POST handler; until then, 501.
		emitError(501, &match, response);
		return;
	}
	emitError(501, &match, response);
}

} // namespace handler
} // namespace webserv
