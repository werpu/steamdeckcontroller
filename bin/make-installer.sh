#!/usr/bin/env bash
set -euo pipefail

# Packages dist/ binaries into a single self-extracting installer script.
#
# The generated installer (steamdeckcontroller-install.sh):
#   - installs the daemon to ~/.local/share/steamdeckcontroller/  (survives SteamOS updates)
#   - installs the GTK frontend to ~/.local/bin/                  (survives SteamOS updates)
#   - generates and installs systemd service files to /etc/systemd/system/
#     with paths baked in at install time

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
DIST_DIR="$REPO_DIR/dist"
PKG_DIR="$REPO_DIR/packaging"
OUTPUT="$DIST_DIR/steamdeckcontroller-install.sh"

mkdir -p "$DIST_DIR"

if [ ! -x "$DIST_DIR/steamdeckcontrollerd" ]; then
    echo "Missing: $DIST_DIR/steamdeckcontrollerd" >&2
    echo "Run bin/build_under_macos.sh or bin/build_under_x86.sh first." >&2
    exit 1
fi

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

cp "$DIST_DIR/steamdeckcontrollerd" "$STAGE/steamdeckcontrollerd"
chmod 0755 "$STAGE/steamdeckcontrollerd"

cp "$PKG_DIR/prepare-gadget.sh" "$STAGE/prepare-gadget.sh"
chmod 0755 "$STAGE/prepare-gadget.sh"

PAYLOAD_FILES="steamdeckcontrollerd prepare-gadget.sh"

if [ -x "$DIST_DIR/steamdeckcontroller" ]; then
    cp "$DIST_DIR/steamdeckcontroller" "$STAGE/steamdeckcontroller"
    chmod 0755 "$STAGE/steamdeckcontroller"
    PAYLOAD_FILES="$PAYLOAD_FILES steamdeckcontroller"
fi

tar -czf "$STAGE/payload.tar.gz" -C "$STAGE" $PAYLOAD_FILES

# ---- write installer header (single-quoted heredoc = no expansion here) ----
cat > "$OUTPUT" << 'INSTALLER_END'
#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo $0" >&2
    exit 1
fi

# Determine the real user who invoked sudo
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

echo "==> Installing for user '$REAL_USER' (home: $USER_HOME)"

# Create the steamdeckctl group and add the user to it so they can reach the
# daemon socket (which is 0660 root:steamdeckctl).
if ! getent group steamdeckctl >/dev/null 2>&1; then
    groupadd --system steamdeckctl
    echo "  Created group: steamdeckctl"
fi
NEEDS_REBOOT=false
if ! id -nG "$REAL_USER" | tr ' ' '\n' | grep -qx steamdeckctl; then
    usermod -aG steamdeckctl "$REAL_USER"
    echo "  Added $REAL_USER to group steamdeckctl"
    NEEDS_REBOOT=true
fi

# Extract payload
TMPDIR_EXTRACT=$(mktemp -d)
trap 'rm -rf "$TMPDIR_EXTRACT"' EXIT

PAYLOAD_LINE=$(awk '/^__PAYLOAD__$/{print NR+1; exit}' "$0")
tail -n +"$PAYLOAD_LINE" "$0" | base64 -d | tar -xzf - -C "$TMPDIR_EXTRACT"

# Install daemon and gadget helper into user's persistent home dir
mkdir -p "$DAEMON_DIR"
install -m 0755 "$TMPDIR_EXTRACT/steamdeckcontrollerd" "$DAEMON_DIR/steamdeckcontrollerd"
install -m 0755 "$TMPDIR_EXTRACT/prepare-gadget.sh"    "$DAEMON_DIR/prepare-gadget.sh"
chown -R "$REAL_USER:$REAL_USER" "$DAEMON_DIR"
echo "  $DAEMON_DIR/steamdeckcontrollerd"
echo "  $DAEMON_DIR/prepare-gadget.sh"

# Install GTK frontend to user's local bin (if present in payload)
if [ -f "$TMPDIR_EXTRACT/steamdeckcontroller" ]; then
    mkdir -p "$USER_BIN"
    install -m 0755 "$TMPDIR_EXTRACT/steamdeckcontroller" "$USER_BIN/steamdeckcontroller"
    chown "$REAL_USER:$REAL_USER" "$USER_BIN/steamdeckcontroller"
    echo "  $USER_BIN/steamdeckcontroller"
fi

# Generate systemd service files with paths baked in
mkdir -p /etc/systemd/system

cat > /etc/systemd/system/steamdeckcontroller-prepare.service << EOF
[Unit]
Description=Prepare Steam Deck Controller USB gadget support
DefaultDependencies=no
After=sysinit.target
Before=multi-user.target

[Service]
Type=oneshot
ExecStart=$DAEMON_DIR/prepare-gadget.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

cat > /etc/systemd/system/steamdeckcontroller.service << EOF
[Unit]
Description=Steam Deck Controller USB Passthrough Daemon
After=multi-user.target steamdeckcontroller-prepare.service
Wants=steamdeckcontroller-prepare.service

[Service]
Type=simple
ExecStart=$DAEMON_DIR/steamdeckcontrollerd
Restart=on-failure
RuntimeDirectory=steamdeckcontroller
RuntimeDirectoryMode=0755
User=root
Group=root

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable steamdeckcontroller-prepare.service
systemctl enable steamdeckcontroller.service

echo ""
echo "Done. Services enabled."
echo ""
echo "Make sure $USER_BIN is on PATH for the $REAL_USER account."
echo "Add to ~/.bash_profile or ~/.profile if needed:"
echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""

if [ "$NEEDS_REBOOT" = true ]; then
    echo ""
    echo "A reboot is required for the group membership to take effect."
    echo "Rebooting in 10 seconds — press Ctrl+C to cancel."
    for i in 10 9 8 7 6 5 4 3 2 1; do
        printf "  %d...\n" "$i"
        sleep 1
    done
    reboot
else
    echo ""
    echo "Start now with:"
    echo "  sudo systemctl start steamdeckcontroller-prepare.service"
    echo "  sudo systemctl start steamdeckcontroller.service"
fi

exit 0
__PAYLOAD__
INSTALLER_END

# ---- append base64-encoded payload after the exit marker ----
base64 < "$STAGE/payload.tar.gz" >> "$OUTPUT"

chmod +x "$OUTPUT"

# ---- generate companion uninstall script ----
UNINSTALL="$DIST_DIR/steamdeckcontroller-uninstall.sh"

cat > "$UNINSTALL" << 'UNINSTALL_END'
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
UNINSTALL_END

chmod +x "$UNINSTALL"

echo "Created: $OUTPUT"
echo "Created: $UNINSTALL"
echo ""
echo "Transfer both files to the Steam Deck."
echo "Install:   sudo ./steamdeckcontroller-install.sh"
echo "Uninstall: sudo ./steamdeckcontroller-uninstall.sh"
