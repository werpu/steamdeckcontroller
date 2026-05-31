#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 [--prefix /usr/local] [--build-dir dist]"
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
BINARY="$REPO_DIR/$BUILD_DIR/steamdeckcontroller"
DAEMON="$REPO_DIR/$BUILD_DIR/steamdeckcontrollerd"

if [ ! -x "$BINARY" ]; then
    echo "Missing executable: $BINARY" >&2
    echo "Build first:  bin/build_under_macos.sh  or  bin/build_under_x86.sh" >&2
    exit 1
fi
if [ ! -x "$DAEMON" ]; then
    echo "Missing executable: $DAEMON" >&2
    echo "Build first:  bin/build_under_macos.sh  or  bin/build_under_x86.sh" >&2
    exit 1
fi

if ! getent group steamdeckctl >/dev/null 2>&1; then
    groupadd --system steamdeckctl
    echo "Created group: steamdeckctl"
fi
CALLING_USER="${SUDO_USER:-}"
if [ -n "$CALLING_USER" ] && [ "$CALLING_USER" != "root" ]; then
    usermod -aG steamdeckctl "$CALLING_USER"
    echo "Added $CALLING_USER to group steamdeckctl (re-login required)."
fi

install -d "$PREFIX/bin"
install -m 0755 "$BINARY" "$PREFIX/bin/steamdeckcontroller"
install -m 0755 "$DAEMON" "$PREFIX/bin/steamdeckcontrollerd"

install -d "$PREFIX/lib/steamdeckcontroller"
install -m 0755 "$PKG_DIR/prepare-gadget.sh" "$PREFIX/lib/steamdeckcontroller/prepare-gadget.sh"

install -d /etc/systemd/system
install -m 0644 "$PKG_DIR/steamdeckcontroller-prepare.service" /etc/systemd/system/steamdeckcontroller-prepare.service
install -m 0644 "$PKG_DIR/steamdeckcontroller.service" /etc/systemd/system/steamdeckcontroller.service

install -d /usr/local/share/applications
install -m 0644 "$PKG_DIR/steamdeckcontroller.desktop" /usr/local/share/applications/steamdeckcontroller.desktop

systemctl daemon-reload
systemctl enable steamdeckcontroller-prepare.service
systemctl enable steamdeckcontroller.service

echo "Installed steamdeckcontroller to $PREFIX/bin/steamdeckcontroller"
echo "Installed steamdeckcontrollerd to $PREFIX/bin/steamdeckcontrollerd"
echo "Installed preparation service: steamdeckcontroller-prepare.service"
echo "Installed daemon service: steamdeckcontroller.service"

echo ""
echo "Starting services..."
systemctl reset-failed steamdeckcontroller-prepare.service 2>/dev/null || true
systemctl start steamdeckcontroller-prepare.service || true
systemctl start steamdeckcontroller.service
echo ""
systemctl --no-pager --lines=0 status steamdeckcontroller.service || true

if [ -n "$CALLING_USER" ] && [ "$CALLING_USER" != "root" ]; then
    echo ""
    echo "NOTE: $CALLING_USER was added to group steamdeckctl. Log out and back in"
    echo "(or reboot) before running the frontend, so it can reach the daemon socket."
fi
echo ""
echo "The desktop launcher runs the unprivileged GTK frontend."
