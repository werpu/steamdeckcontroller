#!/usr/bin/env bash
set -euo pipefail

# Installs all Homebrew dependencies needed to build steamdeckcontroller on macOS.
#
# Two build paths are supported:
#   bin/build_for_macos.sh    — native build + unit tests (needs cmake + Xcode CLT)
#   bin/build_under_macos.sh  — Linux x86_64 cross-build via Docker
#                               (needs docker, colima, docker-buildx/BuildKit)

if ! command -v brew &>/dev/null; then
    echo "Homebrew not found. Install it first: https://brew.sh" >&2
    exit 1
fi

echo "==> Installing Homebrew packages..."
# cmake          — native build (build_for_macos.sh)
# docker         — Docker CLI client
# colima         — lightweight Docker daemon (replaces Docker Desktop)
# docker-buildx  — BuildKit builder (avoids the deprecated legacy builder)
brew install cmake docker colima docker-buildx

echo "==> Linking docker-buildx as a Docker CLI plugin..."
# brew installs the binary but does not wire it up as a CLI plugin, so
# `docker buildx` / BuildKit are unavailable until this symlink exists.
mkdir -p "$HOME/.docker/cli-plugins"
ln -sfn "$(brew --prefix)/bin/docker-buildx" "$HOME/.docker/cli-plugins/docker-buildx"

echo "==> Verifying..."
cmake --version | head -1
docker --version
docker buildx version

echo ""
echo "All build dependencies installed."
echo ""
echo "Next steps:"
echo "  colima start                 # start the Docker daemon (once per session)"
echo "  bin/build_for_macos.sh       # native build + run unit tests (no Docker)"
echo "  bin/build_under_macos.sh     # cross-compile Linux binaries + build installer"
