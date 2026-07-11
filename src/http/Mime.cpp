#include "webserv/http/Mime.hpp"

#include "webserv/StringUtil.hpp"

namespace webserv {
namespace http {

namespace {

struct MimeEntry {
	const char *ext;
	const char *mime;
};

// Ordered small table; O(N) lookup is fine for the range of extensions
// a webserv realistically sees. Text types carry an explicit charset so
// the tester sees UTF-8 for HTML/CSS/JS.
const MimeEntry kTable[] = {
	{ ".html", "text/html; charset=utf-8"        },
	{ ".htm",  "text/html; charset=utf-8"        },
	{ ".css",  "text/css; charset=utf-8"         },
	{ ".js",   "application/javascript; charset=utf-8" },
	{ ".json", "application/json; charset=utf-8" },
	{ ".xml",  "application/xml; charset=utf-8"  },
	{ ".txt",  "text/plain; charset=utf-8"       },
	{ ".md",   "text/plain; charset=utf-8"       },
	{ ".png",  "image/png"                       },
	{ ".jpg",  "image/jpeg"                      },
	{ ".jpeg", "image/jpeg"                      },
	{ ".gif",  "image/gif"                       },
	{ ".webp", "image/webp"                      },
	{ ".svg",  "image/svg+xml"                   },
	{ ".ico",  "image/x-icon"                    },
	{ ".pdf",  "application/pdf"                 },
	{ ".mp3",  "audio/mpeg"                      },
	{ ".mp4",  "video/mp4"                       },
	{ ".webm", "video/webm"                      },
	{ ".zip",  "application/zip"                 },
	{ ".gz",   "application/gzip"                },
	{ ".tar",  "application/x-tar"               },
	{ ".wasm", "application/wasm"                },
	// Fonts — added for I4 in the test report.
	{ ".woff", "font/woff"                       },
	{ ".woff2","font/woff2"                      },
	{ ".ttf",  "font/ttf"                        },
	{ ".otf",  "font/otf"                        },
	{ ".eot",  "application/vnd.ms-fontobject"   },
	// Extra image types testers sometimes probe.
	{ ".bmp",  "image/bmp"                       },
	{ ".tif",  "image/tiff"                      },
	{ ".tiff", "image/tiff"                      },
	{ ".avif", "image/avif"                      },
	// Multimedia extras.
	{ ".ogg",  "audio/ogg"                       },
	{ ".wav",  "audio/wav"                       },
	{ ".flac", "audio/flac"                      },
	{ ".webm", "video/webm"                      },
	{ ".mkv",  "video/x-matroska"                },
	// Text extras.
	{ ".csv",  "text/csv; charset=utf-8"         },
	{ ".yaml", "text/yaml; charset=utf-8"        },
	{ ".yml",  "text/yaml; charset=utf-8"        }
};
const std::size_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);

} // anonymous

std::string mimeForFilename(const std::string &filename)
{
	std::string::size_type dot = filename.rfind('.');
	if (dot == std::string::npos) {
		return "application/octet-stream";
	}
	std::string ext = strutil::toLower(filename.substr(dot));
	for (std::size_t i = 0; i < kTableSize; ++i) {
		if (ext == kTable[i].ext) return kTable[i].mime;
	}
	return "application/octet-stream";
}

} // namespace http
} // namespace webserv
