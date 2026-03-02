@echo off
echo ========================================
echo   WorkTimer v0.1.0 - Release Builder
echo ========================================
echo.

set VERSION=0.1.0
set NSIS_EXE=C:\Program Files (x86)\NSIS\makensis.exe
set RELEASE_DIR=%~dp0release
set BUILD_EXE=%~dp0build\bin\Release\WorkTimer.exe
set ICO=%~dp0resources\app.ico

if not exist "%BUILD_EXE%" (
    echo [ERROR] WorkTimer.exe not found.
    echo   Run cmake build first.
    pause & exit /b 1
)

if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%"

echo [1/2] Creating portable zip...
set PORTABLE_DIR=%RELEASE_DIR%\WorkTimer_portable
if exist "%PORTABLE_DIR%" rmdir /s /q "%PORTABLE_DIR%"
mkdir "%PORTABLE_DIR%"
copy "%BUILD_EXE%" "%PORTABLE_DIR%\WorkTimer.exe"
copy "%ICO%"       "%PORTABLE_DIR%\app.ico"

powershell -Command "Compress-Archive -Path '%PORTABLE_DIR%\*' -DestinationPath '%RELEASE_DIR%\WorkTimer_v%VERSION%_portable.zip' -Force"
rmdir /s /q "%PORTABLE_DIR%"
echo [OK] %RELEASE_DIR%\WorkTimer_v%VERSION%_portable.zip

echo.
echo [2/2] Creating installer...
if not exist "%NSIS_EXE%" (
    echo [WARN] NSIS not found: %NSIS_EXE%
    echo        Install from https://nsis.sourceforge.io
) else (
    "%NSIS_EXE%" "%~dp0installer\WorkTimer.nsi"
    if %ERRORLEVEL% EQU 0 (
        move "%~dp0installer\WorkTimer_Setup_v%VERSION%.exe" "%RELEASE_DIR%\"
        echo [OK] %RELEASE_DIR%\WorkTimer_Setup_v%VERSION%.exe
    ) else (
        echo [ERROR] Installer build failed.
    )
)

echo.
echo ========================================
echo Release files in: %RELEASE_DIR%
echo ========================================
echo   - WorkTimer_v%VERSION%_portable.zip  (no install needed)
echo   - WorkTimer_Setup_v%VERSION%.exe     (installer)
echo.
echo Upload both to GitHub Releases!
echo ========================================
pause
