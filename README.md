# Microdlna

This is a fork of microdlna which is itself a fork a minidlna.

Microdlna is a stateless dlna server with no external dependencies with an emphasis on
low memory usage.

It has **no database**, which means no long, resource-consuming scan: changes made
on the server (new, moved or removed files) are exposed immediately. As a
trade-off, it also has no search capabilities, sorting, thumbnails, or format
conversion, but it can cope with the media directory not existing at start
time.

## Why this fork

This fork exists to run microdlna on my Raspberry Pi and enjoy my videos on my
TV over DLNA, and to fix blocking issues I ran into with the forked version.

It has been a success: it now runs reliably on my Raspberry Pi and works well.

## Modifications

Bug fixes, security and reliability:

- Network reliability at startup: retry with backoff while no network
  interface is detected yet, instead of failing immediately (useful when
  booting on a Raspberry Pi where the network isn't up yet).
- HTTP security: fixed the `Host` header check (anti DNS-rebinding), which
  could match on a prefix instead of comparing the exact value.
- HTTP security: explicit, bounds-checked (`PATH_MAX`) construction of the
  full served file path before `stat()`, and removal of a redundant double
  URL-unescape.
- Major fix for sending large files (`sendfile.c`): switched to `off_t`
  (instead of `size_t`) to correctly handle files larger than 2GB, always
  fall back to read/write on any `sendfile()` error, and guard against an
  infinite loop if `sendfile()` returns 0.
- Removed a problematic `setpriority()` call.
- `mime.c`: fixed an out-of-bounds read before the start of the filename
  for any name shorter than 6 characters with no extension.
- `getifaddr.c`: fixed an out-of-bounds read past the end of the string in
  `validate_uuid()` for any input shorter than a full UUID (`--uuid`).
- `dirlist.c`: fixed `get_directory_listing()` returning `NULL` (treated as
  an error, so no HTTP response was sent) for a valid but empty Browse
  result, e.g. a `StartingIndex` past the total item count.

Other topics:

- Build adjustments (debug/release modes).
- Reworked config file (`microdlna.conf`), notably for Raspberry Pi media
  paths.
- Added `install.sh`/`uninstall.sh` scripts and the systemd unit.
- Added icon choice: import a custom image and select it at build time
  (`make ICON=<name>`), see `docs/Icons.md`.
- Added a unit test suite (`make check`) and a CI workflow, see `## Tests`
  below.

### Test suite hardening (unit, functional, ThreadSanitizer)

Added a broad set of unit and functional tests to make deep refactoring
safe, and fixed every production bug they turned up:

- GENA eventing (`upnpevents.c`) was effectively unusable: the response to
  `SUBSCRIBE` never actually carried the `SID`/`Timeout` headers, so a
  client could never renew or unsubscribe, and subscribing to an unknown
  event URL returned `200` instead of an error.
- Out-of-bounds reads on event subscriber SIDs: a `memcmp` compared a fixed
  41 bytes against a client-supplied SID that could be shorter, and a
  `Callback` header with no path was read past its end.
- `minissdp.c`: M-SEARCH requests over ~199 bytes (common with a long
  `USER-AGENT`) were silently truncated and dropped instead of answered;
  the receive buffer was enlarged and the overflow check made reachable.
- `upnpsoap.c`: the listening port was truncated to 4 digits when building
  `<res>` URLs in Browse results, making every file unreachable once the
  port reached 10000+.
- `upnphttp.c`: the `transferMode.dlna.org: Interactive` check was
  inverted, rejecting images and accepting videos instead of the reverse.
- `microdlna.c`: `SIGTERM`/`SIGINT` could be ignored for up to
  `notify_interval` (895s by default) if the signal arrived just before
  `select()`, and the `SIGHUP` handler called non-async-signal-safe code
  (logging, `malloc`, sockets) directly from signal context. Fixed with a
  self-pipe: handlers only set a flag and wake the main loop, which does
  the actual shutdown/reload.
- `microdlna.c`/`threads.c`: a use-after-free at shutdown, where
  `media_dir` and the interface table could be freed while a detached
  `serve_file()` thread was still reading them (caught by
  ThreadSanitizer). Shutdown now waits briefly for in-flight transfers to
  finish before freeing them.

## Compile 

    make

### Compile Release

Compile with more optimisations and stripping

    make BUILD=release

## Tests

There are two independent kinds of tests, plus a ThreadSanitizer variant of
the second kind. None of them affect the `microdlnad` binary built by
`make`, and the real daemon mode (fork/setsid/setuid) is never exercised.

### Unit tests

Link individual `.c` modules directly (no process spawned, no sockets), with
AddressSanitizer/UndefinedBehaviorSanitizer/LeakSanitizer always on:

    make check

`make check` (alias: `make test`) compiles and runs every test under
`tests/`.

### Functional tests

Spawn the real `microdlnad` binary as a separate process and talk to it over
real sockets (HTTP/SOAP over TCP, SSDP over UDP on the loopback interface),
under AddressSanitizer/UndefinedBehaviorSanitizer:

    make check-functional

Run the same functional test suite against a build compiled with
ThreadSanitizer instead, to catch data races under real concurrent traffic:

    make check-functional-tsan

To debug a single functional test binary, or filter by test name:

    MICRODLNAD_BIN=build/tests/functional/sanitized/microdlnad \
      build/tests/functional/bin/ftest_events renew

CI (`.github/workflows/ci.yml`) runs the dev build, the release build, and
all three test suites (unit, functional, functional ThreadSanitizer) as
separate jobs on every push and pull request.

See `docs/dev/TESTS.md` and `docs/dev/FUNCTIONAL_TESTS.md` for how the tests
are organized and how to add one.

## Installation

Install systemd service (cf config file in /usr/local/etc/microdlna.conf)

    ./install.sh


Uninstall systemd service

    ./uninstall.sh

## Thanks

Many thanks to Michael J Walsh for his projet https://github.com/mjfwalsh/microdlna which avoid me a lot of work