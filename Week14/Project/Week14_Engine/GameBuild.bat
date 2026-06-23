@echo off
setlocal

set SOLUTION_DIR=%~dp0
set GAME_DIR=%SOLUTION_DIR%GameBuild

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -property installationPath`) do set VS_PATH=%%i
if not defined VS_PATH (
    echo ERROR: Visual Studio installation not found.
    pause
    exit /b 1
)
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -no_logo
set "PATH=%PATH%"

echo ============================================
echo  Game Build Script
echo ============================================

echo.
echo [1/2] Building Game x64...
"%SOLUTION_DIR%Scripts\python\python.exe" "%SOLUTION_DIR%Scripts\RunProcessCleanEnv.py" msbuild "%SOLUTION_DIR%KraftonEngine.sln" /p:Configuration=Game /p:Platform=x64 /m /nr:false /v:minimal
if %ERRORLEVEL% neq 0 (
    echo BUILD FAILED
    pause
    exit /b 1
)

echo.
echo [2/2] Packaging Game build...
powershell -NoProfile -ExecutionPolicy Bypass -File "%SOLUTION_DIR%Scripts\PackageGame.ps1" -RootDir "%SOLUTION_DIR%." -Configuration Game -OutputDir "%GAME_DIR%" -ProductName "KraftonEngine"
if %ERRORLEVEL% neq 0 (
    echo PACKAGE FAILED
    pause
    exit /b 1
)

echo.
echo ============================================
echo  Build complete: %GAME_DIR%
echo ============================================
echo.
echo  GameBuild/
echo    Play.bat
echo    PackageManifest.json
echo    BuildInfo.txt
echo    Bin/
echo      KraftonEngine.exe + *.dll
echo    Shaders/
echo    Content/
echo    Settings/
echo.
pause
