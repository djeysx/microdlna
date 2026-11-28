# Installation

## Requirements

- Linux
- A C compiler (C11) and `make`
- `pod2man`, which comes with Perl, for the manual page
- `git`, used to record the version in the binary

On Debian or Raspberry Pi OS:

```sh
sudo apt install build-essential git perl
```

There is no library to install.

## Build

```sh
git clone https://github.com/djeysx/microdlna.git
cd microdlna
make BUILD=release
```

The binary is `build/microdlnad` and the manual page `build/microdlnad.8`.

A plain `make` produces a development build with debug information. Run
`make clean` when switching between the two, otherwise the objects already built
are reused.

To use another device icon, add `ICON=<name>` (see [Icons](Icons.md)).

## Try it without installing

```sh
build/microdlnad -g -D /path/to/media
```

Press Ctrl+C to stop.

## Install as a service

Check `microdlna.service` first:

- `User=pi` and `Group=pi`: change them if you want another account. That account
  must be able to read the media folder and enter its parent folders.
- The unit restricts what the server can do: the file system is read-only except
  `/tmp`, and `/tmp` is private to the service. A media folder under `/tmp` is
  therefore not visible.
- A port below 1024 needs the `AmbientCapabilities` lines to be uncommented.

Then build and install:

```sh
make BUILD=release
sudo ./install.sh
```

The script does not build anything. It installs:

| File | Destination |
|---|---|
| `build/microdlnad` | `/usr/local/bin/` |
| `microdlna.conf` | `/usr/local/etc/`, only if it is not there yet |
| `microdlna.service` | `/etc/systemd/system/` |
| `build/microdlnad.8` | `/usr/local/share/man/man8/` |

It then enables and starts the service.

Set `media_dir` in `/usr/local/etc/microdlna.conf` and restart:

```sh
sudo systemctl restart microdlna
systemctl status microdlna
```

Leave the `user` setting unset in that file: the service already chooses the
account.

## Update

```sh
git pull
make clean
make BUILD=release
sudo ./install.sh
sudo systemctl restart microdlna
```

The restart is needed because the script does not restart a service that is
already running.

Your configuration file is kept. The service unit is **overwritten**: apply your
changes to `microdlna.service` again, or keep them in a systemd override
(`sudo systemctl edit microdlna`).

## Uninstall

```sh
sudo ./uninstall.sh
```

> **Warning:** this also deletes `/usr/local/etc/microdlna.conf`. Copy it first if
> you want to keep your settings.

## Next steps

- [Configuration](CONFIGURATION.md)
- [Troubleshooting](TROUBLESHOOTING.md)
