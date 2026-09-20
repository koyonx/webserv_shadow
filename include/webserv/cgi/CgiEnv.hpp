#ifndef WEBSERV_CGI_ENV_HPP
#define WEBSERV_CGI_ENV_HPP

#include "webserv/config/Config.hpp"
#include "webserv/http/Request.hpp"

#include <string>
#include <vector>

namespace webserv {
namespace cgi {

// Build the CGI environment for a request per RFC 3875.
//
// Inputs:
//   req         : parsed request
//   origin      : the listener that accepted the connection (for
//                 SERVER_NAME / SERVER_PORT)
//   server      : the ServerConfig matched by the router (for
//                 SERVER_NAME fallback + document root)
//   scriptPath  : filesystem path of the CGI script to execute
//                 (SCRIPT_FILENAME + PATH_TRANSLATED base)
//   scriptUri   : URI-side path of the script (SCRIPT_NAME);
//                 for /cgi/foo.php this is "/cgi/foo.php"
//   pathInfo    : extra path after the script in the URI (may be
//                 empty); PATH_INFO gets it verbatim, PATH_TRANSLATED
//                 gets root + pathInfo
//
// Returns a vector of "KEY=VALUE" strings ready to feed execve after
// building a char* array.
std::vector<std::string> buildEnv(
	const webserv::http::Request         &req,
	const webserv::config::Listen        &origin,
	const webserv::config::ServerConfig  &server,
	const std::string                    &scriptPath,
	const std::string                    &scriptUri,
	const std::string                    &pathInfo);

// Convert a "Some-Header" name into the "HTTP_SOME_HEADER" form CGI
// wants: uppercase, '-' -> '_', prefix "HTTP_".
std::string headerNameToCgi(const std::string &name);

} // namespace cgi
} // namespace webserv

#endif
