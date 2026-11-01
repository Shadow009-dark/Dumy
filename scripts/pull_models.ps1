# pull_models.ps1 - downloads the models DUMY uses (once).
# Run:  powershell -File scripts\pull_models.ps1
$ErrorActionPreference = "Continue"

if (-not (Get-Command ollama -ErrorAction SilentlyContinue)) {
    Write-Error "Ollama not found. Install it first: https://ollama.com/download"
    exit 1
}

Write-Host "=== DUMY - pulling Ollama models ===" -ForegroundColor Cyan

# Main chat/reasoning model. Use qwen3:14b instead if your GPU has >= 16 GB VRAM.
ollama pull qwen3:8b
# Small fast helper: query expansion, reranking, map-reduce summaries.
ollama pull qwen3:4b
# Embeddings with good Azerbaijani support (required for indexing PDFs).
ollama pull bge-m3

Write-Host ""
Write-Host "Installed models:" -ForegroundColor Green
ollama list
Write-Host "Tip: edit config.json (chat_model) if you prefer qwen3:14b." -ForegroundColor Yellow
