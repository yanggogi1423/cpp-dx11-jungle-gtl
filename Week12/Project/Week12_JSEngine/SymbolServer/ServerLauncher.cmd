@echo off
cd /d "%~dp0.."
Scripts\python\python.exe SymbolServer\Internal\symbol_server_local.py dashboard
pause
