#include "webserv/handler/Multipart.hpp"

#include "webserv/StringUtil.hpp"

#include <cstddef>

namespace webserv {
namespace handler {

std::string headerParam(const std::string &headerLine,
                        const std::string &key)
{
	std::string lkey = strutil::toLower(key) + "=";
	std::string llin = strutil::toLower(headerLine);

	std::string::size_type pos = 0;
	while ((pos = llin.find(lkey, pos)) != std::string::npos) {
		// Ensure "key=" is not embedded in another token.
		if (pos > 0) {
			char prev = llin[pos - 1];
			if (prev != ';' && prev != ' ' && prev != '\t') {
				pos += lkey.size();
				continue;
			}
		}
		std::string::size_type vstart = pos + lkey.size();
		if (vstart >= headerLine.size()) return "";
		if (headerLine[vstart] == '"') {
			// Quoted value with \-escapes.
			std::string val;
			std::size_t i = vstart + 1;
			while (i < headerLine.size() && headerLine[i] != '"') {
				if (headerLine[i] == '\\' && i + 1 < headerLine.size()) {
					val += headerLine[i + 1];
					i   += 2;
				} else {
					val += headerLine[i];
					i   += 1;
				}
			}
			return val;
		}
		std::string::size_type end = headerLine.find_first_of("; \t", vstart);
		if (end == std::string::npos) end = headerLine.size();
		return headerLine.substr(vstart, end - vstart);
	}
	return "";
}

std::string extractBoundary(const std::string &contentType)
{
	return headerParam(contentType, "boundary");
}

std::string sanitizeUploadName(const std::string &raw)
{
	if (raw.empty()) return "";
	std::string::size_type slash = raw.find_last_of("/\\");
	std::string base = (slash == std::string::npos) ? raw : raw.substr(slash + 1);
	if (base.empty() || base == "." || base == "..") return "";
	for (std::size_t i = 0; i < base.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(base[i]);
		if (c < 0x20 || c == 0x7F) return "";
	}
	return base;
}

namespace {

// Split header block ("Name: value\r\nName2: value2\r\n") into
// (name, value) pairs. Returns false only on completely malformed
// input; empty lines are tolerated.
bool splitPartHeaders(const std::string &block,
                      std::vector<std::pair<std::string, std::string> > &out)
{
	std::size_t pos = 0;
	while (pos < block.size()) {
		std::size_t nl = block.find('\n', pos);
		std::string line = (nl == std::string::npos)
		                 ? block.substr(pos)
		                 : block.substr(pos, nl - pos);
		pos = (nl == std::string::npos) ? block.size() : nl + 1;
		if (!line.empty() && line[line.size() - 1] == '\r') {
			line.erase(line.size() - 1);
		}
		if (line.empty()) continue;
		std::string::size_type colon = line.find(':');
		if (colon == std::string::npos) return false;
		std::string name  = strutil::trim(line.substr(0, colon));
		std::string value = strutil::trim(line.substr(colon + 1));
		if (name.empty()) return false;
		out.push_back(std::make_pair(name, value));
	}
	return true;
}

const std::string *findHeader(
	const std::vector<std::pair<std::string, std::string> > &hs,
	const std::string                                       &name)
{
	for (std::size_t i = 0; i < hs.size(); ++i) {
		if (strutil::iequals(hs[i].first, name)) return &hs[i].second;
	}
	return NULL;
}

} // anonymous

bool parseMultipart(const std::string          &body,
                    const std::string          &boundary,
                    std::vector<MultipartPart> &parts)
{
	if (boundary.empty()) return false;

	const std::string delim = "--" + boundary;
	std::string::size_type pos = body.find(delim);
	if (pos == std::string::npos) return false;

	while (pos < body.size()) {
		pos += delim.size();
		if (pos > body.size()) return false;

		// Close-delim "--" terminates the multipart body.
		if (pos + 2 <= body.size()
		 && body.compare(pos, 2, "--") == 0) {
			return true;
		}
		// Every part-delim is followed by CRLF (or a bare LF for
		// tolerance).
		if (pos + 2 <= body.size()
		 && body.compare(pos, 2, "\r\n") == 0) {
			pos += 2;
		} else if (pos + 1 <= body.size() && body[pos] == '\n') {
			pos += 1;
		} else {
			return false;
		}

		std::string::size_type hdrEnd = body.find("\r\n\r\n", pos);
		std::string::size_type hdrEndAlt = body.find("\n\n", pos);
		if (hdrEndAlt != std::string::npos
		 && (hdrEnd == std::string::npos || hdrEndAlt < hdrEnd)) {
			hdrEnd = hdrEndAlt;
			if (hdrEnd == std::string::npos) return false;
			// hdrEnd points at "\n\n"; body starts hdrEnd + 2
			std::string headerBlock = body.substr(pos, hdrEnd - pos);
			pos = hdrEnd + 2;

			std::vector<std::pair<std::string, std::string> > partHdrs;
			if (!splitPartHeaders(headerBlock, partHdrs)) return false;

			const std::string closeDelim = "\n--" + boundary;
			std::string::size_type endPos = body.find(closeDelim, pos);
			if (endPos == std::string::npos) return false;

			MultipartPart part;
			part.body = body.substr(pos, endPos - pos);
			// Strip a possible trailing '\r' left over from CRLF.
			if (!part.body.empty()
			 && part.body[part.body.size() - 1] == '\r') {
				part.body.erase(part.body.size() - 1);
			}

			const std::string *cd = findHeader(partHdrs, "Content-Disposition");
			if (cd != NULL) {
				part.name     = headerParam(*cd, "name");
				part.filename = headerParam(*cd, "filename");
			}
			const std::string *ct = findHeader(partHdrs, "Content-Type");
			if (ct != NULL) {
				part.contentType = *ct;
			}
			parts.push_back(part);
			pos = endPos + 1;   // include the \n before --boundary
			continue;
		}
		if (hdrEnd == std::string::npos) return false;

		std::string headerBlock = body.substr(pos, hdrEnd - pos);
		pos = hdrEnd + 4;

		std::vector<std::pair<std::string, std::string> > partHdrs;
		if (!splitPartHeaders(headerBlock, partHdrs)) return false;

		const std::string closeDelim = "\r\n--" + boundary;
		std::string::size_type endPos = body.find(closeDelim, pos);
		if (endPos == std::string::npos) return false;

		MultipartPart part;
		part.body = body.substr(pos, endPos - pos);

		const std::string *cd = findHeader(partHdrs, "Content-Disposition");
		if (cd != NULL) {
			part.name     = headerParam(*cd, "name");
			part.filename = headerParam(*cd, "filename");
		}
		const std::string *ct = findHeader(partHdrs, "Content-Type");
		if (ct != NULL) {
			part.contentType = *ct;
		}
		parts.push_back(part);
		pos = endPos + 2;   // consume the CRLF before --boundary
	}
	// Reached EOF without a closing delim.
	return false;
}

} // namespace handler
} // namespace webserv
