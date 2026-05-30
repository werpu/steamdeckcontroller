#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo $0" >&2
    exit 1
fi

REAL_USER="${SUDO_USER:-}"
if [ -z "$REAL_USER" ] || [ "$REAL_USER" = "root" ]; then
    echo "Cannot determine target user. Invoke via sudo from your normal account:" >&2
    echo "  sudo $0" >&2
    exit 1
fi

USER_HOME=$(getent passwd "$REAL_USER" | cut -d: -f6)
if [ -z "$USER_HOME" ] || [ ! -d "$USER_HOME" ]; then
    echo "Home directory for '$REAL_USER' not found." >&2
    exit 1
fi

DAEMON_DIR="$USER_HOME/.local/share/steamdeckcontroller"
USER_BIN="$USER_HOME/.local/bin"

echo "==> Stopping and disabling services..."
systemctl disable --now steamdeckcontroller.service 2>/dev/null || true
systemctl disable --now steamdeckcontroller-prepare.service 2>/dev/null || true

echo "==> Removing systemd service files..."
rm -f /etc/systemd/system/steamdeckcontroller.service
rm -f /etc/systemd/system/steamdeckcontroller-prepare.service
systemctl daemon-reload

echo "==> Removing binaries..."
rm -rf "$DAEMON_DIR"
rm -f "$USER_BIN/steamdeckcontroller"

echo "==> Cleaning up runtime socket..."
rm -f /run/steamdeckcontroller/control.sock
rmdir /run/steamdeckcontroller 2>/dev/null || true

echo ""
echo "Removed Steam Deck Controller Passthrough for user '$REAL_USER'."
echo "Any active USB gadget must be stopped before the kernel module can be unloaded."
