@echo off
setlocal enabledelayedexpansion

set "FOLDER="
if defined CUDA_PATH set "FOLDER=%CUDA_PATH%"
if not defined FOLDER if defined CUDA_PATH_V13_3 set "FOLDER=%CUDA_PATH_V13_3%"
if not defined FOLDER if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.3" set "FOLDER=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.3"
if not defined FOLDER if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2" set "FOLDER=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2"
if not defined FOLDER if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v10.0" set "FOLDER=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v10.0"

if not defined FOLDER (
    set "returnVal="
) else (
    set "returnVal=%FOLDER:\=/%"
)

( endlocal
    set "%~1=%returnVal%"
)
goto :eof
