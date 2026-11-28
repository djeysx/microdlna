# MicroDLNA

[![CI](https://github.com/djeysx/microdlna/actions/workflows/ci.yml/badge.svg)](https://github.com/djeysx/microdlna/actions/workflows/ci.yml)
[![License: GPL v2](https://img.shields.io/badge/license-GPLv2-blue.svg)](COPYING)

A lightweight, stateless DLNA media server written in C. Point it at a folder and
watch your videos on your TV, with no database to build and nothing to scan.

## Features

- **No database, no scan:** new, moved or removed files show up immediately.
- **No external dependencies**, with an emphasis on low memory usage.
- **Tolerant at startup:** it waits for the network to come up, and the media
  directory does not need to exist yet.
- **Ready to run as a service:** systemd unit and install scripts included.
- **Custom device icon**, chosen at build time.

## Limitations

By design, MicroDLNA has:

- no search and no custom sorting,
- no thumbnails,
- no format conversion: your player must support the files as they are,
- no IPv6.

## Quick start

On Linux, with a C compiler, `make`, `git` and `pod2man` installed:

```sh
git clone https://github.com/djeysx/microdlna.git
cd microdlna
make BUILD=release
```

Set `media_dir` in `microdlna.conf` to the folder holding your media, then install
and start the service:

```sh
sudo ./install.sh
systemctl status microdlna
```

The service runs as the user `pi`. If that user does not exist on your machine,
edit `microdlna.service` before installing. See [Installation](docs/INSTALLATION.md)
for the details.

## Configuration

The installed configuration file is `/usr/local/etc/microdlna.conf`. The most
useful settings:

| Setting | Purpose | Default |
|---|---|---|
| `media_dir` | Folder to publish (required) | none |
| `friendly_name` | Name shown on your TV | host name |
| `network_interface` | Interfaces to use, e.g. `eth0,wlan0` | all but loopback |
| `port` | HTTP port | `2800` |

Restart the service after a change:

```sh
sudo systemctl restart microdlna
```

All options are described in [Configuration](docs/CONFIGURATION.md).

## Tested with

- A TV, over DLNA
- VLC

## Why this fork

This fork exists to run microdlna on my Raspberry Pi and enjoy my videos on my
TV over DLNA, and to fix blocking issues I ran into with the forked version.

It has been a success: it now runs reliably on my Raspberry Pi and works well.

Among the improvements:

- Files larger than 2 GB are sent correctly.
- Stopping the service no longer waits for the next network announcement.
- Long discovery requests are answered instead of being silently dropped.
- Playback links work when the port is 10000 or higher.
- Stricter checks on incoming HTTP requests.

The full list is in the [Changelog](CHANGELOG.md).

## Development

```sh
make                # development build, in build/
make check          # unit tests
make check-functional
make check-functional-tsan
```

- **Unit tests** (`make check`, alias `make test`) link individual modules
  directly, with no process and no sockets, under AddressSanitizer,
  UndefinedBehaviorSanitizer and LeakSanitizer.
- **Functional tests** (`make check-functional`) start the real server and talk to
  it over HTTP/SOAP and SSDP on the loopback interface, under AddressSanitizer
  and UndefinedBehaviorSanitizer.
- **`make check-functional-tsan`** runs the functional tests under
  ThreadSanitizer to catch data races.

The tests do not affect the `microdlnad` binary built by `make`, and they never
exercise the real daemon mode (fork, setsid, setuid).

To run one functional test binary, filtered by test name, after
`make check-functional`:

```sh
MICRODLNAD_BIN=build/tests/functional/sanitized/microdlnad \
  build/tests/functional/bin/ftest_events renew
```

CI runs the development build, the release build and the three test suites on
every push and pull request.

## Documentation

| Page | Content |
|---|---|
| [Installation](docs/INSTALLATION.md) | Build, service, update, uninstall |
| [Configuration](docs/CONFIGURATION.md) | Every option and the config file syntax |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Server not found, empty folders, logs |
| [Icons](docs/Icons.md) | Using your own device icon |
| [Changelog](CHANGELOG.md) | What changed in each version |

## Credits

MicroDLNA is a fork of [microdlna](https://github.com/mjfwalsh/microdlna) by
Michael J. Walsh, itself a fork of MiniDLNA, and it includes code from the
MiniUPnP project. Many thanks to Michael J. Walsh, whose project saved me a lot
of work.

## License

GNU General Public License version 2, see [COPYING](COPYING). The licenses of the
original projects are in [LICENCE.minidlna](LICENCE.minidlna) and
[LICENCE.miniupnpd](LICENCE.miniupnpd).
