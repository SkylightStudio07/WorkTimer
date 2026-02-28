@echo off
echo ========================================
echo   WorkTimer Build Script
echo ========================================
echo.

set WX_DIR=C:/wxWidgets-3.2.4
set NSIS_EXE=C:\Program Files (x86)\NSIS\makensis.exe

echo [1/3] CMake configure...
cmake -S "%~dp0" -B "%~dp0build" -G "Visual Studio 17 2022" -A x64 -DwxWidgets_ROOT_DIR="%WX_DIR%"
if %ERRORLEVEL% NEQ 0 ( echo [ERROR] CMake failed. & pause & exit /b 1 )

echo [2/3] Building...
cmake --build "%~dp0build" --config Release
if %ERRORLEVEL% NEQ 0 ( echo [ERROR] Build failed. & pause & exit /b 1 )

echo [OK] Build done: %~dp0build\bin\Release\WorkTimer.exe

echo [3/3] Creating installer...
if exist "%NSIS_EXE%" (
    "%NSIS_EXE%" "%~dp0installer\WorkTimer.nsi"
) else (
    echo [WARN] NSIS not found. Skipping installer.
)

echo.
echo Done!
pause