@echo off
setlocal EnableExtensions
REM Usage: CmakeGenerateProjects.bat <0|1:use_cuda (default is 0)> <0|1:use_dx11 (default is 1)> <0|1:static_wincrt (default is 0)>
set EXIT_CODE=0

cd /d %~dp0

set USE_CUDA=0
set USE_DX11=1
set STATIC_WINCRT=0
if not "%~1"=="" set USE_CUDA=%~1
if not "%~2"=="" set USE_DX11=%~2
if not "%~3"=="" set STATIC_WINCRT=%~3

set GW_DEPS_ROOT=%~dp0..\..\
set OUTPUT_ROOT=%~dp0
set SAMPLES_ROOT_DIR=%~dp0
set CUDA_PATH_=
if "%USE_CUDA%"=="1" (
    call "%~dp0..\scripts\locate_cuda.bat" CUDA_PATH_
    if not defined CUDA_PATH_ goto CUDA_ROOT_UNDEFINED
)

echo GW_DEPS_ROOT = %GW_DEPS_ROOT%
echo USE_CUDA = %USE_CUDA%
echo USE_DX11 = %USE_DX11%
echo STATIC_WINCRT = %STATIC_WINCRT%

call :FindCMake
if not exist "%CMAKE%" goto CMAKE_NOT_FOUND
echo CMAKE = %CMAKE%

set CMAKE_COMMON_PARAMS=-DCUDA_TOOLKIT_ROOT_DIR="%CUDA_PATH_%" -DTARGET_BUILD_PLATFORM=windows -DNV_CLOTH_ENABLE_CUDA=%USE_CUDA% -DNV_CLOTH_ENABLE_DX11=%USE_DX11% -DSTATIC_WINCRT=%STATIC_WINCRT%

call :GenerateVS "Visual Studio 17 2022" "x64" "v143" "vs2022-v143-win64-cmake" "win.x86_64.vc143.md"
if errorlevel 1 goto End

call :GeneratorAvailable "Visual Studio 18 2026"
if errorlevel 1 (
    echo Visual Studio 18 2026 generator not available in this CMake. Skipping VS2026 generation.
) else (
    call :GenerateVS "Visual Studio 18 2026" "x64" "v143" "vs2026-v143-win64-cmake" "win.x86_64.vs2026.vc143.md"
    if errorlevel 1 goto End
)

goto End

:FindCMake
set CMAKE=
for %%C in (cmake.exe) do set CMAKE=%%~$PATH:C
if defined CMAKE goto :eof
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
if defined CMAKE goto :eof
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
goto :eof

:GeneratorAvailable
"%CMAKE%" --help | findstr /C:"%~1" >nul
exit /b %ERRORLEVEL%

:GenerateVS
set GEN_NAME=%~1
set GEN_ARCH=%~2
set GEN_TOOLSET=%~3
set BUILD_DIR=%~4
set OUT_DIR=%~5

echo Generating samples %GEN_NAME% %GEN_ARCH% %GEN_TOOLSET% in compiler\%BUILD_DIR%
if exist "compiler\%BUILD_DIR%" rmdir /s /q "compiler\%BUILD_DIR%"
mkdir "compiler\%BUILD_DIR%"
pushd "compiler\%BUILD_DIR%"
"%CMAKE%" ..\.. -G "%GEN_NAME%" -A %GEN_ARCH% -T %GEN_TOOLSET% %CMAKE_COMMON_PARAMS% -DBL_DLL_OUTPUT_DIR="%OUTPUT_ROOT%bin\%OUT_DIR%" -DBL_LIB_OUTPUT_DIR="%OUTPUT_ROOT%lib\%OUT_DIR%" -DBL_EXE_OUTPUT_DIR="%OUTPUT_ROOT%bin\%OUT_DIR%"
set EXIT_CODE=%ERRORLEVEL%
popd
exit /b %EXIT_CODE%

:CMAKE_NOT_FOUND
echo CMake was not found. Install CMake or Visual Studio CMake tools.
set EXIT_CODE=1
goto End

:CUDA_ROOT_UNDEFINED
echo CUDA was requested but CUDA_PATH could not be located.
set EXIT_CODE=1
goto End

:End
if not "%EXIT_CODE%"=="0" echo samples\CmakeGenerateProjects.bat failed with error code %EXIT_CODE%
exit /b %EXIT_CODE%

