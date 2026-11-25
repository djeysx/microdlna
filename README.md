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

## Compile 

    ./configure.sh
    make

### Compile Release

Compile with more optimisations and stripping

    ./configure.sh
    make BUILD=release

## Tests

Unit tests, no external dependency required, with
AddressSanitizer/UndefinedBehaviorSanitizer/LeakSanitizer always on:

    ./configure.sh
    make check

`make check` (alias: `make test`) compiles and runs every test under
`tests/`; it never affects the `microdlnad` binary built by `make`. CI
(`.github/workflows/ci.yml`) runs it, plus the dev and release builds, on
every push and pull request.

See `docs/dev/TESTS.md` for how the tests are organized and how to add one.

## Installation

Install systemd service (cf config file in /usr/local/etc/microdlna.conf)

    ./install.sh


Uninstall systemd service

    ./uninstall.sh

## Thanks

Many thanks to Michael J Walsh for his projet https://github.com/mjfwalsh/microdlna which avoid me a lot of work