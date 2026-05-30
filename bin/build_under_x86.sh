#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR="$REPO_DIR/build"
DIST_DIR="$REPO_DIR/dist"

cd "$REPO_DIR"

echo "==> Configuring..."
cmake -S . -B "$BUILD_DIR"

echo "==> Building..."
cmake --build "$BUILD_DIR"

echo "==> Copying binaries to dist/..."
mkdir -p "$DIST_DIR"

cp "$BUILD_DIR/steamdeckcontrollerd" "$DIST_DIR/steamdeckcontrollerd"
echo "  dist/steamdeckcontrollerd"

if [ -x "$BUILD_DIR/steamdeckcontroller" ]; then
    cp "$BUILD_DIR/steamdeckcontroller" "$DIST_DIR/steamdeckcontroller"
    echo "  dist/steamdeckcontroller"
else
    echo "  dist/steamdeckcontroller  (skipped — GTK frontend not built)"
fi

echo ""
echo "==> Packaging self-extracting installer..."
"$SCRIPT_DIR/make-installer.sh"

echo ""
echo "Done. Run the installer with:"
echo "  sudo ./steamdeckcontroller-install.sh"
