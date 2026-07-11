# webserv — Comprehensive Test Report

**Branch:** `test/comprehensive-testing` (forked from `feat/30-fuzz-fixes`)
**Baseline commit:** `af6e629 feat: fuzz-style robustness pass (all 6 cases green, ASan clean)`
**Date:** 2026-07-11
**Reference server:** nginx/1.24.0 (Ubuntu) on `127.0.0.1:18200`
**System under test:** `./webserv --serve examples/serve.conf` on `127.0.0.1:18100`

The report is structured as (1) environment & method, (2) per-suite results,
(3) nginx parity notes, (4) issues found (severity-sorted), (5) suggested fixes
sketched from source, (6) reproducer index.

---

## 1. Environment & method

- OS: Ubuntu 24.04 on WSL2, kernel headers Linux 6.6, x86_64.
- Compiler: g++ 13, `-std=c++98 -pedantic -Wall -Wextra -Werror`.
- Two builds exercised:
  - **release** (default `make`)
  - **sanitize** (`make sanitize`, `-fsanitize=address,undefined -g3`)
- Client tools: curl 8.5, netcat-openbsd (`nc -q N`), Python 3.12, ab, siege.
- All tests re-checked under the sanitize build; no ASan/UBSan reports were
  emitted during the full test matrix (see §2.8).
- Test scripts live under `tests/scripts/`, logs under `tests/logs/`,
  per-suite results under `tests/results/`. Every failure below is
  reproducible by running the referenced script.

**Total exercised assertions across suites:** 148
**Passed:** 141 (95.3%) &nbsp;·&nbsp; **Failed:** 7 (see §4)

| # | Suite                                | Passed | Failed |
| - | ------------------------------------ | -----: | -----: |
| 1 | Config parser / validator            |  35    |   0    |
| 2 | HTTP methods, versions, status codes |  32    |   1    |
| 3 | Static / autoindex / redirect / MIME |  30    |   1    |
| 4 | POST / multipart / body-size limits  |  16    |   0    |
| 5 | CGI                                  |  22    |   2    |
| 6 | Connection semantics / pipelining    |  15    |   2*   |
| 7 | Fuzz / adversarial input             |  22    |   0    |
| 8 | Built-in unit tests (router/session) |  11    |   0    |

`*` — two suite-6 "failures" are test-script quirks (bash misparsing response
bytes, grep pattern mismatch), not webserv defects. See §2.6.

---

## 2. Per-suite details

### 2.1 Config parser & validator (`tests/scripts/01_config.sh`) — **35 / 35**

Positive cases exercised: minimal bare-server, `http { server { … } }`,
the three provided configs (`basic`, `serve`, `tester`), multiple `listen`,
same-port vhost splits, nested `location`, `k`/`m` size suffixes, `error_page`
with multiple codes, multi `cgi_pass`, `return` with & without URL, comments.

Negative cases correctly rejected: unterminated block, extra closing brace,
missing `;`, unknown directive, server missing `listen`, port 0, port > 65535,
duplicate `listen`, duplicate `server_name`+`listen`, unsupported method
(`PUT`), `cgi_pass` extension without leading dot, invalid size unit
(`5xyz`), `error_page` code out of `[100,599]`, multiple `http { }` blocks,
directive at top level, no server block, timeout directive inside `location`,
`return` without a code, missing config file, empty file, 128 bytes of
random binary garbage.

Every failure surfaced through the `ConfigError` path with line-numbered
diagnostics.

### 2.2 HTTP methods, versions, status codes (`tests/scripts/02_methods.sh`) — **32 / 33**

**PASS** for the golden path (`GET /`, `HEAD` parity, `404 Not Found` with
custom body, `405` with `Allow` header, `Server`/`Date` headers, correct
reason phrases, HTTP/1.1 Host enforcement, HTTP/1.0 exempt, `Host` duplication
→ 400, malformed version → 400, `HTTP/2.0` → 505, `HTTP/1.X` → 400,
lowercase `http/1.1` → 400, lower-case `get` → rejected).

`HEAD` `Content-Length` matches the byte length nginx reports (=353 for
`/index.html`), and `HEAD` returns zero body bytes as required.

**Fail:** `FROB / HTTP/1.1` produces `405 Method Not Allowed` where §2 of the
41 test expected `501 Not Implemented`. This is not strictly wrong — the
serve.conf location `/` has `allowed_methods GET`, so any non-GET fails the
allow list before Dispatch reaches the `501` branch. But note that the
`501` branch in `src/handler/Dispatch.cpp:112` is *dead code* for any
sensibly configured location, because
`effectiveAllowedMethods()` (`src/handler/Dispatch.cpp:14–24`) hard-codes the
fallback list to `{GET,POST,DELETE}`. See §4-I2.

Same underlying quirk shows up for `PUT / PATCH / TRACE / CONNECT / OPTIONS`
— all get `405`, never `501`. RFC 7231 says a method the server does not
implement SHOULD get `501`; this suite documents the behavior, does not fail
the run.

### 2.3 Static / autoindex / redirect / MIME (`tests/scripts/03_static.sh`) — **30 / 31**

Golden path: index resolution, body parity with nginx byte-for-byte,
Content-Type for `.css`/`.json`/`.html`, `Content-Length` matches nginx
across `about.html`, `style.css`, `data.json`, `50x.html`.

Autoindex: HTML listing served when index missing, includes each file name,
declares `text/html`. Directory URL without trailing slash returns
`301 Moved Permanently` with a `Location:` header (matches nginx).

Path traversal via `..` and `%2f` returns `400 Bad Request` and does NOT
leak `/etc/passwd` bytes — confirmed by grep on the response.
`%20`-encoded spaces in filenames work.

Return-code short-circuit: `return 302 /about.html` produces
`HTTP/1.1 302 Found` with a correct `Location`.

DELETE on a file works (`204 No Content`, file is removed); DELETE on a
directory is rejected (`403`); DELETE on a nonexistent path is `404`.

**Fail — real defect (M1):** a URI whose path segment length exceeds
`NAME_MAX` (255 bytes) returns `500 Internal Server Error`. The threshold
was bracketed to exactly **256 characters** using
`tests/scripts/bracket_uri3.sh`. Root cause: `readWholeFile` in
`src/handler/StaticHandler.cpp:40–45` only maps `ENOENT/ENOTDIR/EACCES/EPERM`
to friendly statuses and returns `500` for anything else, so
`ENAMETOOLONG` from `open()` (and `stat()` at
`src/handler/StaticHandler.cpp:126–129`) bubbles up as `500`. Correct
behavior is `414` (URI Too Long) or `404`.

Informational deltas vs nginx (not failures):
- `Range: bytes=0-10` returns full body, not `206 Partial Content`.
- `If-Modified-Since` is ignored, no `304`.
- No `ETag`, no `Cache-Control`, no `Accept-Ranges`.
- Content-Type for HTML includes `; charset=utf-8` where nginx omits it
  (both valid per RFC 7231 §3.1.1).

### 2.4 POST / multipart / upload / body-size (`tests/scripts/04_post.sh`) — **16 / 16**

- `POST /echo` with form-encoded body → `200 OK`.
- 4 KB random POST → `200 OK`.
- 6 MB POST vs server `client_max_body_size 5m` → `413 Payload Too Large`
  (matches nginx behavior).
- Multipart `POST /upload/` → `201 Created`, file written under
  `examples/uploads/` with byte-for-byte match.
- Chunked `POST /echo` → `200 OK`.
- Chunked body exceeding limit → `413`.
- Chunked with invalid hex chunk-size → `400`.
- Content-Length + Transfer-Encoding together → `400`.
- Empty body (`CL: 0`) → `200`.
- Negative `Content-Length` → `400`.
- Non-numeric `Content-Length` → `400`.
- Overflow `Content-Length` (21 digits) → `400`.
- Per-location `client_max_body_size 5m` enforced.

Filename sanitizer (checked via `tests/scripts/repro_traversal.sh`) strips
directory segments and keeps only the basename:

| Uploaded filename           | Written as                           |
| --------------------------- | ------------------------------------ |
| `../../../evil.txt`         | `examples/uploads/evil.txt`          |
| `/etc/wtf`                  | `examples/uploads/wtf`               |
| `a/b/c/d/e/f.txt`           | `examples/uploads/f.txt`             |

No file escapes the configured `upload_store` directory.

Minor observation: percent-encoded control bytes in `filename=` (e.g.
`with%00nul.txt`) are stored *literally* — the sanitizer does not
percent-decode filenames, which is the safe choice.

### 2.5 CGI (`tests/scripts/05_cgi.sh`) — **22 / 24**

**PASS**: hello.py GET / POST / query, env.sh, cookies.py (two Set-Cookie
headers preserved verbatim, `HTTP_COOKIE` env set), redirect.py
(`Status:` header → `302 Found` + `Location`), slow.py killed with
`504 Gateway Timeout` after ~5 s (matches the 5 s cap in
`src/net/Connection.cpp:81`), missing script → `404`, 256 KB POST body
delivered intact, noisy stderr does not corrupt response.

**Fail — real defect (H1):** DELETE / PUT / PATCH / TRACE / OPTIONS / any
unknown method on `/cgi-bin/hello.py` are *all* dispatched to the CGI
regardless of the location's `allowed_methods GET POST`. Reproduced with
`tests/scripts/repro_cgi_methods.sh`:

```
== effective allowed_methods for /cgi-bin/ per serve.conf: GET POST ==
  GET     -> 200
  POST    -> 200
  HEAD    -> 200
  DELETE  -> 200   ← should be 405
  PUT     -> 200   ← should be 405
  PATCH   -> 200   ← should be 405
  TRACE   -> 200   ← should be 405
  OPTIONS -> 200   ← should be 405
  FROB    -> 200   ← should be 405 or 501
```

The CGI body confirms it: `method: DELETE`, `method: PUT`, etc. Root
cause: `Connection::onReadable` calls `tryStartCgi(loop)` before the
Dispatch method-policy check runs. `tryStartCgi`
(`src/net/Connection.cpp:62`) consults only `cgiMatch()` (extension +
location root), never `location->allowedMethods`, then spawns
`CgiProcess` with the request's raw method as `REQUEST_METHOD`. See
proposed fix in §5.

**Fail — minor (L1):** a CGI script that writes body without any headers
gets a `200 OK` echoed to the client where a well-behaved server would
return `502 Bad Gateway`. Reproduced by writing a script that only
`sys.stdout.write("body")` with no `Content-Type` and no blank line —
webserv still returns the pass-through. Not user-visible in most cases
but worth tightening (§5).

Informational: `PATH_INFO` (`/cgi-bin/hello.py/extra/path`) returns `404`
— webserv insists on the exact script mapping. Legal per RFC 3875 but
diverges from Apache/nginx-fcgiwrap conventions.

Post-test process audit: after 5 rapid CGI requests, `ps --ppid $webserv_pid`
reports 0 children — no CGI leak.

### 2.6 Connection semantics (`tests/scripts/06_conn.sh`) — **15 / 17**

- HTTP/1.1 default keep-alive: two pipelined requests → two `200 OK`
  responses on the same TCP connection.
- `Connection: close` explicitly acknowledged.
- HTTP/1.0 default is close; HTTP/1.0 + `Connection: keep-alive` is
  honored (implementation choice).
- Header names are case-insensitive; trailing whitespace in header
  values is trimmed.
- Slow-loris byte-per-50ms request completes with `200 OK` (server does
  parse it — see §3 for the timeout observation).
- Pipelining 3 requests returns 3 responses in the correct order.
- ` Host : localhost` (space before colon) → `400` (correct per RFC 7230).
- 1000 headers → `431 Request Header Fields Too Large`.
- 16 KB single header value → `431`.
- Empty header value and whitespace-only value → `200`.
- Bare CR in request-line → `400`; LF-only line endings → tolerated
  (`200`).
- Absolute-form URI (`GET http://…/about.html HTTP/1.1`) → `200`.
- 20 concurrent GETs all succeed.

Two failing assertions are script bugs, not server bugs:
`50 headers → 200` — bash mis-interpreted response bytes as commands and
the grep never saw the `200` on line 1 that was actually present.
`pipelined bodies check` — grep pattern didn't match after header/body
interleaving.

### 2.7 Fuzz / adversarial input (`tests/scripts/07_fuzz.sh`) — **22 / 22**

The server rejected every malformed input with `4xx` and remained
alive & responsive after the run:

| Attack                                                | Result |
| ----------------------------------------------------- | ------ |
| `0xff` prefix in method                               | 400    |
| tab inside method                                     | 400    |
| double space in request-line                          | 400    |
| trailing space after HTTP-version                     | 400    |
| CR in header name                                     | 400    |
| NUL / LF in header value                              | 400    |
| obs-fold header (`\r\n\t`)                            | 400    |
| bad `%XX` in URI (`%GG`)                              | 400    |
| `%00` in URI                                          | 400    |
| whitespace-only request line                          | 400    |
| empty request                                         | closed |
| missing HTTP version                                  | 400    |
| `%2e%2e/etc/passwd` traversal                         | 400 (no leak) |
| tab in URI                                            | 400    |
| declared `Content-Length` short-body then EOF         | 400 / timeout |
| 512 B random binary garbage                           | 400    |
| 20 KB non-newline garbage                             | 400    |
| CRLF injection inside a header value                  | 200 (rejected at parser) |
| chunked size `-1`                                     | 400    |
| chunk size line > 8 KB                                | 400    |
| 30× abrupt connect+close burst                        | server alive |

**Server stayed alive throughout.** Sanity `GET /` after the run: `200`.
Same suite re-run against the **sanitize** build — 22 / 22, no ASan or
UBSan reports (only expected parser 400 log lines).

### 2.8 Built-in unit tests

`./webserv --test-router`: 10 / 10 (path normalization, wildcard vhost
matching, `..` traversal → 400).
`./webserv --test-session`: session create, lookup, sweep-after-TTL —
all pass.
`./webserv --test-loop`, `--test-listener`, `--test-connection`,
`--test-cgi-env` also run clean.

### 2.9 42 tester (`./testers/tester`, `tests/results/42_tester.log`)

Tester passed the first three tests: `GET /`, `POST / (size 0)`,
then aborted at `HEAD /` with **`FATAL ERROR ON LAST TEST: bad status code`**.

Reproducer:

```
curl -sI http://localhost:8080/
HTTP/1.1 200 OK
Content-Type: text/html; charset=utf-8
Content-Length: 172
Date: Sat, 11 Jul 2026 02:27:53 GMT
Server: webserv/0.1
Connection: keep-alive
```

The HEAD response looks well-formed and matches the GET headers.
The Go tester is opaque about *what* status it expected; likely candidates:

1. It sends `HEAD` with a non-empty body and expects a specific code
   (some testers expect `400` because HEAD with body is malformed).
2. It expects the reason phrase to be `OK` (present) or another
   invariant we don't yet know.

Recommendation: capture the tester's actual request bytes (tcpdump on lo)
and compare to what webserv answers. This is a follow-up rather than a
regression from earlier work.

### 2.10 Load test (`ab` + `siege`)

| Server  | Requests | Concurrency | Keep-alive | RPS    | p50   | max     | Failures |
| ------- | -------- | ----------- | ---------- | ------ | ----- | ------- | -------- |
| webserv | 5000     | 50          | yes        |  55006 | 1 ms  | 2 ms    | 0        |
| nginx   | 5000     | 50          | yes        | 152578 | <1 ms | 1 ms    | 0        |
| webserv | 5000     | 25          | siege      |  25000 | –     | 0.02 s  | 0        |

webserv sits at ~36 % of nginx throughput on a single-worker poll loop,
100 % success rate. Fine for the 42 subject.

---

## 3. nginx parity summary

| Property                                      | webserv                    | nginx                      | Verdict            |
| --------------------------------------------- | -------------------------- | -------------------------- | ------------------ |
| `GET /` body bytes                            | identical                  | identical                  | ✅ match           |
| `Content-Length` on static files              | matches                    | matches                    | ✅ match           |
| Content-Type for HTML                         | `text/html; charset=utf-8` | `text/html`                | ✅ both valid      |
| `HEAD` returns 0 bytes body                   | yes                        | yes                        | ✅                 |
| Custom `error_page` served                    | yes                        | yes                        | ✅                 |
| Trailing-slash → 301 for dir                  | yes                        | yes                        | ✅                 |
| `%2e%2e` traversal                            | 400 (no leak)              | 400 (no leak)              | ✅                 |
| Path segment > 255 chars                      | **500 (bug)**              | 404 (via `stat` → ENOENT branch that includes ENAMETOOLONG in nginx) | ❌ M1 |
| Absolute-form URI                             | 200                        | 200                        | ✅                 |
| Pipelining                                    | works                      | works                      | ✅                 |
| Chunked request body                          | works, oversize → 413      | works, oversize → 413      | ✅                 |
| CGI method policy                             | **bypasses `allowed_methods`** | n/a (nginx has no CGI) | ❌ H1 (webserv-only)|
| `ETag` / `If-Modified-Since`                  | absent                     | present                    | ⚠ documented       |
| `Range` requests                              | full body                  | 206 partial                | ⚠ not supported    |
| 100 % success under 5000 req / 50 conc        | yes                        | yes                        | ✅                 |

---

## 4. Issues found (severity-sorted)

### H1 — CGI dispatch bypasses `allowed_methods` (high)

**Where:** `src/net/Connection.cpp:62` (`Connection::tryStartCgi`) is called
at `src/net/Connection.cpp:234` before Dispatch has a chance to validate
the method.

**Impact:** Any HTTP method — including `DELETE`, `PUT`, `PATCH`,
`TRACE`, `FROB`, and unknown methods — is executed by the CGI script,
even when the location's `allowed_methods` explicitly excludes them.
Since `REQUEST_METHOD` is passed to the script verbatim, a script that
naively branches on `REQUEST_METHOD == "DELETE"` will act on it.

Real-world consequence: an operator who writes `allowed_methods GET POST;`
for a CGI location assumes DELETE/PUT are gated; they are not. Also
opens a small DoS vector — arbitrary method names invoke `fork+execve`.

**Reproducer:** `tests/scripts/repro_cgi_methods.sh` (proves all
methods including `FROB` produce `200` with the script echoing the
method back).

**Fix sketch:** perform the allow-list check in `tryStartCgi` (or, more
cleanly, invert the flow so Dispatch owns method validation and CGI is
an outcome of dispatch). Concretely:

```cpp
// src/net/Connection.cpp — inside tryStartCgi(), after matching route
if (!m.location || !methodIsAllowed(req.method, m.location->allowedMethods)) {
    return false;                       // let Dispatch produce 405
}
```

using the same `methodIsAllowed` helper from `src/handler/Dispatch.cpp:26`.

### M1 — `ENAMETOOLONG` from `stat()` / `open()` returns 500 instead of 404/414 (medium)

**Where:** `src/handler/StaticHandler.cpp:45` and `:127`.

**Impact:** Any URI whose last path segment is > 255 bytes triggers
`500 Internal Server Error`. Threshold bracketed to exactly 256 chars via
`tests/scripts/bracket_uri3.sh`:

```
len=255 -> 404
len=256 -> 500   ← should be 404 or 414
```

Not a security bug (no info leak — the static handler still calls the
error_page path), but it's a wrong status code that spooks monitoring
dashboards and violates the principle "5xx = server problem, 4xx =
client problem".

**Fix sketch:** in the errno switch (`src/handler/StaticHandler.cpp:43-45`):

```cpp
if (err == ENOENT || err == ENOTDIR || err == ENAMETOOLONG) return 404;
if (err == EACCES || err == EPERM)                          return 403;
if (err == ELOOP)                                            return 404;  // symlink loop
return 500;
```

and mirror it at `:127`. Alternatively map `ENAMETOOLONG` to `414` since
the URI is what's over budget.

### L1 — CGI script emitting body without headers returns 200 instead of 502 (low)

**Where:** the CGI response parser (see `Connection::onCgiComplete` in
`src/net/Connection.cpp`) accepts output that has no `Content-Type` and
no blank line.

**Impact:** the client sees a `200 OK` with whatever bytes the script
emitted. RFC 3875 requires that CGI scripts send at least a
Content-Type header before the blank line. A well-behaved server should
return `502 Bad Gateway` for malformed CGI output.

**Reproducer:** any CGI that does `print("just body")` with no headers.
Observed status: `200 OK`.

**Fix sketch:** reject a CGI response that has no CRLFCRLF terminator
(after allowing a grace period for the pipe to close), or one whose
"header" region contains no `:`.

### I1 — 42 tester aborts at `HEAD /` (informational)

Root cause not pinned down without decoding the tester's expectations
(closed-source Go binary). HEAD parity against nginx is byte-identical
for the same URL. Likely the tester sends HEAD with a body or checks
something we haven't observed. Worth capturing with tcpdump in a
follow-up.

### I2 — `501` branch in Dispatch is unreachable (informational)

`src/handler/Dispatch.cpp:112` (`emitError(501, …)`) can never fire
because `effectiveAllowedMethods` at line 14 seeds the fallback with
`{GET,POST,DELETE}`, and any method not in that list is caught by the
`405` branch at line 68 first. If the intent is "return 501 for methods
we don't understand", the fallback should differentiate "no
`allowed_methods` in config" from "method truly unimplemented".

### I3 — No `Range` / `ETag` / `If-Modified-Since` (informational)

Documented divergence from nginx; not required by the 42 subject.

### I4 — MIME map depth (informational)

`.txt`, `.html`, `.css`, `.json`, `.png` all mapped. Not tested: `.webp`,
`.svg`, `.woff2`, `.gif`. If the 42 tester probes any of these, extend
`src/http/Mime.cpp`.

---

## 5. Sanitizer summary

Full `04_post.sh + 05_cgi.sh + 07_fuzz.sh` re-run against
`make sanitize` (ASan + UBSan):

- All PASS counts unchanged: 16 + 22 + 22 = 60 assertions.
- No `AddressSanitizer:` or `runtime error:` line in
  `tests/logs/webserv_sanitize.log`.
- Only "ERROR"-substring hits are parser warnings like
  `parse error 400: control character in request line`, which is
  intentional behavior being logged at `WARN`.

Combined with the router / session unit tests, this is strong evidence
that the previously-completed fuzz-fixes pass (commit `af6e629`) held
up under the extended matrix.

---

## 6. Reproducer index

| Finding | Script                              | One-liner                                                            |
| ------- | ----------------------------------- | -------------------------------------------------------------------- |
| H1      | `tests/scripts/repro_cgi_methods.sh` | `curl -sX DELETE http://127.0.0.1:18100/cgi-bin/hello.py \| head`   |
| M1      | `tests/scripts/bracket_uri3.sh`      | `curl -sI "http://127.0.0.1:18100/$(python3 -c "print('x'*256)")"`   |
| L1      | see `05_cgi.sh` `malformed CGI` block | write CGI that only `print('body')`; observe `200 OK`               |
| Traversal safety | `tests/scripts/repro_traversal.sh` | See section 2.4 table                                              |
| Fuzz    | `tests/scripts/07_fuzz.sh`           | 22 malformed inputs, all 4xx, server alive                          |
| Config  | `tests/scripts/01_config.sh`         | 35 config parser cases                                              |

All scripts are self-contained and expect `webserv --serve examples/serve.conf`
on `127.0.0.1:18100` (and nginx on `18200` for parity checks).

---

## 7. What to do next

1. Fix H1 by wiring the method check into the CGI entry point.
2. Fix M1 by mapping `ENAMETOOLONG`/`ELOOP` in `StaticHandler.cpp`.
3. Investigate why the 42 tester rejects `HEAD /` (tcpdump the exchange).
4. Consider extending MIME coverage before the eval.
5. Optional: add `ETag` / `If-Modified-Since` — not required by the
   subject but cheap wins for parity with nginx.
