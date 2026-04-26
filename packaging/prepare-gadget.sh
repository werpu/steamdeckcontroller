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

if [ ! -d /sys/class/udc ] || ! find /sys/class/udc -mindepth 1 -maxdepth 1 | grep -q .; then
    echo "No USB device controller is exposed in /sys/class/udc." >&2
    echo "On Steam Deck, check BIOS: Advanced > USB Configuration > USB Dual Role Device = DRD." >&2
    exit 1
fi

exit 0
