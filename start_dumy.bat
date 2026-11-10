@echo off
rem DUMY serveri baslat ve chat sehifesini ac.
rem Tələblər: Ollama quraşdırılıb, gemma3:4b ve bge-m3 modelleri cekilib.
chcp 65001 >nul
cd /d "%~dp0"

rem --- Ollama: bir chat modeli + bge-m3 (CPU) eyni vaxtda qalsin -------------
rem 1 olsa, bge-m3 yuklenende gemma3:4b-ni cixarir ve her sualda reload olur.
set "OLLAMA_MAX_LOADED_MODELS=2"
set "OLLAMA_KEEP_ALIVE=-1"
set "OLLAMA_FLASH_ATTENTION=1"

rem --- Port artiq istifadededirse ikinci instance acilmir --------------------
netstat -ano | findstr /R /C:"LISTENING" | findstr /C:":8085 " >nul
if not errorlevel 1 (
    echo Server artiq isleyir - http://127.0.0.1:8085/ sehifesi acilir.
    start "" "http://127.0.0.1:8085/"
    exit /b 0
)

rem --- Ollama islemir? Onu baslat -------------------------------------------
curl.exe -s --max-time 2 http://127.0.0.1:11434/api/tags >nul 2>&1
if errorlevel 1 (
    echo Ollama basladilir...
    start "Ollama" /min ollama serve
    timeout /t 3 /nobreak >nul
)

rem --- Modelleri evvelceden yukle (ilk sual yavas olmasin) -------------------
echo Modeller yuklenir: gemma3:4b (GPU) + bge-m3 (CPU)...
curl.exe -s --max-time 120 -X POST http://127.0.0.1:11434/api/generate ^
  -H "Content-Type: application/json" ^
  -d "{\"model\":\"gemma3:4b\",\"prompt\":\"salam\",\"stream\":false,\"keep_alive\":-1,\"options\":{\"num_ctx\":4096,\"num_predict\":1}}" >nul
curl.exe -s --max-time 120 -X POST http://127.0.0.1:11434/api/embed ^
  -H "Content-Type: application/json" ^
  -d "{\"model\":\"bge-m3\",\"input\":\"salam\",\"keep_alive\":-1,\"options\":{\"num_gpu\":0}}" >nul

rem --- Serveri ise sal ------------------------------------------------------
set "EXE=build\Release\dumy.exe"
if not exist "%EXE%" set "EXE=build\audit\Release\dumy.exe"
if not exist "%EXE%" (
    echo [XETA] dumy.exe tapilmadi. Evvelce layiheni qurun:
    echo   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=%%VCPKG_ROOT%%\scripts\buildsystems\vcpkg.cmake
    echo   cmake --build build --config Release
    pause
    exit /b 1
)

echo DUMY basladilir... (brauzer avtomatik acilacaq)
"%EXE%"
if errorlevel 1 (
    echo.
    echo [XETA] Server bitti xeta ile. Port 8085 ve ya Ollama bagli ola biler.
    echo        Log: data\dumy.log
    pause
)
