#ifndef WEBSERV_CONFIG_CONFIG_HPP
#define WEBSERV_CONFIG_CONFIG_HPP

#include <cstddef>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace webserv {
namespace config {

struct Listen {
	std::string host;   // "0.0.0.0" by default
	int         port;

	Listen();
	Listen(const std::string &h, int p);

	bool operator==(const Listen &o) const;
	bool operator<(const Listen &o)  const;
};

struct Return {
	int         code;   // 0 = no return
	std::string url;    // may be empty (e.g. `return 444;`)

	Return();
};

struct LocationConfig {
	std::string                        path;

	// Effective values after inheritance from server/http. All fields
	// are already resolved by the validator; consumers do not need to
	// climb parents at request time.
	std::vector<std::string>           allowedMethods;   // uppercased
	bool                               autoindex;
	std::string                        root;
	std::vector<std::string>           indexes;
	std::size_t                        maxBodySize;      // bytes
	std::map<int, std::string>         errorPages;
	std::string                        uploadStore;      // empty = no upload
	std::map<std::string, std::string> cgiPass;          // ".php" -> "/usr/bin/php-cgi"
	bool                               hasReturn;
	Return                             ret;

	std::vector<LocationConfig>        locations;        // nested

	LocationConfig();
};

struct ServerConfig {
	std::vector<Listen>                listens;          // non-empty after validation
	std::vector<std::string>           serverNames;      // lowercased
	bool                               autoindex;
	std::string                        root;
	std::vector<std::string>           indexes;
	std::size_t                        maxBodySize;
	std::map<int, std::string>         errorPages;
	int                                keepaliveTimeout; // seconds
	int                                clientBodyTimeout;
	int                                sendTimeout;

	std::vector<LocationConfig>        locations;

	ServerConfig();
};

struct Config {
	std::vector<ServerConfig> servers;   // non-empty after validation

	Config();
};

// Pretty-print for debugging.
void dumpConfig(const Config &cfg, std::ostream &os);

} // namespace config
} // namespace webserv

#endif
