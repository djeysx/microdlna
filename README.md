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

Other topics:

- Build adjustments (debug/release modes).
- Reworked config file (`microdlna.conf`), notably for Raspberry Pi media
  paths.
- Added `install.sh`/`uninstall.sh` scripts and the systemd unit.

## Compile 

    ./configure.sh
    make

### Compile Release

Compile with more optimisations and stripping

    ./configure.sh
    make BUILD=release

## Installation

Install systemd service (cf config file in /usr/local/etc/microdlna.conf)

    ./install.sh


Uninstall systemd service

    ./uninstall.sh

## Thanks

Many thanks to Michael J Walsh for his projet https://github.com/mjfwalsh/microdlna which avoid me a lot of work