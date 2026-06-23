# JSEngine Symbol Server

## Overview

Symbols are published into a local `symstore` directory, then exposed to the team through a simple HTTP static server. Source files are also snapshotted into the same HTTP root so source loading does not require GitHub access on debugger PCs.

Default symbol store:

```text
C:\symbols
```

Visual Studio / WinDbg symbol path format:

```text
srv*C:\SymbolsCache*http://<host-ip>:8080
```

## Build Symbols

For the usual local workflow, double-click:

```text
SymbolServer\ServerLauncher.cmd
```

This opens the local symbol server dashboard. Use it to start or stop the server, publish symbols, and monitor recent requests.

If you want one-click build, publish, and server start without the dashboard, double-click:

```text
SymbolServer\Tools\All.cmd
```

Build the configurations that should be published:

```powershell
msbuild JSEngine.sln /p:Configuration=Release /p:Platform=x64 /m
msbuild JSEngine.sln /p:Configuration=GameClientRelease /p:Platform=x64 /m
```

Optional debug configurations:

```powershell
msbuild JSEngine.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild JSEngine.sln /p:Configuration=GameClientDebug /p:Platform=x64 /m
```

## Publish Symbols

Double-click:

```text
SymbolServer\Tools\Publish.cmd
```

Or run manually.

Publishing also copies indexed source files to:

```text
C:\symbols\src\<commit>\
```

The PDB source-server stream downloads source files from `http://<host-ip>:8080/src/<commit>/...`.

Release symbols only:

```powershell
Scripts\python\python.exe SymbolServer\Internal\symbol_server_local.py publish
```

Release and debug symbols:

```powershell
Scripts\python\python.exe SymbolServer\Internal\symbol_server_local.py publish --include-debug
```

## Serve Symbols Over HTTP

Double-click one of these on the host PC:

```text
SymbolServer\Tools\Start.cmd
SymbolServer\Tools\Stop.cmd
SymbolServer\Tools\Restart.cmd
SymbolServer\Tools\Status.cmd
SymbolServer\Tools\Log.cmd
SymbolServer\ServerLauncher.cmd
```

Or run manually:

```powershell
Scripts\python\python.exe SymbolServer\Internal\symbol_server_local.py start-server
```

The script prints local and LAN URLs. Team members use the LAN URL in their debugger symbol path.

Example:

```text
srv*C:\SymbolsCache*http://192.168.0.15:8080
```

## Visual Studio Setup

Open:

```text
Tools > Options > Debugging > Symbols
```

Add:

```text
srv*C:\SymbolsCache*http://<host-ip>:8080
```

For source server support, open:

```text
Tools > Options > Debugging > General
```

Enable:

```text
Enable source server support
```

Source server support does not require Git or GitHub authentication. Visual Studio downloads matching source files from the same HTTP server that serves PDB files.
