[CmdletBinding()]
param(
    [string]$SymbolStore = "C:\symbols",

    [int]$Port = 8080,

    [string]$BindAddress = "0.0.0.0",

    [string]$PythonPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptRoot "..\..")).Path

function Resolve-Python {
    if ($PythonPath) {
        if (!(Test-Path -LiteralPath $PythonPath)) {
            throw "PythonPath does not exist: $PythonPath"
        }
        return (Resolve-Path -LiteralPath $PythonPath).Path
    }

    $BundledPython = Join-Path $RepoRoot "Scripts\python\python.exe"
    if (Test-Path -LiteralPath $BundledPython) {
        return (Resolve-Path -LiteralPath $BundledPython).Path
    }

    $PythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($PythonCommand) {
        return $PythonCommand.Source
    }

    throw "Could not find Python. Pass -PythonPath or keep Scripts\python\python.exe available."
}

function Get-IPv4Addresses {
    $Addresses = New-Object System.Collections.Generic.List[string]
    foreach ($Item in Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue) {
        if ($Item.IPAddress -eq "127.0.0.1") {
            continue
        }
        if ($Item.PrefixOrigin -eq "WellKnown") {
            continue
        }
        $Addresses.Add($Item.IPAddress)
    }
    return $Addresses
}

if (!(Test-Path -LiteralPath $SymbolStore)) {
    New-Item -ItemType Directory -Force -Path $SymbolStore | Out-Null
}

$ResolvedStore = (Resolve-Path -LiteralPath $SymbolStore).Path
$Python = Resolve-Python

Write-Host "Symbol store HTTP server"
Write-Host "  Root : $ResolvedStore"
Write-Host "  Bind : $BindAddress"
Write-Host "  Port : $Port"
Write-Host ""
Write-Host "Local URL:"
Write-Host "  http://localhost:$Port/"

$IPs = @(Get-IPv4Addresses)
if ($IPs.Count -gt 0) {
    Write-Host ""
    Write-Host "Team URLs:"
    foreach ($IP in $IPs) {
        Write-Host "  http://$IP`:$Port/"
    }
}

Write-Host ""
Write-Host "Visual Studio symbol path example:"
Write-Host "  srv*C:\SymbolsCache*http://localhost:$Port"
Write-Host ""
Write-Host "Press Ctrl+C to stop the server."
Write-Host ""

& $Python -m http.server $Port --bind $BindAddress --directory $ResolvedStore
