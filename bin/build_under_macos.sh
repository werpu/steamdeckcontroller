#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
DIST_DIR="$REPO_DIR/dist"
IMAGE_TAG="steamdeckcontroller-builder"

cd "$REPO_DIR"

if ! command -v docker &>/dev/null; then
    echo "Docker CLI not found. Install via: brew install docker" >&2
    exit 1
fi

if ! docker info &>/dev/null; then
    echo "==> Docker daemon not running, starting colima..."
    if ! command -v colima &>/dev/null; then
        echo "colima not found. Install via: brew install colima" >&2
        exit 1
    fi
    colima start
fi

# Prefer BuildKit (the modern builder), but fall back to the legacy builder
# when the buildx component is not installed, so the build works either way.
# Install BuildKit to silence the legacy-builder deprecation warning:
#   brew install docker-buildx
#   mkdir -p ~/.docker/cli-plugins
#   ln -sfn "$(brew --prefix)/bin/docker-buildx" ~/.docker/cli-plugins/docker-buildx
if docker buildx version &>/dev/null; then
    export DOCKER_BUILDKIT=1
else
    export DOCKER_BUILDKIT=0
fi

echo "==> Building Linux x86_64 binaries inside Docker..."
docker build \
    --platform linux/amd64 \
    -f docker/Dockerfile \
    -t "$IMAGE_TAG" \
    .

echo "==> Running tests inside Docker..."
docker run --rm --platform linux/amd64 "$IMAGE_TAG" bash -c \
    '/build/input_translation_tests && /build/control_protocol_tests'

echo "==> Extracting binaries to dist/..."
mkdir -p "$DIST_DIR"

CONTAINER=$(docker create --platform linux/amd64 "$IMAGE_TAG")
cleanup() { docker rm -f "$CONTAINER" &>/dev/null || true; }
trap cleanup EXIT

docker cp "$CONTAINER:/build/steamdeckcontrollerd" "$DIST_DIR/steamdeckcontrollerd"
echo "  dist/steamdeckcontrollerd"

if docker cp "$CONTAINER:/build/steamdeckcontroller" "$DIST_DIR/steamdeckcontroller" 2>/dev/null; then
    echo "  dist/steamdeckcontroller"
else
    echo "  dist/steamdeckcontroller  (skipped — GTK frontend not built)"
fi

echo ""
echo "==> Packaging self-extracting installer..."
"$SCRIPT_DIR/make-installer.sh"

echo ""
echo "Done. Transfer the installer to the Steam Deck and run:"
echo "  sudo ./steamdeckcontroller-install.sh"
