#!/usr/bin/env bash
set -euo pipefail

# Native macOS build — compiles and runs the portable unit tests locally.
# The Linux daemon and GTK frontend are not built (they require Linux headers
# and GTK, which are not available here).

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR="$REPO_DIR/build-macos"

cd "$REPO_DIR"

echo "==> Configuring (native macOS)..."
cmake -S . -B "$BUILD_DIR" -DBUILD_TESTING=ON

echo "==> Building test targets..."
cmake --build "$BUILD_DIR" --target input_translation_tests control_protocol_tests

echo "==> Running tests..."
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo ""
echo "All tests passed."
