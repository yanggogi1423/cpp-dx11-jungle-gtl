param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [string]$BuildName = "",
    [string]$SymbolServer = "\\172.21.10.28\symbols",
    [string]$BuildDir = "",
    [string]$ProjectRoot = "",
    [string]$DebuggerToolsDir = "",
    [switch]$EnableSourceServer,
    [string]$SourceRepo = "",
    [switch]$VerifySymbols,
    [switch]$FailOnError,
    [switch]$RequireCleanSource
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Message) {
    Write-Host "[Info] $Message"
}

function Write-Skip([string]$Message) {
    Write-Host "[Warn] $Message"
}

function Write-ErrorStep([string]$Message) {
    Write-Host "[Error] $Message"
}

function Warn-OrThrow([string]$Message) {
    if ($FailOnError) {
        throw $Message
    }

    Write-Skip $Message
}

function Skip-OrFail([string]$Message) {
    if ($FailOnError) {
        throw $Message
    }

    Write-Skip $Message
    exit 0
}

function Resolve-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path.Trim().Trim('"').Trim("'"))
}

function Resolve-DebuggingTool([string]$ToolName, [string]$DebuggerToolsDir) {
    if ($DebuggerToolsDir) {
        $Candidates = @(
            (Join-Path $DebuggerToolsDir $ToolName),
            (Join-Path (Join-Path $DebuggerToolsDir "srcsrv") $ToolName),
            (Join-Path (Join-Path (Join-Path $DebuggerToolsDir "Debuggers") "x64") $ToolName),
            (Join-Path (Join-Path (Join-Path (Join-Path $DebuggerToolsDir "Debuggers") "x64") "srcsrv") $ToolName)
        )

        foreach ($Candidate in $Candidates) {
            if (Test-Path -LiteralPath $Candidate) { return (Resolve-Path -LiteralPath $Candidate).ProviderPath }
        }
    }

    $FromPath = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($FromPath) { return $FromPath.Source }

    $WindowsKits = "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers"
    foreach ($Arch in @("x64", "x86")) {
        $Candidate = Join-Path (Join-Path $WindowsKits $Arch) $ToolName
        if (Test-Path -LiteralPath $Candidate) { return $Candidate }

        $SrcSrvCandidate = Join-Path (Join-Path (Join-Path $WindowsKits $Arch) "srcsrv") $ToolName
        if (Test-Path -LiteralPath $SrcSrvCandidate) { return $SrcSrvCandidate }
    }

    return $null
}

function Resolve-SourceServerToolsDir([string]$SymbolServer, [string]$DebuggerToolsDir) {
    $Candidates = @()
    if ($DebuggerToolsDir) {
        $Candidates += $DebuggerToolsDir
    }

    $Candidates += @(
        (Join-Path $SymbolServer "_tools\srcsrv"),
        (Join-Path $SymbolServer "_tools\Debuggers\x64\srcsrv"),
        (Join-Path $SymbolServer "_tools\Debuggers\x64"),
        (Join-Path $SymbolServer "_tools")
    )

    foreach ($CandidateDir in $Candidates) {
        if (-not $CandidateDir) {
            continue
        }

        $PdbStr = Resolve-DebuggingTool "pdbstr.exe" $CandidateDir
        $SrcTool = Resolve-DebuggingTool "srctool.exe" $CandidateDir
        if ($PdbStr -and $SrcTool) {
            return (Split-Path -Parent $PdbStr)
        }
    }

    return ""
}

function Get-BinaryFiles([string]$Directory) {
    return Get-ChildItem -LiteralPath $Directory -File |
        Where-Object { $_.Extension -in @(".exe", ".dll") }
}

function Copy-FilesToFallback([array]$Files, [string]$FallbackDir) {
    New-Item -ItemType Directory -Force -Path $FallbackDir | Out-Null
    foreach ($File in $Files) {
        Copy-Item -LiteralPath $File.FullName -Destination $FallbackDir -Force
    }
}

function Clear-SymbolServerMisses([string]$SymbolServer, [string]$FileName) {
    $FileStoreDir = Join-Path $SymbolServer $FileName
    if (-not (Test-Path -LiteralPath $FileStoreDir)) {
        return
    }

    $MissFiles = @(Get-ChildItem -LiteralPath $FileStoreDir -Recurse -Filter "symsrv.miss.txt" -File -ErrorAction SilentlyContinue)
    foreach ($MissFile in $MissFiles) {
        Write-Step "Removing stale symbol miss cache: $($MissFile.FullName)"
        Remove-Item -LiteralPath $MissFile.FullName -Force -ErrorAction SilentlyContinue
    }

    $Dirs = @(Get-ChildItem -LiteralPath $FileStoreDir -Recurse -Directory -ErrorAction SilentlyContinue | Sort-Object FullName -Descending)
    foreach ($Dir in $Dirs) {
        $Children = @(Get-ChildItem -LiteralPath $Dir.FullName -Force -ErrorAction SilentlyContinue)
        if ($Children.Count -eq 0) {
            Remove-Item -LiteralPath $Dir.FullName -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-GitChecked {
    param(
        [string[]]$Arguments,
        [string]$FailureMessage,
        [string[]]$SafeDirectories = @()
    )

    $GitArgs = @()
    foreach ($SafeDirectory in $SafeDirectories) {
        if ($SafeDirectory) {
            $GitArgs += @("-c", "safe.directory=$SafeDirectory")
        }
    }

    $GitArgs += $Arguments
    $Output = @(& git @GitArgs 2>&1)
    foreach ($Line in $Output) {
        if ($Line) {
            Write-Host "    git: $Line"
        }
    }

    if ($LASTEXITCODE -ne 0) {
        $Detail = ($Output | Out-String).Trim()
        if ($Detail) {
            throw "$FailureMessage Detail: $Detail"
        }

        throw $FailureMessage
    }

    return $Output
}

function Add-FileToSymbolStore(
    [string]$SymStore,
    [string]$FilePath,
    [string]$SymbolServer,
    [string]$ProductName,
    [string]$BuildName,
    [string]$Commit
) {
    Clear-SymbolServerMisses $SymbolServer (Split-Path -Leaf $FilePath)
    Write-Step "Registering file to symbol server: $FilePath"
    & $SymStore add /f $FilePath /s $SymbolServer /t $ProductName /v $BuildName /c "Commit $Commit"
    if ($LASTEXITCODE -ne 0) {
        Write-Skip "symstore.exe failed for $FilePath. Removing stale miss cache and retrying once."
        Clear-SymbolServerMisses $SymbolServer (Split-Path -Leaf $FilePath)
        & $SymStore add /f $FilePath /s $SymbolServer /t $ProductName /v $BuildName /c "Commit $Commit"
        if ($LASTEXITCODE -ne 0) {
            throw "symstore.exe failed for '$FilePath' after retry."
        }
    }
}

function Ensure-SourceRepo([string]$RepoRoot, [string]$RepoPath, [string]$Commit) {
    $RepoParent = Split-Path -Parent $RepoPath
    New-Item -ItemType Directory -Force -Path $RepoParent | Out-Null

    if (-not (Test-Path -LiteralPath $RepoPath)) {
        Write-Step "Creating bare source repo: $RepoPath"
        Invoke-GitChecked @("clone", "--bare", $RepoRoot, $RepoPath) "git clone --bare failed for source repo: $RepoPath"
    }

    Write-Step "Updating bare source repo with commit: $Commit"
    $FetchError = $null
    try {
        Invoke-GitChecked @("--git-dir=$RepoPath", "fetch", $RepoRoot, "+$Commit`:refs/heads/source-server-latest") "git fetch failed for source repo: $RepoPath" @($RepoPath, $RepoRoot)
    } catch {
        $FetchError = $_.Exception.Message
        Write-Skip "git fetch failed. Checking whether the commit already exists in the bare source repo."
    }

    Write-Step "Verifying bare source repo contains commit: $Commit"
    try {
        Invoke-GitChecked @("--git-dir=$RepoPath", "cat-file", "-e", "$Commit^{commit}") "Source repo does not contain commit after fetch: $Commit" @($RepoPath)
    } catch {
        if ($FetchError) {
            throw "git fetch failed and the source repo does not already contain commit '$Commit'. Fetch error: $FetchError"
        }

        throw
    }

    $CrashDumpPath = "KraftonEngine/Source/Engine/Platform/CrashDump.cpp"
    Invoke-GitChecked @("--git-dir=$RepoPath", "cat-file", "-e", "$Commit`:$CrashDumpPath") "Source repo does not contain expected source path after push: $CrashDumpPath" @($RepoPath)
    Write-Step "Verified source path in bare repo: $CrashDumpPath"
}

function Publish-FetchScript([string]$SymbolServer) {
    $LocalFetchScript = Join-Path $PSScriptRoot "FetchSourceFromGit.cmd"
    if (-not (Test-Path -LiteralPath $LocalFetchScript)) {
        throw "FetchSourceFromGit.cmd not found: $LocalFetchScript"
    }

    $RemoteFetchScript = Join-Path $SymbolServer "FetchSourceFromGit.cmd"
    Copy-Item -LiteralPath $LocalFetchScript -Destination $RemoteFetchScript -Force
    return $RemoteFetchScript
}

function Test-PdbHasSourceServerStream([string]$PdbStr, [string]$PdbFile) {
    $ReadBackPath = Join-Path ([System.IO.Path]::GetTempPath()) ("srcsrv_verify_{0}.txt" -f ([System.Guid]::NewGuid().ToString("N")))
    try {
        & $PdbStr -r "-p:$PdbFile" "-i:$ReadBackPath" -s:srcsrv | Out-Null
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $ReadBackPath)) {
            return $false
        }

        $Content = Get-Content -LiteralPath $ReadBackPath -Raw
        return ($Content -match "SRCSRV: source files" -and $Content -match "SRCSRVCMD=")
    } finally {
        Remove-Item -LiteralPath $ReadBackPath -Force -ErrorAction SilentlyContinue
    }
}

function Assert-SourceServerStreams([array]$PdbFiles, [string]$PdbStr) {
    if (-not $PdbStr) {
        throw "pdbstr.exe is required to verify source server streams."
    }

    foreach ($Pdb in $PdbFiles) {
        Write-Step "Verifying source server stream: $($Pdb.FullName)"
        if (-not (Test-PdbHasSourceServerStream $PdbStr $Pdb.FullName)) {
            throw "Source server stream is missing or invalid in '$($Pdb.FullName)'."
        }
    }
}

function Get-BinariesWithSiblingPdb([array]$BinaryFiles) {
    $Result = @()
    foreach ($Binary in $BinaryFiles) {
        $PdbPath = [System.IO.Path]::ChangeExtension($Binary.FullName, ".pdb")
        if (Test-Path -LiteralPath $PdbPath) {
            $Result += $Binary
        }
    }

    return $Result
}

function Assert-SymbolServerLoadsPrivateSymbols(
    [string]$SymChk,
    [array]$BinaryFiles,
    [string]$SymbolServer
) {
    if (-not $SymChk) {
        throw "symchk.exe is required to verify symbol server registration."
    }

    $CacheDir = Join-Path ([System.IO.Path]::GetTempPath()) ("KraftonSymbolVerify_{0}" -f ([System.Guid]::NewGuid().ToString("N")))
    New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null

    try {
        foreach ($Binary in $BinaryFiles) {
            Write-Step "Verifying symbol server can load private symbols: $($Binary.FullName)"
            $PreviousErrorActionPreference = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            try {
                $Output = @(& $SymChk /v $Binary.FullName /s "srv*$CacheDir*$SymbolServer" 2>&1 | ForEach-Object { $_.ToString() })
            } finally {
                $ErrorActionPreference = $PreviousErrorActionPreference
            }

            if ($LASTEXITCODE -ne 0) {
                $Detail = ($Output | Out-String).Trim()
                throw "symchk.exe failed for '$($Binary.FullName)'. Detail: $Detail"
            }

            $OutputText = ($Output | Out-String)
            if ($OutputText -notmatch "private symbols & lines" -or $OutputText -notmatch "Line numbers:\s+TRUE") {
                throw "Symbol server did not return private symbols and line information for '$($Binary.FullName)'."
            }
        }
    } finally {
        Remove-Item -LiteralPath $CacheDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

try {
    $RepoRoot = if ($ProjectRoot) {
        Resolve-FullPath $ProjectRoot
    } else {
        Resolve-FullPath (Join-Path $PSScriptRoot "..")
    }

    $EngineDir = Join-Path $RepoRoot "KraftonEngine"
    $OutDir = if ($BuildDir) {
        Resolve-FullPath $BuildDir
    } else {
        Join-Path $EngineDir "Bin\$Configuration"
    }

    if (-not (Test-Path -LiteralPath $OutDir)) {
        Skip-OrFail "Build output directory not found: $OutDir"
    }

    try {
        if (-not (Test-Path -LiteralPath $SymbolServer)) {
            Skip-OrFail "Symbol server is not reachable: $SymbolServer"
        }
    } catch {
        Skip-OrFail "Cannot access symbol server '$SymbolServer'. Detail: $($_.Exception.Message)"
    }

    $PdbFiles = @(Get-ChildItem -LiteralPath $OutDir -Filter "*.pdb" -File)
    if ($PdbFiles.Count -eq 0) {
        Skip-OrFail "No PDB files found in: $OutDir"
    }

    $BinaryFiles = @(Get-BinaryFiles $OutDir)
    Write-Step "PDB files found: $($PdbFiles.Count)"
    foreach ($Pdb in $PdbFiles) {
        Write-Step "PDB: $($Pdb.FullName)"
    }

    Write-Step "Binary files found: $($BinaryFiles.Count)"
    foreach ($Binary in $BinaryFiles) {
        Write-Step "Binary: $($Binary.FullName)"
    }

    if ($BinaryFiles.Count -eq 0) {
        Write-Skip "No .exe/.dll files found in build output. Dumps from another machine may fail with 'No matching binary found'."
    }

    $Commit = (git -C $RepoRoot rev-parse HEAD 2>$null).Trim()
    if (-not $Commit) {
        Skip-OrFail "Cannot resolve git commit. Skipping symbol upload."
    }

    if (-not $BuildName) {
        $BuildName = "$Configuration-$($Commit.Substring(0, [Math]::Min(12, $Commit.Length)))"
    }

    if (-not $SourceRepo) {
        $SourceRepo = Join-Path $SymbolServer "SourceRepos\Week12.git"
    }

    Write-Step "Configuration: $Configuration"
    Write-Step "Platform: $Platform"
    Write-Step "BuildName: $BuildName"
    Write-Step "Commit: $Commit"
    Write-Step "OutDir: $OutDir"
    Write-Step "SymbolServer: $SymbolServer"
    Write-Step "SourceRepo: $SourceRepo"

    if ($EnableSourceServer) {
        $AddSourceServer = Join-Path $PSScriptRoot "AddSourceServer.ps1"
        if (-not (Test-Path -LiteralPath $AddSourceServer)) {
            Write-Skip "AddSourceServer.ps1 not found. Source server data will not be embedded."
        } else {
            $DirtyFiles = @(git -C $RepoRoot status --short 2>$null)
            if ($DirtyFiles.Count -gt 0) {
                $DirtyMessage = "Working tree has uncommitted changes. Source server can only recover committed files from $Commit."
                if ($RequireCleanSource) {
                    throw $DirtyMessage
                }

                Write-Skip $DirtyMessage
            }

            try {
                Ensure-SourceRepo $RepoRoot $SourceRepo $Commit
                $FetchScript = Publish-FetchScript $SymbolServer
                $SourceServerToolsDir = Resolve-SourceServerToolsDir $SymbolServer $DebuggerToolsDir
                if ($SourceServerToolsDir) {
                    Write-Step "Source server tools: $SourceServerToolsDir"
                } else {
                    Warn-OrThrow "pdbstr.exe/srctool.exe not found locally or in '$SymbolServer\_tools'. Source server data may not be embedded on this PC."
                }

                $SourceArgs = @(
                    "-ExecutionPolicy", "Bypass",
                    "-File", $AddSourceServer,
                    "-BuildDir", $OutDir,
                    "-RepoRoot", $RepoRoot,
                    "-SourceRepo", $SourceRepo,
                    "-Commit", $Commit,
                    "-FetchScriptPath", $FetchScript
                )

                if ($SourceServerToolsDir) {
                    $SourceArgs += @("-DebuggerToolsDir", $SourceServerToolsDir)
                }

                Write-Step "Embedding source server data before symbol registration..."
                & powershell @SourceArgs
                if ($LASTEXITCODE -ne 0) {
                    Warn-OrThrow "AddSourceServer.ps1 failed with exit code $LASTEXITCODE."
                }
            } catch {
                Warn-OrThrow "Source server data was not embedded. Detail: $($_.Exception.Message)"
            }
        }
    }

    if ($EnableSourceServer -and ($VerifySymbols -or $FailOnError)) {
        $PdbStrForVerification = Resolve-DebuggingTool "pdbstr.exe" $SourceServerToolsDir
        if ($PdbStrForVerification) {
            Assert-SourceServerStreams $PdbFiles $PdbStrForVerification
        } else {
            Warn-OrThrow "pdbstr.exe was not found. Cannot verify source server streams."
        }
    }

    $SymStoreToolsDir = $DebuggerToolsDir
    if (-not $SymStoreToolsDir -and $SourceServerToolsDir) {
        $SymStoreToolsDir = $SourceServerToolsDir
    }

    $SymStore = Resolve-DebuggingTool "symstore.exe" $SymStoreToolsDir
    if (-not $SymStore) {
        $FallbackDir = Join-Path $SymbolServer ("_fallback\{0}\{1}_{2}" -f $env:COMPUTERNAME, $Configuration, $Platform)
        if ($FailOnError) {
            throw "symstore.exe not found. Cannot register exact PDBs to the symbol server."
        }

        Write-Skip "symstore.exe not found. Copying PDB and binary files to fallback folder: $FallbackDir"
        Copy-FilesToFallback (@($PdbFiles) + @($BinaryFiles)) $FallbackDir
        exit 0
    }

    foreach ($Pdb in $PdbFiles) {
        Add-FileToSymbolStore $SymStore $Pdb.FullName $SymbolServer "KraftonEngine" $BuildName $Commit
    }

    foreach ($Binary in $BinaryFiles) {
        Add-FileToSymbolStore $SymStore $Binary.FullName $SymbolServer "KraftonEngine" $BuildName $Commit
    }

    if ($VerifySymbols -or $FailOnError) {
        $SymChk = Resolve-DebuggingTool "symchk.exe" $SymStoreToolsDir
        $BinariesToVerify = @(Get-BinariesWithSiblingPdb $BinaryFiles)
        if ($BinariesToVerify.Count -eq 0) {
            Warn-OrThrow "No binaries with sibling PDBs were found for symbol server verification."
        } elseif ($SymChk) {
            Assert-SymbolServerLoadsPrivateSymbols $SymChk $BinariesToVerify $SymbolServer
        } else {
            Warn-OrThrow "symchk.exe was not found. Cannot verify exact PDBs from the symbol server."
        }
    }

    Write-Step "Symbol upload step finished."
    exit 0
} catch {
    $FinalMessage = "Symbol upload failed because of an unexpected error: $($_.Exception.Message)"
    if ($FailOnError) {
        Write-ErrorStep $FinalMessage
    } else {
        Write-Skip $FinalMessage
    }

    if ($_.InvocationInfo -and $_.InvocationInfo.PositionMessage) {
        Write-Host "[Warn] $($_.InvocationInfo.PositionMessage)"
    }

    if ($FailOnError) {
        exit 1
    }

    exit 0
}
