@echo off
rem DUMY serveri baslat ve chat sehifesini ac (qovlugun yerini avtomatik tapir).
rem Port 8085 artiq istifadededirse ikinci instance acilmir - sadeca sehife acilir.
chcp 65001 >nul
cd /d "%~dp0"

netstat -ano | findstr /R /C:"LISTENING" | findstr /C:":8085 " >nul
if not errorlevel 1 (
    echo Server artiq isleyir - http://127.0.0.1:8085/ sehifesi acilir.
    start "" "http://127.0.0.1:8085/"
    exit /b 0
)

set "EXE=build\Release\dumy.exe"
if not exist "%EXE%" set "EXE=build\dumy.exe"
if not exist "%EXE%" (
    echo [XETA] dumy.exe tapilmadi. Evvelce layiheni qurun:
    echo   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
    echo   cmake --build build --config Release
    pause
    exit /b 1
)

echo DUMY basladilir... ( brauzer avtomatik acilacaq )
"%EXE%"
if errorlevel 1 (
    echo.
    echo [XETA] Server bitti xeta ile. Port 8085 bagli ola biler.
    pause
)
