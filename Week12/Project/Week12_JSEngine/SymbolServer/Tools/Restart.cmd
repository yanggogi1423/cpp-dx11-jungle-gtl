@echo off
cd /d "%~dp0..\.."
Scripts\python\python.exe SymbolServer\Internal\symbol_server_local.py stop-server
Scripts\python\python.exe SymbolServer\Internal\symbol_server_local.py start-server
pause
