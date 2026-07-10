# Running the 42 testers against webserv

Both testers under `./testers/` are interactive Go binaries — they print instructions and ask you to `press enter` between phases. Automated runs need input piped in.

## Setup once

The tester expects a directory `YoupiBanane` in the repo root:

```
YoupiBanane/
├── youpi.bad_extension
├── youpi.bla
├── nop/
│   ├── youpi.bad_extension
│   └── other.pouic
└── Yeah/
    └── not_happy.bad_extension
```

Provided as tracked empty files under `YoupiBanane/`.

The tester also assumes:

- `/` → GET only, must return 200 → served by `index.html` at repo root.
- `/*.bla` → POST via `./testers/cgi_tester`. Configured through `cgi_pass .bla ./testers/cgi_tester;`.
- `/post_body` → POST with `client_max_body_size 100`.
- `/directory/` → GET, root `YoupiBanane`, `index youpi.bad_extension`.

## Run

```bash
# Terminal 1:
./webserv --serve examples/tester.conf

# Terminal 2:
./testers/tester      http://localhost:8080
./testers/cgi_tester  http://localhost:8080
```

At each `press enter to continue` prompt, hit Enter.

## Notes

- Both testers connect to whatever host:port you pass. `examples/tester.conf` binds `127.0.0.1:8080`.
- Successful runs print "OK" per test; failures print `FATAL ERROR ON LAST TEST: <reason>`.
- If a test hangs, the tester eventually times out and moves on. Ctrl-C to abort.
