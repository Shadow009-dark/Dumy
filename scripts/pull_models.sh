#!/usr/bin/env bash
# pull_models.sh - downloads the models DUMY uses (once).
# Run:  bash scripts/pull_models.sh
set -euo pipefail

if ! command -v ollama >/dev/null 2>&1; then
    echo "Ollama not found. Install it first: https://ollama.com/download" >&2
    exit 1
fi

echo "=== DUMY - pulling Ollama models ==="

# Main chat/reasoning model. Use qwen3:14b instead if your GPU has >= 16 GB VRAM.
ollama pull qwen3:8b
# Small fast helper: query expansion, reranking, map-reduce summaries.
ollama pull qwen3:4b
# Embeddings with good Azerbaijani support (required for indexing PDFs).
ollama pull bge-m3

echo
echo "Installed models:"
ollama list
echo "Tip: edit config.json (chat_model) if you prefer qwen3:14b."
