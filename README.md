# quectel-CM

`quectel-CM` is the main QConnectManager program for bringing up data connections on Quectel modules. It detects the modem interface, configures the selected PDP/APN, starts the data call, and configures the network interface.

## Build

Install a C compiler and `make`, then build from the repository root:

```sh
make
```

The build places binaries in `./out`:

```text
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

Run `quectel-CM` with `sudo` because it opens modem device nodes and changes network interface state.

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

## Notes

- The default data call is IPv4.
- The default PDP is `1` for QMI and `0` for MBIM.
- In MBIM mode, specify an APN with `-s`; the program exits if no APN is provided.
- Stop a foreground connection with `Ctrl+C` so the program can cleanly bring the interface down.