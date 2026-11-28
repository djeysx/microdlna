# Changelog

Changes made in this fork of
[microdlna](https://github.com/mjfwalsh/microdlna).

## Unreleased

### Added

- Device icon chosen at build time (`make ICON=<name>`), with `gen_icon.sh` to
  import your own image, see [Icons](docs/Icons.md).
- Unit tests, functional tests, a ThreadSanitizer variant and a CI workflow.
- Installation, configuration and troubleshooting pages.

### Changed

- The `configure.sh` step is gone: run `make` directly.
- Build output goes to `build/`.
- The sample configuration puts the PID file in `/tmp`.

### Fixed

- Icons were sent with their last byte missing.
- Event subscriptions (GENA) were unusable: the response to `SUBSCRIBE` lacked the
  `SID` and `Timeout` headers, so a client could not renew or unsubscribe.
  Subscribing to an unknown event URL returned `200` instead of an error.
- Out-of-bounds reads on event subscriber identifiers and on a `Callback` header
  without a path.
- Discovery (`M-SEARCH`) requests over about 199 bytes, common with a long
  `USER-AGENT`, were dropped instead of answered.
- The port was truncated to 4 digits in Browse results, making every file
  unreachable with a port of 10000 or higher.
- The `transferMode.dlna.org: Interactive` check was inverted, rejecting images
  and accepting videos.
- `SIGTERM` and `SIGINT` could be ignored for up to `notify_interval`, and the
  `SIGHUP` handler ran code that is unsafe in a signal handler.
- Use-after-free at shutdown while a file transfer was still running.
- Out-of-bounds read for short file names without an extension.
- Out-of-bounds read when validating a short `--uuid`.
- A valid but empty Browse result got no HTTP response.

## 1.1

### Changed

- The device description names this fork as manufacturer.
- The README describes the changes made in the fork.

## 1.0

First version of this fork.

### Added

- Development and release build modes.
- `install.sh` and `uninstall.sh` scripts, systemd unit and manual page
  installation.
- Commented configuration file, with media paths suited to a Raspberry Pi.

### Fixed

- Files larger than 2 GB are sent correctly, with a fallback when `sendfile()`
  fails or makes no progress.
- The `Host` header check (against DNS rebinding) could accept a prefix instead of
  the exact value.
- The path of a served file is built with bounds checking, and is no longer
  URL-decoded twice.
- More robust parsing of the network interface list.

### Included from upstream

Changes by Michael J. Walsh:

- Retry with increasing delays while no network interface is available, instead of
  failing at startup.
- Removal of a problematic `setpriority()` call.
