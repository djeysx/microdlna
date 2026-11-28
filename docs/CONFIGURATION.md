# Configuration

MicroDLNA takes its settings from the command line, from a configuration file, or
both.

```sh
microdlnad -f /usr/local/etc/microdlna.conf
microdlnad -g -D /path/to/media
```

Options are applied in the order they appear, so a later value replaces an earlier
one. To override a setting from the file, put the option after `-f`.

The installed service reads `/usr/local/etc/microdlna.conf`. Restart it after a
change:

```sh
sudo systemctl restart microdlna
```

## Configuration file syntax

```ini
# A comment, on its own line
media_dir=/home/data/video
friendly_name=Living room
```

- One `name=value` per line. The name is the long command line option without the
  leading `--`; `_` and `-` are interchangeable.
- No spaces around `=`. A space before it makes the option unknown, and a space
  after it becomes part of the value.
- No quotes around values: they would be part of the value.
- Keep comments on their own line. A `#` ends the value wherever it appears, so
  values cannot contain `#`, and any space before it stays in the value.
- Lines without `=` are ignored. An unknown option stops the server with an error.
- Options that take no value, such as `debug` or `foreground`, are accepted on the
  command line only.

## Options

### Media

| Command line | Config file | Default | Description |
|---|---|---|---|
| `-D`, `--media-dir <path>` | `media_dir` | none | Folder to publish. **Required.** It does not need to exist at startup. |

### Network

| Command line | Config file | Default | Description |
|---|---|---|---|
| `-p`, `--port <n>` | `port` | `2800` | HTTP port, 1 to 65535. |
| `-i`, `--network-interface <list>` | `network_interface` | all but loopback | Comma-separated interface names, 4 at most. IPv4 only. |
| `-c`, `--max-connections <n>` | `max_connections` | `10` | Maximum number of simultaneous file transfers, at least 1. |

### UPnP

| Command line | Config file | Default | Description |
|---|---|---|---|
| `-F`, `--friendly-name <name>` | `friendly_name` | host name | Name shown by players, 63 bytes at most. |
| `-U`, `--uuid <uuid>` | `uuid` | built from the MAC address | Device identifier, as `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`. |
| `-t`, `--notify-interval <seconds>` | `notify_interval` | `895` | Delay between two network announcements. |

### Running environment

| Command line | Config file | Default | Description |
|---|---|---|---|
| `-u`, `--user <name or uid>` | `user` | current user | User to switch to after startup. Only the user ID changes, not the group. |
| `-P`, `--pid-file <path>` | `pid_file` | none | PID file. Ignored in the foreground. |
| `-L`, `--log-file <path>` | `log_file` | none | Log file. Ignored in the foreground. |
| `-l`, `--log-level <level>` | `log_level` | `info` | See [Log levels](#log-levels). |

### Command line only

| Option | Description |
|---|---|
| `-f`, `--config-file <path>` | Read a configuration file. |
| `-g`, `--foreground` | Stay in the foreground and log to the terminal. |
| `-S`, `--mode-systemd` | Foreground mode, without timestamps in the log. |
| `-d`, `--debug` | Foreground mode, with the `debug` log level. |
| `-v`, `--verbose` | Set the `error` log level. |
| `-h`, `--help` | Show the help and exit. |
| `-V`, `--version` | Show the version and exit. |

## Foreground and background

Without `-g`, `-S` or `-d`, the server detaches and runs in the background. It
then logs only if `log_file` is set.

In the foreground, `log_file` and `pid_file` are ignored and messages go to the
terminal. This is the case of the installed service, which uses `--mode-systemd`:
read its logs with `journalctl -u microdlna`.

## Log levels

From the least to the most verbose:

`off` < `fatal` < `info` < `error` < `debug`

Note that `error` is **more** verbose than `info`: with the default level, error
messages are not shown. Use `error` or `debug` when something goes wrong. Fatal
startup errors are always printed.

## Signals

| Signal | Effect |
|---|---|
| `SIGHUP` | Re-reads the network interfaces. The configuration file is **not** reloaded. |
| `SIGTERM`, `SIGINT` | Announces the departure to the network and stops. |

After an IP address change:

```sh
sudo systemctl kill -s HUP microdlna
```

## Device icon

The icon is compiled into the binary and chosen at build time with
`make ICON=<name>`. See [Icons](Icons.md).
