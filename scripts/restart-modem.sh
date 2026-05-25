#!/bin/sh

set -u

AT_DEVICE="${1:-/dev/ttyUSB2}"
WAIT_SECONDS="${2:-10}"
MODE="${3:-soft}"

usage() {
    echo "Usage: sudo $0 [at_device] [wait_seconds] [soft|hard]"
    echo "Example: sudo $0 /dev/ttyUSB2 10 soft"
    echo "Example: sudo $0 /dev/ttyUSB2 30 hard"
}

print_missing_device_help() {
    echo "AT device '$AT_DEVICE' did not come back after ${WAIT_SECONDS}s." >&2
    echo "Check that the modem is visible to Linux, for example:" >&2
    echo "  lsusb" >&2
    echo "  ls /dev/ttyUSB* /dev/cdc-wdm* 2>/dev/null" >&2
    echo "If this is a virtual machine, reconnect/pass through the Quectel USB device to the VM." >&2
}

case "$AT_DEVICE" in
    -h|--help)
        usage
        exit 0
        ;;
esac

case "$WAIT_SECONDS" in
    *[!0-9]*|'')
        echo "wait_seconds must be a positive integer" >&2
        usage >&2
        exit 1
        ;;
esac

case "$MODE" in
    soft|hard)
        ;;
    *)
        echo "mode must be 'soft' or 'hard'" >&2
        usage >&2
        exit 1
        ;;
esac

if [ ! -e "$AT_DEVICE" ]; then
    echo "AT device '$AT_DEVICE' does not exist" >&2
    exit 1
fi

if [ ! -w "$AT_DEVICE" ]; then
    echo "AT device '$AT_DEVICE' is not writable. Run this script with sudo." >&2
    exit 1
fi

OLD_STTY=""
if command -v stty >/dev/null 2>&1; then
    OLD_STTY="$(stty -F "$AT_DEVICE" -g 2>/dev/null || true)"
    stty -F "$AT_DEVICE" 115200 cs8 -cstopb -parenb -ixon -ixoff -crtscts raw -echo min 0 time 10 2>/dev/null || true
fi

echo "Sending modem restart command to $AT_DEVICE"
if ! printf 'AT\r' > "$AT_DEVICE"; then
    echo "Failed to write to '$AT_DEVICE'" >&2
    exit 1
fi

sleep 1

if [ "$MODE" = "hard" ]; then
    if ! printf 'AT+CFUN=1,1\r' > "$AT_DEVICE"; then
        echo "Failed to send restart command to '$AT_DEVICE'" >&2
        exit 1
    fi
else
    if ! printf 'AT+CFUN=0\r' > "$AT_DEVICE"; then
        echo "Failed to set modem to minimum functionality on '$AT_DEVICE'" >&2
        exit 1
    fi

    sleep 3

    if ! printf 'AT+CFUN=1\r' > "$AT_DEVICE"; then
        echo "Failed to restore modem full functionality on '$AT_DEVICE'" >&2
        exit 1
    fi
fi

if [ -n "$OLD_STTY" ]; then
    stty -F "$AT_DEVICE" "$OLD_STTY" 2>/dev/null || true
fi

if [ "$MODE" = "hard" ]; then
    echo "Hard restart command sent. Waiting ${WAIT_SECONDS}s for the modem to re-enumerate."
else
    echo "Soft restart command sent. Waiting ${WAIT_SECONDS}s for SIM status to refresh."
fi
sleep "$WAIT_SECONDS"

if [ ! -e "$AT_DEVICE" ]; then
    print_missing_device_help
    exit 1
fi

echo "Restarted."