# quectel-CM

`quectel-CM` is the main QConnectManager program for bringing up data connections on Quectel modules. It detects the modem interface, configures the selected PDP/APN, starts the data call, and configures the network interface.

## Build

Install a C compiler and `make`, then build from the repository root:

```sh
make
```

The build places binaries in `./out`:

```text
out/quectel-cm-test
out/quectel-CM
out/quectel-qmi-proxy
out/quectel-mbim-proxy
out/quectel-atc-proxy
```

For a clean parallel build:

```sh
make clean
make -j
```

To remove generated output:

```sh
make clean
```

## Run

Run `quectel-cm-test` with `sudo` because it opens modem device nodes and changes network interface state.

After inserting a SIM card, restart the modem before running `quectel-CM` so the module refreshes SIM status:

```sh
sudo ./scripts/restart-modem.sh /dev/ttyUSB2 10 soft
```

The first argument is the AT command device. If your module exposes AT commands on a different port, replace `/dev/ttyUSB2` with that device. The second argument is the wait time in seconds before starting `quectel-CM`. The default `soft` mode sends `AT+CFUN=0` followed by `AT+CFUN=1`, which refreshes the modem without forcing USB re-enumeration. Use `hard` only when you want a full `AT+CFUN=1,1` reboot.

Basic IPv4 data call:

```sh
sudo ./out/quectel-CM -s APN_NAME
```

Replace `APN_NAME` with the APN from your carrier, for example `internet` or `cmnet`.

IPv4 and IPv6 data call:

```sh
sudo ./out/quectel-CM -s APN_NAME -4 -6
```

APN with username, password, and authentication mode:

```sh
sudo ./out/quectel-CM -s APN_NAME USERNAME PASSWORD AUTH
```

`AUTH` can be `0`/`none`, `1`/`pap`, `2`/`chap`, or `3`/`MsChapV2`.

If the SIM requires a PIN:

```sh
sudo ./out/quectel-CM -s APN_NAME -p PIN_CODE
```

Select a network interface when multiple modems are present:

```sh
sudo ./out/quectel-CM -i INTERFACE -s APN_NAME
```

Select a PDP context:

```sh
sudo ./out/quectel-CM -n PDP -s APN_NAME
```

Use QMAP interface binding:

```sh
sudo ./out/quectel-CM -n PDP -m IFACE_IDX -s APN_NAME
```

Enable verbose logs:

```sh
sudo ./out/quectel-CM -v -s APN_NAME
```

Save logs to a file:

```sh
sudo ./out/quectel-CM -f quectel-CM.log -s APN_NAME
```

Hang up a running PDP data call:

```sh
sudo ./out/quectel-CM -k PDP
```

## Config-driven modem test

`quectel-cm-test` is a companion test runner for repeated connect/disconnect
testing. It reads a simple `key=value` configuration file, optionally applies
RAT/band settings through AT commands, starts `quectel-CM`, logs modem/network
status, optionally pings an address such as `8.8.8.8`, disconnects, sleeps, and
repeats.

Build it with the normal build command:

```sh
make -j
```

The binary is placed in `./out`:

```text
out/quectel-cm-test
```

Run a test with:

```sh
sudo ./out/quectel-cm-test -c configs/qcm-test.conf
```

Save the test-runner log somewhere else with:

```sh
sudo ./out/quectel-cm-test -c configs/qcm-test.conf -f qcm-test.log
```

The sample config is [configs/qcm-test.conf](configs/qcm-test.conf). Important
keys are:

- `cm_path`: path to the `quectel-CM` binary used to start the data call.
- `apn`, `user`, `password`, `auth`, `pin`, `proxy`, `interface`, `pdp`,
	`qmap_iface_idx`, `ipv4`, `ipv6`, `no_dhcp`, `udhcpc_script`, `bridge`, `verbose`: forwarded
	to `quectel-CM` as normal command-line options.
- `connect_duration_sec`: how long each connection cycle stays up.
	Set this to `0` to keep the connection up until `Ctrl+C`, `SIGINT`, or
	`SIGTERM`.
- `connection_times`: how many connect/disconnect cycles to run.
- `sleep_between_sec`: sleep time after disconnect before the next cycle.
- `connect_wait_sec`: time allowed for `quectel-CM` to bring up the data call
	before the first status snapshot.
- `status_interval_sec`: interval for modem info, interface status, and ping
	snapshots during the connected period.
- `log_dir`: directory for generated and bare log filenames. The default is
	`log`.
- `log_file`: log file used by `quectel-cm-test`. If this is a bare filename,
	it is created under `log_dir`. If empty, a filename is generated from the
	config filename.
- `cm_log_file`: log file passed to `quectel-CM`; use the same file if you want
	one combined log. Bare filenames are also created under `log_dir`.
- `at_device`: AT command port, for example `/dev/ttyUSB2`.
- `rat` and `band`: friendly names that are resolved through `at_map_file`.
- `info_commands`: AT commands logged during each status snapshot, separated by
	`|`.
- `ping_enable`, `ping_address`, `ping_count`, `ping_timeout_sec`: optional
	log-only ping check.

Set `udhcpc_script` when you want `busybox udhcpc` to use a script from a
custom location instead of `/usr/share/udhcpc/default.script` or
`/etc/udhcpc/default.script`:

```ini
udhcpc_script=scripts/default.script_ip
```

This passes `-S scripts/default.script_ip` to `quectel-CM`, which then runs
`busybox udhcpc ... -s scripts/default.script_ip`.

On systems using `systemd-resolved`, `/etc/resolv.conf` usually points to the
local stub resolver `127.0.0.53`. In that mode, the DHCP script must update
`systemd-resolved` link DNS state with `resolvectl`; editing `/etc/resolv.conf`
or calling old `resolvconf -a` syntax may leave the modem link without DNS until
the DNS service is restarted. The provided DHCP scripts handle this case.

When `quectel-CM` stops, it also runs the configured udhcpc script with the
`deconfig` action so stale `systemd-resolved` DNS state is removed from the modem
interface. The `default.script_ip` script accepts dotted DHCP netmasks such as
`255.255.240.0` and converts them to CIDR prefixes for the `ip` command.

Friendly RAT/band names are mapped to real AT commands in
[configs/qcm-at-map.conf](configs/qcm-at-map.conf). Keep this file easy to
inspect and edit. Quectel RAT/band commands can vary by module and firmware, so
verify the command strings for your exact module before using them in long tests.
Multiple AT commands for one friendly name can be separated with `|`.

Preset config files are provided for common tests:

- [configs/qcm-5g-n78.conf](configs/qcm-5g-n78.conf): 5G NR n78,
	keep connected.
- [configs/qcm-5g-n78-cycletest.conf](configs/qcm-5g-n78-cycletest.conf): 5G NR n78,
	connect 10 seconds, sleep 5 seconds, repeat 10 times.
- [configs/qcm-4g.conf](configs/qcm-4g.conf):
	LTE mode, all/default LTE bands, keep connected.

Run them with:

```sh
sudo ./out/quectel-cm-test -c configs/qcm-5g-n78.conf
sudo ./out/quectel-cm-test -c configs/qcm-5g-n78-cycletest.conf
sudo ./out/quectel-cm-test -c configs/qcm-4g.conf
```

The test log shows connection evidence from several layers:

- `quectel-CM` logs the modem mode, registration/data-call activity, and IP
	assignment.
- `quectel-cm-test` logs AT query output such as `AT+QNWINFO`, `AT+QCSQ`,
	`AT+COPS?`, `AT+CGREG?`, `AT+CEREG?`, and `AT+C5GREG?`.
- If `interface` is configured, `quectel-cm-test` logs interface operstate and
	address information.
- If `ping_enable=1`, ping output is logged. Ping is log-only: a ping failure is
	recorded but does not force disconnect or reconnect.

## Notes

- The default data call is IPv4.
- The default PDP is `1` for QMI and `0` for MBIM.
- In MBIM mode, specify an APN with `-s`; the program exits if no APN is provided.
- Stop a foreground connection with `Ctrl+C` so the program can cleanly bring the interface down.