#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 [--prefix /usr/local]"
    echo ""
    echo "Removes the daemon (steamdeckcontrollerd) and its systemd services."
    echo "Does not touch the GTK frontend if installed separately."
}

PREFIX="/usr/local"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            PREFIX="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 1
            ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo $0" >&2
    exit 1
fi

systemctl disable --now steamdeckcontroller.service 2>/dev/null || true
systemctl disable --now steamdeckcontroller-prepare.service 2>/dev/null || true

rm -f /etc/systemd/system/steamdeckcontroller.service
rm -f /etc/systemd/system/steamdeckcontroller-prepare.service
systemctl daemon-reload

rm -f "$PREFIX/bin/steamdeckcontrollerd"
rm -rf "$PREFIX/lib/steamdeckcontroller"

rm -f /run/steamdeckcontroller/control.sock
rmdir /run/steamdeckcontroller 2>/dev/null || true

echo "Removed steamdeckcontrollerd and its systemd services."
echo "Any active USB gadget must be torn down manually before the kernel module can be unloaded."
