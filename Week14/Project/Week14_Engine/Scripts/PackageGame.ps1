param(
    [string]$RootDir = "",
    [ValidateSet("Debug", "Game", "Release")]
    [string]$Configuration = "Game",
    [string]$OutputDir = "",
    [string]$ProductName = "KraftonEngine",
    [string]$VersionName = "",
    [string]$GitCommit = "",
    [string]$BuildTime = "",
    [switch]$DryRun,
    [switch]$NoClean,
    [switch]$IncludePdb,
    [switch]$SkipSmokeTest,
    [switch]$LaunchSmokeTest,
    [int]$LaunchSmokeTimeoutSeconds = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$Script:GeneratedTextEncoding = New-Object System.Text.UTF8Encoding -ArgumentList $false

function Resolve-FullPath {
    param([string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Ensure-TrailingSlash {
    param([string]$Path)
    if ($Path.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        return $Path
    }
    return $Path + [System.IO.Path]::DirectorySeparatorChar
}

function Get-RelativePathCompat {
    param(
        [string]$BasePath,
        [string]$FullPath
    )
    $BaseUri = New-Object System.Uri (Ensure-TrailingSlash (Resolve-FullPath $BasePath))
    $PathUri = New-Object System.Uri (Resolve-FullPath $FullPath)
    return [System.Uri]::UnescapeDataString($BaseUri.MakeRelativeUri($PathUri).ToString()).Replace("/", "\")
}

function Normalize-RelativePath {
    param([string]$Path)
    return $Path.Replace("\", "/")
}

function Get-Sha256HexFromBytes {
    param([byte[]]$Bytes)
    $Sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $Hash = $Sha.ComputeHash($Bytes)
        return -join ($Hash | ForEach-Object { $_.ToString("x2") })
    }
    finally {
        $Sha.Dispose()
    }
}

function Get-EntryBytes {
    param($Entry)
    if ($Entry.Kind -eq "Generated") {
        return $Script:GeneratedTextEncoding.GetBytes($Entry.Content)
    }
    return [System.IO.File]::ReadAllBytes($Entry.Source)
}

function Get-EntrySize {
    param($Entry)
    if ($Entry.Kind -eq "Generated") {
        return (Get-EntryBytes $Entry).Length
    }
    return (Get-Item -LiteralPath $Entry.Source).Length
}

function Get-EntryHash {
    param($Entry)
    if ($Entry.Kind -eq "Generated") {
        return Get-Sha256HexFromBytes (Get-EntryBytes $Entry)
    }
    return (Get-FileHash -LiteralPath $Entry.Source -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Add-FileEntry {
    param(
        [System.Collections.Generic.List[object]]$Entries,
        [string]$Source,
        [string]$RelativePath
    )
    if (!(Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required package file was not found: $Source"
    }
    $Entries.Add([PSCustomObject]@{
        Kind = "File"
        Source = Resolve-FullPath $Source
        RelativePath = Normalize-RelativePath $RelativePath
        Content = $null
    })
}

function Add-GeneratedEntry {
    param(
        [System.Collections.Generic.List[object]]$Entries,
        [string]$RelativePath,
        [string]$Content
    )
    $Entries.Add([PSCustomObject]@{
        Kind = "Generated"
        Source = $null
        RelativePath = Normalize-RelativePath $RelativePath
        Content = $Content
    })
}

function Add-DirectoryEntries {
    param(
        [System.Collections.Generic.List[object]]$Entries,
        [string]$SourceDir,
        [string]$RelativeRoot,
        [string[]]$ExcludeRelativePaths = @()
    )
    if (!(Test-Path -LiteralPath $SourceDir -PathType Container)) {
        throw "Required package directory was not found: $SourceDir"
    }

    $ExcludeSet = @{}
    foreach ($ExcludePath in $ExcludeRelativePaths) {
        if (![string]::IsNullOrWhiteSpace($ExcludePath)) {
            $ExcludeSet[(Normalize-RelativePath $ExcludePath).ToLowerInvariant()] = $true
        }
    }

    Get-ChildItem -LiteralPath $SourceDir -Recurse -File | ForEach-Object {
        $UnderDir = Get-RelativePathCompat $SourceDir $_.FullName
        $RelativePath = Join-Path $RelativeRoot $UnderDir
        $NormalizedRelativePath = Normalize-RelativePath $RelativePath
        if ($ExcludeSet.ContainsKey($NormalizedRelativePath.ToLowerInvariant())) {
            return
        }
        Add-FileEntry $Entries $_.FullName $RelativePath
    }
}

function Test-SafeCleanTarget {
    param(
        [string]$Target,
        [string]$Root
    )
    $FullTarget = Resolve-FullPath $Target
    $FullRoot = Resolve-FullPath $Root
    $ProjectDir = Resolve-FullPath (Join-Path $FullRoot "KraftonEngine")
    if ($FullTarget -eq $FullRoot -or $FullTarget -eq $ProjectDir) {
        return $false
    }

    $Leaf = Split-Path -Leaf $FullTarget
    if ($Leaf -eq "GameBuild" -or $Leaf -eq "ReleaseBuild" -or $Leaf -like "*Package*") {
        return $true
    }

    if (Test-Path -LiteralPath (Join-Path $FullTarget "PackageManifest.json") -PathType Leaf) {
        return $true
    }

    return $false
}

function Get-DestinationPath {
    param(
        [string]$BaseDir,
        [string]$RelativePath
    )
    return Join-Path $BaseDir ($RelativePath.Replace("/", "\"))
}

function Write-GeneratedFile {
    param(
        [string]$Path,
        [string]$Content
    )
    $Parent = Split-Path -Parent $Path
    if ($Parent) {
        New-Item -ItemType Directory -Force -Path $Parent | Out-Null
    }
    [System.IO.File]::WriteAllText($Path, $Content, $Script:GeneratedTextEncoding)
}

function Format-ByteSize {
    param([int64]$Bytes)
    if ($Bytes -ge 1GB) {
        return "{0:N2} GB" -f ($Bytes / 1GB)
    }
    if ($Bytes -ge 1MB) {
        return "{0:N2} MB" -f ($Bytes / 1MB)
    }
    if ($Bytes -ge 1KB) {
        return "{0:N2} KB" -f ($Bytes / 1KB)
    }
    return "$Bytes B"
}

function Write-PackageSizeReport {
    param(
        [System.Collections.Generic.List[object]]$ManifestEntries,
        [int64]$TotalBytes
    )

    Write-Host "Package size"
    Write-Host ("  Total        : " + (Format-ByteSize $TotalBytes))

    $ByRoot = @{}
    foreach ($Entry in $ManifestEntries) {
        $RootName = ($Entry.Path -split "/")[0]
        if ([string]::IsNullOrWhiteSpace($RootName)) {
            $RootName = "<root>"
        }
        if (!$ByRoot.ContainsKey($RootName)) {
            $ByRoot[$RootName] = 0L
        }
        $ByRoot[$RootName] += [int64]$Entry.Size
    }

    foreach ($Item in ($ByRoot.GetEnumerator() | Sort-Object Value -Descending)) {
        Write-Host ("  {0,-12}: {1}" -f $Item.Key, (Format-ByteSize ([int64]$Item.Value)))
    }

    Write-Host "Largest files"
    foreach ($Entry in ($ManifestEntries | Sort-Object Size -Descending | Select-Object -First 10)) {
        Write-Host ("  {0,10}  {1}" -f (Format-ByteSize ([int64]$Entry.Size)), $Entry.Path)
    }
}

function Get-JsonPropertyValue {
    param(
        $Object,
        [string]$Name
    )
    if ($null -eq $Object) {
        return $null
    }
    $Property = $Object.PSObject.Properties[$Name]
    if ($null -eq $Property) {
        return $null
    }
    return $Property.Value
}

function Resolve-StartupScenePackagePath {
    param([string]$StartLevelName)

    $Scene = $StartLevelName.Trim().Replace("\", "/")
    if ([string]::IsNullOrWhiteSpace($Scene)) {
        return ""
    }

    if (!$Scene.EndsWith(".Scene", [System.StringComparison]::OrdinalIgnoreCase)) {
        $Scene = $Scene + ".Scene"
    }

    if ($Scene.StartsWith("Content/Scene/", [System.StringComparison]::OrdinalIgnoreCase)) {
        return $Scene
    }

    if ($Scene.Contains("/")) {
        return $Scene
    }

    return "Content/Scene/" + $Scene
}

function Test-PackageOutput {
    param(
        [string]$PackageDir,
        $Manifest
    )

    $ManifestPath = Join-Path $PackageDir "PackageManifest.json"
    if (!(Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw "Package smoke failed: manifest was not written: $ManifestPath"
    }

    $ExePath = Join-Path $PackageDir "Bin\KraftonEngine.exe"
    if (!(Test-Path -LiteralPath $ExePath -PathType Leaf)) {
        throw "Package smoke failed: executable was not written: $ExePath"
    }

    foreach ($RequiredDir in @("Bin", "Shaders", "Content", "Settings")) {
        $DirPath = Join-Path $PackageDir $RequiredDir
        if (!(Test-Path -LiteralPath $DirPath -PathType Container)) {
            throw "Package smoke failed: required directory was not written: $DirPath"
        }
    }

    $ProjectSettingsPath = Join-Path $PackageDir "Settings\ProjectSettings.ini"
    if (!(Test-Path -LiteralPath $ProjectSettingsPath -PathType Leaf)) {
        throw "Package smoke failed: ProjectSettings.ini was not written: $ProjectSettingsPath"
    }

    $ProjectSettings = Get-Content -LiteralPath $ProjectSettingsPath -Raw | ConvertFrom-Json
    $GameSettings = Get-JsonPropertyValue $ProjectSettings "Game"
    $StartLevelName = [string](Get-JsonPropertyValue $GameSettings "StartLevelName")
    if ([string]::IsNullOrWhiteSpace($StartLevelName)) {
        throw "Package smoke failed: Game.StartLevelName is empty in Settings/ProjectSettings.ini"
    }

    $StartupScenePath = Resolve-StartupScenePackagePath $StartLevelName
    $StartupSceneFullPath = Get-DestinationPath $PackageDir $StartupScenePath
    if (!(Test-Path -LiteralPath $StartupSceneFullPath -PathType Leaf)) {
        throw "Package smoke failed: startup scene was not written: $StartupScenePath"
    }

    foreach ($RequiredScene in @("Main", "Loading", "PreInGame", "InGame")) {
        $RequiredScenePath = Resolve-StartupScenePackagePath $RequiredScene
        $RequiredSceneFullPath = Get-DestinationPath $PackageDir $RequiredScenePath
        if (!(Test-Path -LiteralPath $RequiredSceneFullPath -PathType Leaf)) {
            throw "Package smoke failed: required runtime transition scene was not written: $RequiredScenePath"
        }
    }

    $ManifestEntryList = New-Object "System.Collections.Generic.List[object]"
    foreach ($Entry in $Manifest.Entries) {
        $ManifestEntryList.Add($Entry) | Out-Null
    }

    $EntryCount = $ManifestEntryList.Count
    if ([int64]$Manifest.EntryCount -ne [int64]$EntryCount) {
        throw "Package smoke failed: manifest EntryCount $($Manifest.EntryCount) does not match $EntryCount entries"
    }

    foreach ($Entry in $ManifestEntryList) {
        $Dest = Get-DestinationPath $PackageDir $Entry.Path
        if (!(Test-Path -LiteralPath $Dest -PathType Leaf)) {
            throw "Package smoke failed: manifest entry is missing: $($Entry.Path)"
        }

        $Item = Get-Item -LiteralPath $Dest
        if ([int64]$Item.Length -ne [int64]$Entry.Size) {
            throw "Package smoke failed: size mismatch for $($Entry.Path)"
        }

        $Hash = (Get-FileHash -LiteralPath $Dest -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($Hash -ne $Entry.Sha256) {
            throw "Package smoke failed: hash mismatch for $($Entry.Path)"
        }
    }

    Write-Host "Package smoke ok: $EntryCount entries verified."
}

function Test-LaunchSmoke {
    param(
        [string]$PackageDir,
        [int]$TimeoutSeconds
    )

    if ($TimeoutSeconds -lt 1) {
        $TimeoutSeconds = 1
    }

    $ExePath = Join-Path $PackageDir "Bin\KraftonEngine.exe"
    if (!(Test-Path -LiteralPath $ExePath -PathType Leaf)) {
        throw "Launch smoke failed: executable was not found: $ExePath"
    }

    Write-Host "Launch smoke starting: $ExePath"
    $Process = Start-Process -FilePath $ExePath -WorkingDirectory $PackageDir -PassThru -WindowStyle Hidden
    if (!$Process) {
        throw "Launch smoke failed: process was not created."
    }

    try {
        $Exited = $Process.WaitForExit($TimeoutSeconds * 1000)
        if ($Exited) {
            if ($Process.ExitCode -ne 0) {
                throw "Launch smoke failed: process exited early with code $($Process.ExitCode)."
            }
            Write-Host "Launch smoke ok: process exited with code 0."
            return
        }

        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        Write-Host "Launch smoke ok: process survived $TimeoutSeconds second(s)."
    }
    finally {
        if (!$Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        }
        $Process.Dispose()
    }
}

if ([string]::IsNullOrWhiteSpace($RootDir)) {
    $RootDir = Resolve-FullPath (Join-Path $PSScriptRoot "..")
}
else {
    $RootDir = Resolve-FullPath $RootDir
}

$ProjectDir = Join-Path $RootDir "KraftonEngine"
$BuildOutput = Join-Path $ProjectDir ("Bin\" + $Configuration)
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    if ($Configuration -eq "Game") {
        $OutputDir = Join-Path $RootDir "GameBuild"
    }
    else {
        $OutputDir = Join-Path $RootDir "ReleaseBuild"
    }
}
$OutputDir = Resolve-FullPath $OutputDir

if ([string]::IsNullOrWhiteSpace($VersionName)) {
    $VersionName = Get-Date -Format "yyyyMMdd_HHmm"
}
if ([string]::IsNullOrWhiteSpace($GitCommit)) {
    try {
        $GitCommit = (git -C $RootDir rev-parse --short HEAD 2>$null)
    }
    catch {
        $GitCommit = "Unknown"
    }
    if ([string]::IsNullOrWhiteSpace($GitCommit)) {
        $GitCommit = "Unknown"
    }
}
if ([string]::IsNullOrWhiteSpace($BuildTime)) {
    $BuildTime = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
}

if (!(Test-Path -LiteralPath $BuildOutput -PathType Container)) {
    throw "Build output directory was not found: $BuildOutput"
}

$Entries = New-Object "System.Collections.Generic.List[object]"
Add-FileEntry $Entries (Join-Path $BuildOutput "KraftonEngine.exe") "Bin/KraftonEngine.exe"
Get-ChildItem -LiteralPath $BuildOutput -Filter "*.dll" -File | Sort-Object Name | ForEach-Object {
    Add-FileEntry $Entries $_.FullName ("Bin/" + $_.Name)
}
if ($IncludePdb) {
    Get-ChildItem -LiteralPath $BuildOutput -Filter "*.pdb" -File | Sort-Object Name | ForEach-Object {
        Add-FileEntry $Entries $_.FullName ("Bin/" + $_.Name)
    }
}

Add-DirectoryEntries $Entries (Join-Path $ProjectDir "Shaders") "Shaders"
Add-DirectoryEntries $Entries (Join-Path $ProjectDir "Content") "Content" @("Content/Scene/Default.Scene")
Add-DirectoryEntries $Entries (Join-Path $ProjectDir "Settings") "Settings"

$PlayBat = @"
@echo off
cd /d "%~dp0"
start "" "%~dp0Bin\KraftonEngine.exe"
"@
Add-GeneratedEntry $Entries "Play.bat" $PlayBat

$BuildInfo = @"
Product: $ProductName
Config: $Configuration
BuildVersion: $VersionName
GitCommit: $GitCommit
Executable: KraftonEngine.exe
BuildTime: $BuildTime
"@
Add-GeneratedEntry $Entries "BuildInfo.txt" $BuildInfo

$Desired = @{}
$Entries | ForEach-Object { $Desired[$_.RelativePath.ToLowerInvariant()] = $_ }

$Diff = [ordered]@{
    Added = 0
    Updated = 0
    Unchanged = 0
    Deleted = 0
}
$PreviewLines = New-Object "System.Collections.Generic.List[string]"

foreach ($Entry in $Entries) {
    $Dest = Get-DestinationPath $OutputDir $Entry.RelativePath
    if (!(Test-Path -LiteralPath $Dest -PathType Leaf)) {
        $Diff.Added++
        if ($PreviewLines.Count -lt 120) { $PreviewLines.Add("ADD    " + $Entry.RelativePath) }
        continue
    }

    $SourceSize = Get-EntrySize $Entry
    $DestItem = Get-Item -LiteralPath $Dest
    $SourceHash = Get-EntryHash $Entry
    $DestHash = (Get-FileHash -LiteralPath $Dest -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($DestItem.Length -ne $SourceSize -or $DestHash -ne $SourceHash) {
        $Diff.Updated++
        if ($PreviewLines.Count -lt 120) { $PreviewLines.Add("UPDATE " + $Entry.RelativePath) }
    }
    else {
        $Diff.Unchanged++
    }
}

if (Test-Path -LiteralPath $OutputDir -PathType Container) {
    Get-ChildItem -LiteralPath $OutputDir -Recurse -File | ForEach-Object {
        $Rel = Normalize-RelativePath (Get-RelativePathCompat $OutputDir $_.FullName)
        if ($Rel -eq "PackageManifest.json") {
            return
        }
        if (!$Desired.ContainsKey($Rel.ToLowerInvariant())) {
            $Diff.Deleted++
            if ($PreviewLines.Count -lt 120) { $PreviewLines.Add("DELETE " + $Rel) }
        }
    }
}

Write-Host "Package plan"
Write-Host "  Product      : $ProductName"
Write-Host "  Configuration: $Configuration"
Write-Host "  Version      : $VersionName"
Write-Host "  Output       : $OutputDir"
Write-Host "  DryRun       : $DryRun"
Write-Host "  LaunchSmoke : $LaunchSmokeTest"
Write-Host "  Added        : $($Diff.Added)"
Write-Host "  Updated      : $($Diff.Updated)"
Write-Host "  Unchanged    : $($Diff.Unchanged)"
Write-Host "  Deleted      : $($Diff.Deleted)"
foreach ($Line in $PreviewLines) {
    Write-Host "  $Line"
}
if ($PreviewLines.Count -ge 120) {
    Write-Host "  ... diff output truncated ..."
}

if ($DryRun) {
    Write-Host "Dry run complete. No files were written."
    exit 0
}

if ((Test-Path -LiteralPath $OutputDir -PathType Container) -and !$NoClean) {
    if (!(Test-SafeCleanTarget $OutputDir $RootDir)) {
        throw "Refusing to clean unsafe output directory: $OutputDir"
    }
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$ManifestEntries = New-Object "System.Collections.Generic.List[object]"
foreach ($Entry in $Entries) {
    $Dest = Get-DestinationPath $OutputDir $Entry.RelativePath
    $Parent = Split-Path -Parent $Dest
    if ($Parent) {
        New-Item -ItemType Directory -Force -Path $Parent | Out-Null
    }

    if ($Entry.Kind -eq "Generated") {
        Write-GeneratedFile $Dest $Entry.Content
    }
    else {
        Copy-Item -LiteralPath $Entry.Source -Destination $Dest -Force
    }

    $DestItem = Get-Item -LiteralPath $Dest
    $DestHash = (Get-FileHash -LiteralPath $Dest -Algorithm SHA256).Hash.ToLowerInvariant()
    $ManifestEntries.Add([PSCustomObject]@{
        Path = $Entry.RelativePath
        Size = $DestItem.Length
        Sha256 = $DestHash
        Source = if ($Entry.Source) { Normalize-RelativePath (Get-RelativePathCompat $RootDir $Entry.Source) } else { "<generated>" }
    })
}

$TotalBytes = 0L
foreach ($Entry in $ManifestEntries) {
    $TotalBytes += [int64]$Entry.Size
}

$Manifest = [PSCustomObject]@{
    ProductName = $ProductName
    Configuration = $Configuration
    VersionName = $VersionName
    GitCommit = $GitCommit
    BuildTime = $BuildTime
    GeneratedAt = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    EntryCount = $ManifestEntries.Count
    TotalBytes = $TotalBytes
    Entries = $ManifestEntries
}

$ManifestPath = Join-Path $OutputDir "PackageManifest.json"
$Manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ManifestPath -Encoding UTF8

if (!$SkipSmokeTest) {
    Test-PackageOutput $OutputDir $Manifest
}

Write-PackageSizeReport $ManifestEntries $TotalBytes

if ($LaunchSmokeTest) {
    Test-LaunchSmoke $OutputDir $LaunchSmokeTimeoutSeconds
}

Write-Host "Package complete: $OutputDir"
Write-Host "Manifest: $ManifestPath"
