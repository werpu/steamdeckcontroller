#!/usr/bin/env sh
set -eu

usage() {
    echo "Usage: $0 [--prefix /usr/local] [--build-dir build]"
}

PREFIX="/usr/local"
BUILD_DIR="build"

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
BINARY="$REPO_DIR/$BUILD_DIR/steamdeckcontroller"
DAEMON="$REPO_DIR/$BUILD_DIR/steamdeckcontrollerd"

if [ ! -x "$BINARY" ]; then
    echo "Missing executable: $BINARY" >&2
    echo "Build first:" >&2
    echo "  cmake -S . -B $BUILD_DIR" >&2
    echo "  cmake --build $BUILD_DIR" >&2
    exit 1
fi
if [ ! -x "$DAEMON" ]; then
    echo "Missing executable: $DAEMON" >&2
    echo "Build first:" >&2
    echo "  cmake -S . -B $BUILD_DIR" >&2
    echo "  cmake --build $BUILD_DIR" >&2
    exit 1
fi

install -d "$PREFIX/bin"
install -m 0755 "$BINARY" "$PREFIX/bin/steamdeckcontroller"
install -m 0755 "$DAEMON" "$PREFIX/bin/steamdeckcontrollerd"

install -d "$PREFIX/lib/steamdeckcontroller"
install -m 0755 "$SCRIPT_DIR/prepare-gadget.sh" "$PREFIX/lib/steamdeckcontroller/prepare-gadget.sh"

install -d /etc/systemd/system
install -m 0644 "$SCRIPT_DIR/steamdeckcontroller-prepare.service" /etc/systemd/system/steamdeckcontroller-prepare.service
install -m 0644 "$SCRIPT_DIR/steamdeckcontroller.service" /etc/systemd/system/steamdeckcontroller.service

install -d /usr/local/share/applications
install -m 0644 "$SCRIPT_DIR/steamdeckcontroller.desktop" /usr/local/share/applications/steamdeckcontroller.desktop

systemctl daemon-reload
systemctl enable steamdeckcontroller-prepare.service
systemctl enable steamdeckcontroller.service

echo "Installed steamdeckcontroller to $PREFIX/bin/steamdeckcontroller"
echo "Installed steamdeckcontrollerd to $PREFIX/bin/steamdeckcontrollerd"
echo "Installed preparation service: steamdeckcontroller-prepare.service"
echo "Installed daemon service: steamdeckcontroller.service"
echo
echo "Start services with:"
echo "  sudo systemctl start steamdeckcontroller-prepare.service"
echo "  sudo systemctl start steamdeckcontroller.service"
echo "  systemctl status steamdeckcontroller-prepare.service"
echo "  systemctl status steamdeckcontroller.service"
echo
echo "The desktop launcher runs the unprivileged GTK frontend."
