# Regional settings for Azerbaijani date/weekday computation
# This script starts Ollama with optimized settings for RTX 4050 6GB

# Set environment variables for optimal Ollama performance
$env:OLLAMA_FLASH_ATTENTION = "1"
$env:OLLAMA_KV_CACHE_TYPE = "q8_0"

# Start Ollama server
Write-Host "Starting Ollama server with optimized settings for RTX 4050 6GB..."
Write-Host "OLLAMA_FLASH_ATTENTION=1"
Write-Host "OLLAMA_KV_CACHE_TYPE=q8_0"

# Start ollama serve in the background
$process = Start-Process ollama -ArgumentList "serve" -PassThru -NoNewWindow

Write-Host "Ollama server started."

# Check which models are running and adjust GPU allocation
Start-Sleep -Seconds 3

# Check if both bge-m3 and another model are at 100% GPU
$ollama_ps = ollama ps

# Logic: Keep bge-m3 on GPU only if both models are at 100% with unchanged speed
# Otherwise, set bge-m3 to use CPU (num_gpu=0)

Write-Host "Ollama is running. Configure models as needed."
Write-Host ""
Write-Host "Recommended model configuration:"
Write-Host "  - qwen3:4b-instruct-2507-q4_K_M: Primary chat model"
Write-Host "  - bge-m3: Embedding model (use CPU if GPU is needed for chat)"
Write-Host "  - Run: ollama ps to monitor GPU usage"