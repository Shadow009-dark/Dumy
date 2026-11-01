#!/usr/bin/env bash
# install_dependencies_ubuntu.sh - everything DUMY needs on Ubuntu/Debian.
# Run:  bash scripts/install_dependencies_ubuntu.sh
set -euo pipefail

echo "=== DUMY - Ubuntu/Debian dependency install ==="

sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libcurl4-openssl-dev \
    nlohmann-json3-dev \
    libcpp-httplib-dev \
    libpoppler-cpp-dev

# Ollama (skip if already installed)
if ! command -v ollama >/dev/null 2>&1; then
    echo "Installing Ollama..."
    curl -fsSL https://ollama.com/install.sh | sh
else
    echo "Ollama already installed: $(ollama --version 2>/dev/null || true)"
fi

# Docker (optional, only needed for local SearXNG web search)
if ! command -v docker >/dev/null 2>&1; then
    echo "Note: Docker not found. It is OPTIONAL - needed only to run SearXNG."
    echo "      Without it, web search uses the built-in Wikipedia fallback."
fi

echo
echo "Next steps:"
echo "  1) bash scripts/pull_models.sh"
echo "  2) cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"
echo "  3) cmake --build build -j"
echo "  4) ./build/dumy --ingest && ./build/dumy --console"
