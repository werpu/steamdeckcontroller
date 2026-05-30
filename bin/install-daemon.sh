#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 [--prefix /usr/local] [--build-dir dist]"
    echo ""
    echo "Installs only the daemon (steamdeckcontrollerd) and its systemd services."
    echo "Does not require the GTK frontend to be built."
}

PREFIX="/usr/local"
BUILD_DIR="dist"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            PREFIX="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
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

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
PKG_DIR="$REPO_DIR/packaging"
DAEMON="$REPO_DIR/$BUILD_DIR/steamdeckcontrollerd"

if [ ! -x "$DAEMON" ]; then
    echo "Missing executable: $DAEMON" >&2
    echo "Build first:  bin/build_under_macos.sh  or  bin/build_under_x86.sh" >&2
    exit 1
fi

install -d "$PREFIX/bin"
install -m 0755 "$DAEMON" "$PREFIX/bin/steamdeckcontrollerd"

install -d "$PREFIX/lib/steamdeckcontroller"
install -m 0755 "$PKG_DIR/prepare-gadget.sh" "$PREFIX/lib/steamdeckcontroller/prepare-gadget.sh"

install -d /etc/systemd/system
install -m 0644 "$PKG_DIR/steamdeckcontroller-prepare.service" /etc/systemd/system/steamdeckcontroller-prepare.service
install -m 0644 "$PKG_DIR/steamdeckcontroller.service" /etc/systemd/system/steamdeckcontroller.service

systemctl daemon-reload
systemctl enable steamdeckcontroller-prepare.service
systemctl enable steamdeckcontroller.service

echo "Installed steamdeckcontrollerd to $PREFIX/bin/steamdeckcontrollerd"
echo "Installed prepare-gadget.sh to $PREFIX/lib/steamdeckcontroller/"
echo "Installed and enabled systemd services."
echo ""
echo "Start the daemon with:"
echo "  sudo systemctl start steamdeckcontroller-prepare.service"
echo "  sudo systemctl start steamdeckcontroller.service"
echo ""
echo "Check status with:"
echo "  systemctl status steamdeckcontroller.service"
echo "  journalctl -u steamdeckcontroller.service -f"
