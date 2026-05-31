#!/usr/bin/env sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "prepare-gadget.sh must run as root" >&2
    exit 1
fi

modprobe libcomposite 2>/dev/null || true

if ! mountpoint -q /sys/kernel/config; then
    mount -t configfs none /sys/kernel/config
fi

if [ ! -d /sys/kernel/config/usb_gadget ]; then
    echo "ConfigFS is mounted, but /sys/kernel/config/usb_gadget is missing." >&2
    echo "The running kernel may not expose USB gadget support." >&2
    exit 1
fi

# A missing UDC is a normal idle state: the controller only appears when the
# Deck is connected to a host in device mode. Warn but do not fail here — the
# daemon checks for a UDC at START time and reports it then. Failing here would
# block the daemon service from starting at all.
if [ ! -d /sys/class/udc ] || ! find /sys/class/udc -mindepth 1 -maxdepth 1 | grep -q .; then
    echo "No USB device controller in /sys/class/udc yet (normal when not connected to a host)." >&2
    echo "If START later fails, check BIOS: Advanced > USB Configuration > USB Dual Role Device = DRD." >&2
fi

exit 0
