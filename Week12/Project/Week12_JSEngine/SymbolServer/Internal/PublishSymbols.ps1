[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SymbolStore,

    [switch]$IncludeDebug,

    [string]$DebuggerToolsPath,

    [string]$ProductName = "JSEngine",

    [string]$Version,

    [switch]$SkipSourceIndex
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptRoot "..\..")).Path
$EngineRoot = Join-Path $RepoRoot "JSEngine"

function Resolve-DebugTool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $Candidates = @()
    if ($DebuggerToolsPath) {
        $Candidates += Join-Path $DebuggerToolsPath $Name
        $Candidates += Join-Path (Join-Path $DebuggerToolsPath "srcsrv") $Name
    }

    $Command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($Command) {
        $Candidates += $Command.Source
    }

    $ProgramFilesX86 = ${env:ProgramFiles(x86)}
    if ($ProgramFilesX86) {
        $DebuggersRoot = Join-Path $ProgramFilesX86 "Windows Kits\10\Debuggers"
        foreach ($Arch in @("x64", "x86", "arm64")) {
            $ArchRoot = Join-Path $DebuggersRoot $Arch
            $Candidates += Join-Path $ArchRoot $Name
            $Candidates += Join-Path (Join-Path $ArchRoot "srcsrv") $Name
        }
    }

    foreach ($Candidate in $Candidates) {
        if ($Candidate -and (Test-Path -LiteralPath $Candidate)) {
            return (Resolve-Path -LiteralPath $Candidate).Path
        }
    }

    throw "Could not find $Name. Install Debugging Tools for Windows or pass -DebuggerToolsPath."
}

function Invoke-GitText {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $Output = & git -C $RepoRoot @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed: $($Output -join "`n")"
    }

    return (($Output -join "`n").Trim())
}

function Get-RelativeGitPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $RepoFull = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd("\", "/") + [System.IO.Path]::DirectorySeparatorChar
    $FileFull = [System.IO.Path]::GetFullPath($Path)
    if (!$FileFull.StartsWith($RepoFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $null
    }

    return $FileFull.Substring($RepoFull.Length).Replace("\", "/")
}

function Get-LocalSourceBaseUrl {
    param(
        [int]$Port = 8080
    )

    $Address = Get-NetIPAddress -AddressFamily IPv4 |
        Where-Object { $_.IPAddress -ne "127.0.0.1" -and $_.PrefixOrigin -ne "WellKnown" } |
        Select-Object -First 1 -ExpandProperty IPAddress
    if (!$Address) {
        $Address = "127.0.0.1"
    }
    return "http://$Address`:$Port"
}

function ConvertTo-UrlPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    return (($RelativePath -split "/") | ForEach-Object { [System.Uri]::EscapeDataString($_) }) -join "/"
}

function Publish-SourceSnapshot {
    param(
        [Parameter(Mandatory = $true)]
        [array]$Sources,

        [Parameter(Mandatory = $true)]
        [string]$Commit,

        [Parameter(Mandatory = $true)]
        [string]$StorePath
    )

    $Copied = 0
    foreach ($Pair in $Sources) {
        $Source = $Pair.Source
        $RelativePath = $Pair.RelativePath
        $Destination = Join-Path (Join-Path (Join-Path $StorePath "src") $Commit) ($RelativePath -replace "/", "\")
        $DestinationDir = Split-Path -Parent $Destination
        New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
        $Copied += 1
    }
    return $Copied
}

function Get-PdbSourceFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PdbPath,

        [Parameter(Mandatory = $true)]
        [string]$SrcToolPath
    )

    $RawOutput = & $SrcToolPath -r $PdbPath 2>&1

    $Files = New-Object System.Collections.Generic.List[string]
    foreach ($Line in $RawOutput) {
        $Text = "$Line".Trim()
        if (!$Text) {
            continue
        }

        if ($Text -match "^[A-Za-z]:\\|^\\\\") {
            $Files.Add($Text)
        }
    }

    if ($Files.Count -eq 0 -and $LASTEXITCODE -ne 0) {
        throw "srctool failed for $PdbPath`: $($RawOutput -join "`n")"
    }

    return $Files
}

function Write-SourceServerStream {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PdbPath,

        [Parameter(Mandatory = $true)]
        [string]$StreamPath,

        [Parameter(Mandatory = $true)]
        [string]$Commit,

        [Parameter(Mandatory = $true)]
        [string]$SourceBaseUrl,

        [Parameter(Mandatory = $true)]
        [string]$StorePath,

        [Parameter(Mandatory = $true)]
        [string]$SrcToolPath
    )

    $Sources = Get-PdbSourceFiles -PdbPath $PdbPath -SrcToolPath $SrcToolPath
    $Entries = New-Object System.Collections.Generic.List[string]
    $SourcesForSnapshot = @()
    $Seen = @{}

    foreach ($Source in $Sources) {
        $RelativePath = Get-RelativeGitPath -Path $Source
        if (!$RelativePath -or !(Test-Path -LiteralPath $Source -PathType Leaf)) {
            continue
        }

        $Key = $RelativePath.ToLowerInvariant()
        if ($Seen.ContainsKey($Key)) {
            continue
        }

        $Seen[$Key] = $true
        $SourcesForSnapshot += [PSCustomObject]@{ Source = $Source; RelativePath = $RelativePath }
        $Entries.Add("$Source*$RelativePath*$Commit*$SourceBaseUrl*$(ConvertTo-UrlPath -RelativePath $RelativePath)")
    }

    if ($Entries.Count -eq 0) {
        throw "No source files from this repository were found in $PdbPath."
    }

    $Copied = Publish-SourceSnapshot -Sources $SourcesForSnapshot -Commit $Commit -StorePath $StorePath
    $SourceCommand = 'powershell -NoProfile -ExecutionPolicy Bypass -Command "$dst=''%targ%\JSEngineSrc\%var3%\%var2%''; $dir=Split-Path -Parent $dst; New-Item -ItemType Directory -Force -Path $dir | Out-Null; Invoke-WebRequest -Uri ''%var4%/src/%var3%/%var5%'' -OutFile $dst -UseBasicParsing"'
    $Lines = New-Object System.Collections.Generic.List[string]
    $Lines.Add("SRCSRV: ini ------------------------------------------------")
    $Lines.Add("VERSION=2")
    $Lines.Add("INDEXVERSION=2")
    $Lines.Add("VERCTRL=HTTP")
    $Lines.Add("DATETIME=$(Get-Date -Format s)")
    $Lines.Add("SRCSRV: variables ------------------------------------------")
    $Lines.Add("SRCSRVTRG=%targ%\JSEngineSrc\%var3%\%var2%")
    $Lines.Add("SRCSRVCMD=$SourceCommand")
    $Lines.Add("SRCSRV: source files ---------------------------------------")
    foreach ($Entry in $Entries) {
        $Lines.Add($Entry)
    }
    $Lines.Add("SRCSRV: end ------------------------------------------------")

    Set-Content -LiteralPath $StreamPath -Value $Lines -Encoding ASCII
    Write-Host "  Source indexed files: $($Entries.Count)"
    Write-Host "  Source snapshot files: $Copied"
}

function Add-SymbolFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string]$SymStorePath,

        [Parameter(Mandatory = $true)]
        [string]$StorePath,

        [Parameter(Mandatory = $true)]
        [string]$Product,

        [Parameter(Mandatory = $true)]
        [string]$BuildVersion,

        [Parameter(Mandatory = $true)]
        [string]$Comment
    )

    & $SymStorePath add /f $FilePath /s $StorePath /t $Product /v $BuildVersion /c $Comment | Write-Host
    if ($LASTEXITCODE -ne 0) {
        throw "symstore add failed for $FilePath."
    }
}

if (!(Test-Path -LiteralPath $SymbolStore)) {
    throw "Symbol store does not exist or is not reachable: $SymbolStore"
}

$SymStore = Resolve-DebugTool -Name "symstore.exe"
$SrcTool = Resolve-DebugTool -Name "srctool.exe"
$PdbStr = Resolve-DebugTool -Name "pdbstr.exe"

$CommitHash = Invoke-GitText -Arguments @("rev-parse", "HEAD")
$SourceBaseUrl = Get-LocalSourceBaseUrl -Port 8080
if (!$Version) {
    $Version = $CommitHash
}

Write-Host "Symbol store : $SymbolStore"
Write-Host "symstore     : $SymStore"
Write-Host "srctool      : $SrcTool"
Write-Host "pdbstr       : $PdbStr"
Write-Host "git commit   : $CommitHash"
Write-Host "source url   : $SourceBaseUrl/src/$CommitHash/"
Write-Host ""

$Configurations = @("Release", "GameClientRelease")
if ($IncludeDebug) {
    $Configurations = @("Debug", "Release", "GameClientDebug", "GameClientRelease")
}

$PublishedCount = 0
foreach ($Configuration in $Configurations) {
    $TargetName = if ($Configuration.StartsWith("GameClient")) { "JSEngineGame" } else { "JSEngine" }
    $OutputDir = Join-Path (Join-Path $EngineRoot "Bin") $Configuration
    $ExePath = Join-Path $OutputDir "$TargetName.exe"
    $PdbPath = Join-Path $OutputDir "$TargetName.pdb"

    Write-Host "[$Configuration] $TargetName"
    if (!(Test-Path -LiteralPath $ExePath) -or !(Test-Path -LiteralPath $PdbPath)) {
        Write-Warning "  Missing $TargetName.exe or $TargetName.pdb in $OutputDir. Build this configuration first."
        continue
    }

    if (!$SkipSourceIndex) {
        $StreamPath = Join-Path ([System.IO.Path]::GetTempPath()) "$TargetName-$Configuration-srcsrv.stream"
        Write-SourceServerStream -PdbPath $PdbPath -StreamPath $StreamPath -Commit $CommitHash -SourceBaseUrl $SourceBaseUrl -StorePath $SymbolStore -SrcToolPath $SrcTool

        & $PdbStr -w "-p:$PdbPath" "-i:$StreamPath" "-s:srcsrv" | Write-Host
        if ($LASTEXITCODE -ne 0) {
            throw "pdbstr failed for $PdbPath."
        }
    }

    $Comment = "$ProductName $Configuration $CommitHash"
    Add-SymbolFile -FilePath $PdbPath -SymStorePath $SymStore -StorePath $SymbolStore -Product $ProductName -BuildVersion $Version -Comment $Comment
    Add-SymbolFile -FilePath $ExePath -SymStorePath $SymStore -StorePath $SymbolStore -Product $ProductName -BuildVersion $Version -Comment $Comment
    $PublishedCount += 2
    Write-Host ""
}

if ($PublishedCount -eq 0) {
    throw "No symbols were published. Build the target configurations first."
}

Write-Host "Published $PublishedCount files to $SymbolStore"
