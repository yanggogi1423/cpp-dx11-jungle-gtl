@echo off
cd /d "%~dp0..\.."
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-Content C:\symbols\.jse_symbol_http.log -Tail 120 -Wait"
