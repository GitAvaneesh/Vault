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

REM Find Visual Studio
call :FindVS

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

REM ============================================================================
REM Find Visual Studio installation
REM ============================================================================
:FindVS
REM Try VS 2022 first
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
    goto :eof
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
    goto :eof
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
    goto :eof
)

REM Try VS 2019
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
    goto :eof
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" %ARCH%
    goto :eof
)

echo ERROR: Visual Studio not found. Please install VS 2019 or 2022 with C++ workload.
exit /b 1

:error
exit /b 1
