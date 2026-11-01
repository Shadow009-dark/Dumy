# install_dependencies_windows.ps1
# Checks/installs everything DUMY needs on Windows.
# Run:  powershell -ExecutionPolicy Bypass -File scripts\install_dependencies_windows.ps1
$ErrorActionPreference = "Continue"

Write-Host "=== DUMY - Windows dependency check ===" -ForegroundColor Cyan

function Have-Cmd($name) { return [bool](Get-Command $name -ErrorAction SilentlyContinue) }

# 1) winget
if (-not (Have-Cmd "winget")) {
    Write-Warning "winget not found. Install 'App Installer' from the Microsoft Store, or install tools manually."
}

# 2) Visual Studio with the C++ workload (compiler + CMake + vcpkg)
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath
}
if (-not $vsPath) {
    Write-Host "Visual Studio with C++ workload not found." -ForegroundColor Yellow
    Write-Host "Install it with:" -ForegroundColor Yellow
    Write-Host "  winget install --id Microsoft.VisualStudio.2022.Community --override `"--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive`""
    Write-Host "Re-run this script afterwards."
} else {
    Write-Host "Visual Studio found: $vsPath"
    $vcpkg = Join-Path $vsPath "VC\vcpkg\vcpkg.exe"
    if (Test-Path $vcpkg) { Write-Host "vcpkg found: $vcpkg" }
    else { Write-Warning "vcpkg not found inside VS. Install: git clone https://github.com/microsoft/vcpkg C:\vcpkg && C:\vcpkg\bootstrap-vcpkg.bat" }
    $cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $cmake) { Write-Host "CMake found: $cmake" }
    elseif (Have-Cmd "cmake") { Write-Host "CMake found: $(cmake --version | Select-Object -First 1)" }
    else { Write-Warning "CMake not found. winget install Kitware.CMake" }
}

# 3) Ollama
if (Have-Cmd "ollama") {
    Write-Host "Ollama found: $(ollama --version 2>$null)"
} else {
    Write-Warning "Ollama not found - install from https://ollama.com/download"
}

# 4) Docker (optional, only for SearXNG web search)
if (Have-Cmd "docker") { Write-Host "Docker found (optional: SearXNG)." }
else { Write-Host "Docker not found (optional). Web search falls back to Wikipedia without SearXNG." }

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Green
Write-Host "  1) powershell -File scripts\pull_models.ps1"
Write-Host "  2) cmake -S . -B build -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_TOOLCHAIN_FILE=`"<VS>\VC\vcpkg\scripts\buildsystems\vcpkg.cmake`""
Write-Host "  3) cmake --build build --config Release"
Write-Host "  4) build\Release\dumy.exe --ingest   then   build\Release\dumy.exe --console"
