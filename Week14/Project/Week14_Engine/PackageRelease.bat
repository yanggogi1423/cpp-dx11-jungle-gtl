@echo off
setlocal EnableExtensions EnableDelayedExpansion

set SOLUTION_DIR=%~dp0
set PROJECT_DIR=%SOLUTION_DIR%KraftonEngine
set RELEASE_DIR=%SOLUTION_DIR%ReleaseBuild
set BUILD_INFO_FILE=%PROJECT_DIR%\Source\Engine\Platform\BuildInfo.h
set VERSION_NAME=
set DRY_RUN=
set SKIP_SYMBOLS=
set NO_PAUSE=
set LAUNCH_SMOKE=
set LAUNCH_SMOKE_TIMEOUT=
set PRODUCT_NAME=KraftonEngine_Team7
set SYMBOL_PATH=srv*C:\SymbolCache*\\SYMBOL-SERVER\Symbols\Team7
set BUILD_INFO_SYMBOL_PATH=srv*C:\\SymbolCache*\\\\SYMBOL-SERVER\\Symbols\\Team7

:ParseArgs
if "%~1"=="" goto ArgsDone
if /I "%~1"=="--dry-run" (
    set DRY_RUN=1
    shift
    goto ParseArgs
)
if /I "%~1"=="--skip-symbols" (
    set SKIP_SYMBOLS=1
    shift
    goto ParseArgs
)
if /I "%~1"=="--no-pause" (
    set NO_PAUSE=1
    shift
    goto ParseArgs
)
if /I "%~1"=="--launch-smoke" (
    set LAUNCH_SMOKE=1
    shift
    goto ParseArgs
)
if /I "%~1"=="--launch-smoke-timeout" (
    shift
    if "%~1"=="" (
        echo ERROR: --launch-smoke-timeout requires a value.
        goto :Fail
    )
    set LAUNCH_SMOKE_TIMEOUT=%~1
    shift
    goto ParseArgs
)
if not defined VERSION_NAME set VERSION_NAME=%~1
shift
goto ParseArgs

:ArgsDone
if "%VERSION_NAME%"=="" (
    for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format 'yyyyMMdd_HHmm'"`) do set DEFAULT_VERSION_NAME=%%i
    if defined NO_PAUSE (
        set VERSION_NAME=!DEFAULT_VERSION_NAME!
    ) else (
        echo No VersionName was provided.
        echo Press Enter to use the default version name: !DEFAULT_VERSION_NAME!
        set /p VERSION_NAME=VersionName:
        if "!VERSION_NAME!"=="" set VERSION_NAME=!DEFAULT_VERSION_NAME!
    )
)

set GIT_COMMIT=Unknown
for /f %%i in ('git -C "%SOLUTION_DIR%." rev-parse --short HEAD 2^>nul') do set GIT_COMMIT=%%i

set BUILD_TIME=Unknown
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format 'yyyy-MM-dd HH:mm:ss'"`) do set BUILD_TIME=%%i

echo ============================================
echo  Package Release
echo  Version: %VERSION_NAME%
if defined DRY_RUN echo  Mode: Dry Run
if defined LAUNCH_SMOKE echo  Launch Smoke: Enabled
echo ============================================

echo.
echo [1/5] Generating build metadata...
echo Config: Release
echo Version: %VERSION_NAME%
echo Commit: %GIT_COMMIT%
echo BuildTime: %BUILD_TIME%

(
echo #pragma once
echo.
echo namespace BuildInfo
echo {
echo 	inline constexpr const char* ProductName = "%PRODUCT_NAME%";
echo 	inline constexpr const char* BuildConfig = "Release";
echo 	inline constexpr const char* BuildVersion = "%VERSION_NAME%";
echo 	inline constexpr const char* GitCommit = "%GIT_COMMIT%";
echo 	inline constexpr const char* SymbolPath = "%BUILD_INFO_SYMBOL_PATH%";
echo 	inline constexpr const char* BuildTime = "%BUILD_TIME%";
echo }
) > "%BUILD_INFO_FILE%"

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -property installationPath`) do set VS_PATH=%%i
if not defined VS_PATH (
    echo ERROR: Visual Studio installation not found.
    goto :Fail
)
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -no_logo

echo.
echo [2/5] Building Release x64...
msbuild "%SOLUTION_DIR%KraftonEngine.sln" /p:Configuration=Release /p:Platform=x64 /m /nr:false /v:minimal
if errorlevel 1 goto :Fail

echo.
echo [3/5] Packaging runtime files...
set PACKAGE_EXTRA_ARGS=
if defined LAUNCH_SMOKE set PACKAGE_EXTRA_ARGS=!PACKAGE_EXTRA_ARGS! -LaunchSmokeTest
if defined LAUNCH_SMOKE_TIMEOUT set PACKAGE_EXTRA_ARGS=!PACKAGE_EXTRA_ARGS! -LaunchSmokeTimeoutSeconds !LAUNCH_SMOKE_TIMEOUT!
if defined DRY_RUN (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SOLUTION_DIR%Scripts\PackageGame.ps1" -RootDir "%SOLUTION_DIR%." -Configuration Release -OutputDir "%RELEASE_DIR%" -ProductName "%PRODUCT_NAME%" -VersionName "%VERSION_NAME%" -GitCommit "%GIT_COMMIT%" -BuildTime "%BUILD_TIME%" -DryRun !PACKAGE_EXTRA_ARGS!
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SOLUTION_DIR%Scripts\PackageGame.ps1" -RootDir "%SOLUTION_DIR%." -Configuration Release -OutputDir "%RELEASE_DIR%" -ProductName "%PRODUCT_NAME%" -VersionName "%VERSION_NAME%" -GitCommit "%GIT_COMMIT%" -BuildTime "%BUILD_TIME%" !PACKAGE_EXTRA_ARGS!
)
if errorlevel 1 goto :Fail

if defined DRY_RUN goto :Success

echo.
echo [4/5] Uploading symbols...
if defined SKIP_SYMBOLS (
    echo Symbol upload skipped.
) else (
    call "%SOLUTION_DIR%UploadSymbols.bat" "%VERSION_NAME%" --no-pause
    if errorlevel 1 goto :Fail
)

echo.
echo [5/5] Package finished.

:Success
echo.
echo ============================================
if defined DRY_RUN (
    echo  Package dry run complete.
) else (
    echo  Package complete: %RELEASE_DIR%
)
echo ============================================
echo.
if not defined NO_PAUSE pause

endlocal
exit /b 0

:Fail
echo.
echo ============================================
echo  Package failed.
echo ============================================
echo.
if not defined NO_PAUSE pause
endlocal
exit /b 1
