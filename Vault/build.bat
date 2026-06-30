@echo off
REM ============================================================================
REM Vault - Build Script for Windows
REM ============================================================================
REM Prerequisites:
REM   1. Visual Studio 2022 with C++ Desktop workload
REM   2. Windows SDK 10.0.19041.0 or later
REM   3. WebView2 SDK (via NuGet or manual install)
REM
REM Usage:
REM   build.bat [debug|release] [x86|x64]
REM ============================================================================

setlocal enabledelayedexpansion

REM Default configuration
set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=release

set ARCH=%2
if "%ARCH%"=="" set ARCH=x64

REM Find and setup Visual Studio - try multiple methods
set VSCMD_FOUND=0

REM Method 1: Check if already in Developer Command Prompt (cl.exe available)
where cl.exe >nul 2>nul
if %errorlevel%==0 (
    echo [INFO] cl.exe already available in PATH
    set VSCMD_FOUND=1
    goto :BuildStart
)

REM Method 2: Try to find VS installation and call vcvarsall.bat
echo [INFO] Searching for Visual Studio...

REM VS 2022 Community
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [INFO] Found VS 2022 Community
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
    if %errorlevel%==0 set VSCMD_FOUND=1
    goto :BuildStart
)

REM VS 2022 Professional
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [INFO] Found VS 2022 Professional
    call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
    if %errorlevel%==0 set VSCMD_FOUND=1
    goto :BuildStart
)

REM VS 2022 Enterprise
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [INFO] Found VS 2022 Enterprise
    call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
    if %errorlevel%==0 set VSCMD_FOUND=1
    goto :BuildStart
)

REM VS 2019 Community
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [INFO] Found VS 2019 Community
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
    if %errorlevel%==0 set VSCMD_FOUND=1
    goto :BuildStart
)

REM Method 3: Use vswhere.exe if available
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo [INFO] Using vswhere to find VS...
    for /f "usebackq tokens=* delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set VS_INSTALL_DIR=%%i
    )
    if defined VS_INSTALL_DIR (
        if exist "!VS_INSTALL_DIR!\VC\Auxiliary\Build\vcvarsall.bat" (
            echo [INFO] Found VS at !VS_INSTALL_DIR!
            call "!VS_INSTALL_DIR!\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
            if %errorlevel%==0 set VSCMD_FOUND=1
        )
    )
)

:BuildStart
if %VSCMD_FOUND%==0 (
    echo.
    echo ERROR: Visual Studio not found or could not initialize.
    echo.
    echo Please try one of these solutions:
    echo   1. Open "Developer Command Prompt for VS 2022" from Start Menu
    echo   2. Run this script from within that prompt
    echo   3. Install Visual Studio 2022 with "Desktop development with C++" workload
    echo.
    exit /b 1
)

REM Verify cl.exe is now available
where cl.exe >nul 2>nul
if %errorlevel% neq 0 (
    echo.
    echo ERROR: cl.exe still not found after setting up VS environment.
    echo Your PATH may be corrupted. Try restarting and using Developer Command Prompt.
    exit /b 1
)

echo [INFO] Visual Studio environment initialized successfully
echo.

REM Set up paths
set VAULT_DIR=%~dp0
set SHARED_DIR=%VAULT_DIR%Shared
set SERVICE_DIR=%VAULT_DIR%VaultService
set UI_DIR=%VAULT_DIR%VaultUI
set OUTPUT_DIR=%VAULT_DIR%build\%ARCH%\%BUILD_TYPE%

REM Create output directory
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo.
echo ============================================================================
echo Building Vault (%BUILD_TYPE%, %ARCH%)
echo ============================================================================
echo.

REM Compiler flags
set CPPFLAGS=/EHsc /std:c++17 /W4 /MP /I"%SHARED_DIR%" /I"%WindowsSdkDir%Include\%WindowsSDKVersion%\um" /I"%WindowsSdkDir%Include\%WindowsSDKVersion%\shared" /I"%WindowsSdkDir%Include\%WindowsSDKVersion%\winrt"

if /i "%BUILD_TYPE%"=="debug" (
    set CPPFLAGS=%CPPFLAGS% /Zi /Od /D_DEBUG
    set LDFLAGS=/DEBUG
) else (
    set CPPFLAGS=%CPPFLAGS% /O2 /DNDEBUG
    set LDFLAGS=
)

REM Linker libraries
set LIBS=kernel32.lib user32.lib shell32.lib advapi32.lib crypt32.lib bcrypt.lib ole32.lib oleaut32.lib comctl32.lib

echo [1/3] Building VaultService...
echo.

REM Compile VaultService sources
cl.exe %CPPFLAGS% ^
    "%SERVICE_DIR%\SuspendEngine.cpp" ^
    "%SERVICE_DIR%\DetectionEngine.cpp" ^
    "%SERVICE_DIR%\ConfigStore.cpp" ^
    "%SERVICE_DIR%\ProcessWatcher.cpp" ^
    "%SERVICE_DIR%\IPCServer.cpp" ^
    "%SERVICE_DIR%\main.cpp" ^
    /Fo"%OUTPUT_DIR%\ServiceObj\\" ^
    /Fe"%OUTPUT_DIR%\VaultService.exe" ^
    /link %LDFLAGS% %LIBS%

if errorlevel 1 (
    echo.
    echo ERROR: VaultService build failed!
    goto :error
)

echo [2/3] Building VaultUI...
echo.

REM Check for WebView2 SDK
set WEBVIEW2_INCLUDE=
if exist "%VAULT_DIR%packages\Microsoft.Web.WebView2.*\build\native\include" (
    for /d %%i in ("%VAULT_DIR%packages\Microsoft.Web.WebView2.*") do set WEBVIEW2_INCLUDE=%%i\build\native\include
)

if not defined WEBVIEW2_INCLUDE (
    echo WARNING: WebView2 SDK not found. Building without WebViewHost.
    echo          Install via: nuget install Microsoft.Web.WebView2
    echo.
)

REM Compile VaultUI sources
if defined WEBVIEW2_INCLUDE (
    cl.exe %CPPFLAGS% /I"%WEBVIEW2_INCLUDE%" ^
        "%UI_DIR%\IPCClient.cpp" ^
        "%UI_DIR%\WebViewHost.cpp" ^
        "%UI_DIR%\main.cpp" ^
        /Fo"%OUTPUT_DIR%\UIObj\\" ^
        /Fe"%OUTPUT_DIR%\VaultUI.exe" ^
        /link %LDFLAGS% %LIBS%
) else (
    REM Build without WebViewHost (will need manual integration)
    cl.exe %CPPFLAGS% ^
        "%UI_DIR%\IPCClient.cpp" ^
        "%UI_DIR%\main.cpp" ^
        /Fo"%OUTPUT_DIR%\UIObj\\" ^
        /Fe"%OUTPUT_DIR%\VaultUI.exe" ^
        /link %LDFLAGS% %LIBS%
)

if errorlevel 1 (
    echo.
    echo ERROR: VaultUI build failed!
    goto :error
)

echo [3/3] Copying UI assets...
echo.

if not exist "%OUTPUT_DIR%\ui" mkdir "%OUTPUT_DIR%\ui"
copy /Y "%UI_DIR%\ui\vault_ui_v2.html" "%OUTPUT_DIR%\ui\" >nul

echo.
echo ============================================================================
echo Build completed successfully!
echo ============================================================================
echo.
echo Output files:
echo   - %OUTPUT_DIR%\VaultService.exe
echo   - %OUTPUT_DIR%\VaultUI.exe
echo   - %OUTPUT_DIR%\ui\vault_ui_v2.html
echo.
echo To install the service (run as Administrator):
echo   %OUTPUT_DIR%\VaultService.exe install
echo.
echo To run in debug mode:
echo   %OUTPUT_DIR%\VaultService.exe run
echo   %OUTPUT_DIR%\VaultUI.exe
echo.

goto :eof

:error
exit /b 1
