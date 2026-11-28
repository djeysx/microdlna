# Troubleshooting

## Read the logs first

```sh
journalctl -u microdlna -f
```

Error messages are hidden at the default log level. Add this line to
`/usr/local/etc/microdlna.conf`, then restart the service:

```ini
log_level=debug
```

You can also stop the service and run the server by hand:

```sh
sudo systemctl stop microdlna
microdlnad -f /usr/local/etc/microdlna.conf -d
```

## The service does not start

| Message | Cause |
|---|---|
| `Unknown option: …` | Misspelled setting, or a space before `=`. |
| `Invalid log level`, `Invalid port`, `Invalid uuid` | Value not accepted, see [Configuration](CONFIGURATION.md). Check for trailing spaces. |
| `You must specify a media dir` | `media_dir` is missing. |
| `Failed to open socket for HTTP` | Usually the port is already in use. Choose another `port`. With `log_level=debug`, the preceding `bind(http)` line gives the error number. |

If `systemctl status microdlna` reports `status=217/USER`, the account named in
`microdlna.service` does not exist.

## The TV does not see the server

1. Check that the TV and the server are on the same network and subnet. Clients
   from other subnets are rejected.
2. Open the firewall for **UDP 1900** (discovery, multicast) and **TCP 2800** (or
   your `port`).
3. Check the log for `Enabling interface …` lines. If you set
   `network_interface`, check the interface name.
4. Fetch the device description from another machine, using the server's **IP
   address**:

   ```sh
   curl http://192.168.1.10:2800/rootDesc.xml
   ```

   Requests made with a host name instead of the IP address are refused.

## The server waits for the network

`Failed to find any network interfaces (retrying in … seconds)` means that no
usable IPv4 interface was found. The server retries until one appears. If the
network is up, check the `network_interface` setting.

While it is waiting, the server does not react to stop requests, so
`systemctl stop` or `restart` can take a while, until systemd kills it.

If the server's address changes while it is running, tell it to re-read the
interfaces:

```sh
sudo systemctl kill -s HUP microdlna
```

## Folders or files are missing

- Check `media_dir`. If it does not exist, browsing fails until it appears.
- The account running the service (`pi` by default) must be able to read the
  files and to enter every folder leading to them.
- A media folder under `/tmp` is not visible to the service.
- Names starting with `.` or `$` are hidden.
- Only files with a recognised extension are listed, such as `.mp4`, `.mkv`,
  `.avi`, `.mp3`, `.flac` or `.jpg`.

## A file is listed but does not play

There is no format conversion: check that your player supports the format. Try the
same file in VLC to tell a player limitation from a server problem.

If several players are streaming at once and the log shows
`Exceeded max connections`, raise `max_connections`.
