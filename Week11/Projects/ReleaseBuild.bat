@echo off
setlocal EnableExtensions

set "SOLUTION_DIR=%~dp0"
set "PROJECT_DIR=%SOLUTION_DIR%JSEngine"
set "BUILD_OUTPUT=%PROJECT_DIR%\Bin\Release"
set "RELEASE_DIR=%SOLUTION_DIR%ReleaseBuild"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (
    echo Visual Studio installation was not found.
    pause
    exit /b 1
)

set "MSBUILD_EXE=%VS_PATH%\MSBuild\Current\Bin\amd64\MSBuild.exe"
if not exist "%MSBUILD_EXE%" set "MSBUILD_EXE=%VS_PATH%\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD_EXE%" (
    echo MSBuild.exe was not found under Visual Studio installation.
    pause
    exit /b 1
)

rem Some shells can pass both Path and PATH into child processes. MSBuild/CL
rem treats that as a duplicate environment key, so normalize it in this process.
set "PATH="
set "Path="
set "Path=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;%SystemRoot%\System32\WindowsPowerShell\v1.0\"

echo ============================================
echo  Editor Release Build
echo ============================================

echo.
echo [1/3] Building Release x64...
"%MSBUILD_EXE%" "%SOLUTION_DIR%JSEngine.sln" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if errorlevel 1 (
    echo BUILD FAILED
    pause
    exit /b 1
)

echo.
echo [2/3] Preparing output directory...
if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%"

echo.
echo [3/3] Copying editor runtime data...

call :CopyFile "%BUILD_OUTPUT%\JSEngine.exe" "%RELEASE_DIR%" || goto :Fail
call :CopyOptionalFile "%BUILD_OUTPUT%\JSEngine.pdb" "%RELEASE_DIR%" || goto :Fail

for %%F in ("%BUILD_OUTPUT%\*.dll") do (
    if exist "%%~fF" (
        call :CopyFile "%%~fF" "%RELEASE_DIR%" || goto :Fail
    )
)

call :CopyOptionalFile "%PROJECT_DIR%\imgui.ini" "%RELEASE_DIR%" || goto :Fail

call :CopyDir "%PROJECT_DIR%\Shaders" "%RELEASE_DIR%\Shaders" || goto :Fail
call :CopyOptionalDir "%PROJECT_DIR%\DerivedData\ShaderCache" "%RELEASE_DIR%\DerivedData\ShaderCache" || goto :Fail
call :CopyDir "%PROJECT_DIR%\Asset" "%RELEASE_DIR%\Asset" || goto :Fail
call :CopyDir "%PROJECT_DIR%\Settings" "%RELEASE_DIR%\Settings" || goto :Fail
call :CopyDir "%PROJECT_DIR%\Resources" "%RELEASE_DIR%\Resources" || goto :Fail
call :CopyDir "%PROJECT_DIR%\LuaDefinitions" "%RELEASE_DIR%\LuaDefinitions" || goto :Fail
call :CopyOptionalDir "%PROJECT_DIR%\Saves" "%RELEASE_DIR%\Saves" || goto :Fail

echo.
echo ============================================
echo  Build complete: %RELEASE_DIR%
echo ============================================
echo.
echo  ReleaseBuild/
echo    JSEngine.exe
echo    JSEngine.pdb
echo    *.dll
echo    imgui.ini
echo    Shaders/
echo    DerivedData/ShaderCache/
echo    Asset/
echo    Settings/
echo    Resources/
echo    LuaDefinitions/
echo    Saves/
echo.
pause
exit /b 0

:Fail
echo RELEASE PACKAGING FAILED
pause
exit /b 1

:CopyFile
if not exist "%~1" (
    echo Required file missing: %~1
    exit /b 1
)
copy /y "%~1" "%~2\" >nul
if errorlevel 1 exit /b 1
exit /b 0

:CopyOptionalFile
if exist "%~1" copy /y "%~1" "%~2\" >nul
if errorlevel 1 exit /b 1
exit /b 0

:CopyDir
if not exist "%~1\" (
    echo Required directory missing: %~1
    exit /b 1
)
xcopy "%~1" "%~2\" /e /i /y /q >nul
if errorlevel 1 exit /b 1
exit /b 0

:CopyOptionalDir
if exist "%~1\" (
    xcopy "%~1" "%~2\" /e /i /y /q >nul
    if errorlevel 1 exit /b 1
)
exit /b 0
