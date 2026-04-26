#!/usr/bin/env sh
set -eu

PREFIX="/usr/local"

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo $0" >&2
    exit 1
fi

systemctl disable --now steamdeckcontroller.service 2>/dev/null || true
systemctl disable --now steamdeckcontroller-prepare.service 2>/dev/null || true
rm -f /etc/systemd/system/steamdeckcontroller.service
rm -f /etc/systemd/system/steamdeckcontroller-prepare.service
systemctl daemon-reload

rm -f "$PREFIX/bin/steamdeckcontroller"
rm -f "$PREFIX/bin/steamdeckcontrollerd"
rm -rf "$PREFIX/lib/steamdeckcontroller"
rm -f /usr/local/share/applications/steamdeckcontroller.desktop

echo "Removed Steam Deck Controller Passthrough installation files."
echo "Any active USB gadget created by a still-running app must be stopped from the app first."
