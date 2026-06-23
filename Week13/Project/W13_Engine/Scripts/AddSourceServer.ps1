param(
    [string]$PdbPath = "",
    [string]$BuildDir = "",
    [string]$RepoRoot = (Join-Path $PSScriptRoot ".."),
    [string]$SourceRepo = "\\172.21.10.28\symbols\SourceRepos\Week12.git",
    [string]$Commit = "",
    [string]$DebuggerToolsDir = "",
    [string]$SrcToolPath = "",
    [string]$PdbStrPath = "",
    [string]$FetchScriptPath = "",
    [switch]$KeepStreamFiles
)

$ErrorActionPreference = "Stop"

function Resolve-DebuggingTool {
    param(
        [string]$ToolName,
        [string]$ExplicitPath,
        [string]$DebuggerToolsDir
    )

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath) {
            return (Resolve-Path -LiteralPath $ExplicitPath).ProviderPath
        }

        throw "$ToolName not found at '$ExplicitPath'."
    }

    if ($DebuggerToolsDir) {
        $Candidates = @(
            (Join-Path $DebuggerToolsDir $ToolName),
            (Join-Path (Join-Path $DebuggerToolsDir "srcsrv") $ToolName),
            (Join-Path (Join-Path (Join-Path $DebuggerToolsDir "Debuggers") "x64") $ToolName),
            (Join-Path (Join-Path (Join-Path (Join-Path $DebuggerToolsDir "Debuggers") "x64") "srcsrv") $ToolName)
        )

        foreach ($Candidate in $Candidates) {
            if (Test-Path -LiteralPath $Candidate) {
                return (Resolve-Path -LiteralPath $Candidate).ProviderPath
            }
        }
    }

    $FromPath = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($FromPath) {
        return $FromPath.Source
    }

    $WindowsKits = "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers"
    foreach ($Arch in @("x64", "x86")) {
        $Candidate = Join-Path (Join-Path $WindowsKits $Arch) $ToolName
        if (Test-Path -LiteralPath $Candidate) { return $Candidate }

        $SrcSrvCandidate = Join-Path (Join-Path (Join-Path $WindowsKits $Arch) "srcsrv") $ToolName
        if (Test-Path -LiteralPath $SrcSrvCandidate) { return $SrcSrvCandidate }
    }

    throw "$ToolName not found. Install Windows SDK Debugging Tools or pass the explicit tool path."
}

function Convert-ToGitPath {
    param(
        [string]$Path,
        [string]$Root
    )

    $FullPath = [System.IO.Path]::GetFullPath($Path)
    $FullRoot = [System.IO.Path]::GetFullPath($Root)

    if (-not $FullRoot.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $FullRoot += [System.IO.Path]::DirectorySeparatorChar
    }

    if (-not $FullPath.StartsWith($FullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $null
    }

    return $FullPath.Substring($FullRoot.Length).Replace("\", "/")
}

function Resolve-GitExe {
    $Candidates = @(
        "${env:LocalAppData}\Programs\Git\cmd\git.exe",
        "${env:LocalAppData}\Programs\Git\bin\git.exe",
        "${env:ProgramFiles}\Git\cmd\git.exe",
        "${env:ProgramFiles}\Git\bin\git.exe",
        "${env:ProgramFiles(x86)}\Git\cmd\git.exe",
        "${env:ProgramFiles(x86)}\Git\bin\git.exe"
    )

    foreach ($Candidate in $Candidates) {
        if ($Candidate -and (Test-Path -LiteralPath $Candidate)) {
            return $Candidate
        }
    }

    $FromPath = Get-Command "git.exe" -ErrorAction SilentlyContinue
    if ($FromPath) {
        foreach ($Candidate in @($FromPath.Path, $FromPath.Source)) {
            if ($Candidate -and [System.IO.Path]::IsPathRooted($Candidate) -and (Test-Path -LiteralPath $Candidate)) {
                return (Resolve-Path -LiteralPath $Candidate).ProviderPath
            }
        }
    }

    throw "git.exe not found. Install Git for Windows or add git.exe to PATH."
}

function Get-GitTreePathSet {
    param(
        [string]$GitExe,
        [string]$GitRepo,
        [string]$GitCommit
    )

    $ErrorPath = Join-Path ([System.IO.Path]::GetTempPath()) ("srcsrv_git_tree_{0}.err" -f ([System.Guid]::NewGuid().ToString("N")))
    try {
        $Paths = @(& $GitExe -c "safe.directory=$GitRepo" --git-dir="$GitRepo" ls-tree -r --name-only $GitCommit 2>$ErrorPath)
        if ($LASTEXITCODE -ne 0) {
            $Detail = ""
            if (Test-Path -LiteralPath $ErrorPath) {
                $Detail = (Get-Content -LiteralPath $ErrorPath -Raw).Trim()
            }

            throw "Cannot read source repo '$GitRepo' at commit '$GitCommit'. git ls-tree failed. $Detail"
        }

        $Set = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
        foreach ($Path in $Paths) {
            $Trimmed = $Path.Trim()
            if ($Trimmed) {
                [void]$Set.Add($Trimmed)
            }
        }

        return $Set
    } finally {
        Remove-Item -LiteralPath $ErrorPath -Force -ErrorAction SilentlyContinue
    }
}

function Write-SourceServerStream {
    param(
        [string]$StreamPath,
        [string]$PdbFile,
        [string[]]$SourceFiles,
        [string]$Root,
        [string]$GitRepo,
        [string]$GitCommit,
        [string]$GitExe,
        [string]$FetchScript,
        [System.Collections.Generic.HashSet[string]]$GitPaths
    )

    $Lines = New-Object System.Collections.Generic.List[string]
    $Lines.Add("SRCSRV: ini ------------------------------------------------")
    $Lines.Add("VERSION=2")
    $Lines.Add("INDEXVERSION=2")
    $Lines.Add("VERCTRL=Git")
    $Lines.Add("DATETIME=$(Get-Date -Format o)")
    $Lines.Add("SRCSRV: variables ------------------------------------------")
    $Lines.Add("GIT_EXE=$GitExe")
    $Lines.Add("GIT_REPO=$GitRepo")
    $Lines.Add("FETCH_SCRIPT=$FetchScript")
    $Lines.Add("SRCSRVTRG=%targ%\%fnfile%(%var2%)")
    $Lines.Add('SRCSRVCMD=cmd /d /c call "%FETCH_SCRIPT%" "%GIT_EXE%" "%GIT_REPO%" "%var3%" "%var2%" %SRCSRVTRG% "%targ%"')
    $Lines.Add("SRCSRV: source files ---------------------------------------")

    $MappedCount = 0
    foreach ($SourceFile in $SourceFiles) {
        $RelativePath = Convert-ToGitPath -Path $SourceFile -Root $Root
        if (-not $RelativePath) {
            continue
        }

        if ($GitPaths -and -not $GitPaths.Contains($RelativePath)) {
            Write-Warning "Skipping source file not found in commit $GitCommit`: $RelativePath"
            continue
        }

        $Lines.Add("$SourceFile*$RelativePath*$GitCommit")
        $MappedCount++
    }

    $Lines.Add("SRCSRV: end ------------------------------------------------")
    [System.IO.File]::WriteAllLines($StreamPath, $Lines, [System.Text.Encoding]::ASCII)

    if ($MappedCount -eq 0) {
        throw "No source files in '$PdbFile' were under repo root '$Root'."
    }

    return $MappedCount
}

function Get-TrackedSourceFiles {
    param([string]$Root)

    $Files = @()
    git -C $Root ls-files | ForEach-Object {
        $RelativePath = $_.Trim()
        if ($RelativePath -match '\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl)$') {
            $LocalPath = Join-Path $Root ($RelativePath -replace '/', '\')
            if (Test-Path -LiteralPath $LocalPath) {
                $Files += [System.IO.Path]::GetFullPath($LocalPath)
            }
        }
    }

    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed for '$Root'."
    }

    return $Files | Sort-Object -Unique
}

function Get-PdbSourceFiles {
    param(
        [string]$SrcTool,
        [string]$PdbFile,
        [string]$Root
    )

    $SourceFiles = @(& $SrcTool -r $PdbFile 2>$null | Where-Object { $_ -match "^[A-Za-z]:\\|^\\\\" } | Sort-Object -Unique)
    if ($LASTEXITCODE -eq 0 -and $SourceFiles.Count -gt 0) {
        return $SourceFiles
    }

    Write-Warning "srctool.exe failed or returned no source files for '$PdbFile'. Falling back to git tracked source files."
    return @(Get-TrackedSourceFiles -Root $Root)
}

function Test-SourceServerStream {
    param(
        [string]$PdbStr,
        [string]$PdbFile
    )

    $ReadBackPath = Join-Path ([System.IO.Path]::GetTempPath()) ("srcsrv_read_{0}.txt" -f ([System.Guid]::NewGuid().ToString("N")))
    try {
        & $PdbStr -r "-p:$PdbFile" "-i:$ReadBackPath" -s:srcsrv
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $ReadBackPath)) {
            return $false
        }

        $Content = Get-Content -LiteralPath $ReadBackPath -Raw
        return ($Content -match "SRCSRV: source files" -and $Content -match "SRCSRVCMD=")
    } finally {
        Remove-Item -LiteralPath $ReadBackPath -Force -ErrorAction SilentlyContinue
    }
}

$ResolvedRepoRoot = (Resolve-Path -LiteralPath $RepoRoot).ProviderPath

try {
    $SourceRepoExists = Test-Path -LiteralPath $SourceRepo
} catch {
    throw "Cannot access source repo '$SourceRepo'. Check network share credentials and read permission. Detail: $($_.Exception.Message)"
}

if (-not $SourceRepoExists) {
    throw "Source repo not found: $SourceRepo"
}

if (-not $Commit) {
    $Commit = (& git -C $ResolvedRepoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $Commit) {
        throw "Failed to resolve git commit from '$ResolvedRepoRoot'. Pass -Commit explicitly."
    }
}

if ($PdbPath) {
    $PdbFiles = @((Resolve-Path -LiteralPath $PdbPath).ProviderPath)
} elseif ($BuildDir) {
    $ResolvedBuildDir = (Resolve-Path -LiteralPath $BuildDir).ProviderPath
    $PdbFiles = @(Get-ChildItem -LiteralPath $ResolvedBuildDir -Recurse -Filter "*.pdb" -File | ForEach-Object { $_.FullName })
} else {
    throw "Pass either -PdbPath or -BuildDir."
}

if (-not $PdbFiles) {
    throw "No PDB files found."
}

$SrcTool = Resolve-DebuggingTool -ToolName "srctool.exe" -ExplicitPath $SrcToolPath -DebuggerToolsDir $DebuggerToolsDir
$PdbStr = Resolve-DebuggingTool -ToolName "pdbstr.exe" -ExplicitPath $PdbStrPath -DebuggerToolsDir $DebuggerToolsDir
$GitExe = Resolve-GitExe
$GitPaths = Get-GitTreePathSet -GitExe $GitExe -GitRepo $SourceRepo -GitCommit $Commit

if (-not $FetchScriptPath) {
    $FetchScriptPath = Join-Path $PSScriptRoot "FetchSourceFromGit.cmd"
}

if (-not (Test-Path -LiteralPath $FetchScriptPath)) {
    throw "Source server fetch script not found: $FetchScriptPath"
}

$ResolvedFetchScript = (Resolve-Path -LiteralPath $FetchScriptPath).ProviderPath

Write-Host "SrcTool   : $SrcTool"
Write-Host "PdbStr    : $PdbStr"
Write-Host "GitExe    : $GitExe"
Write-Host "FetchCmd  : $ResolvedFetchScript"
Write-Host "RepoRoot  : $ResolvedRepoRoot"
Write-Host "SourceRepo: $SourceRepo"
Write-Host "Commit    : $Commit"
Write-Host "Git Files : $($GitPaths.Count)"
Write-Host "PDB Count : $($PdbFiles.Count)"

foreach ($PdbFile in $PdbFiles) {
    Write-Host "Processing: $PdbFile"

    $SourceFiles = @(Get-PdbSourceFiles -SrcTool $SrcTool -PdbFile $PdbFile -Root $ResolvedRepoRoot)
    if ($SourceFiles.Count -eq 0) {
        throw "No source files found for '$PdbFile'."
    }

    $StreamPath = Join-Path ([System.IO.Path]::GetTempPath()) ("srcsrv_{0}.txt" -f ([System.Guid]::NewGuid().ToString("N")))
    $MappedCount = Write-SourceServerStream -StreamPath $StreamPath -PdbFile $PdbFile -SourceFiles $SourceFiles -Root $ResolvedRepoRoot -GitRepo $SourceRepo -GitCommit $Commit -GitExe $GitExe -FetchScript $ResolvedFetchScript -GitPaths $GitPaths

    & $PdbStr -w "-p:$PdbFile" "-i:$StreamPath" -s:srcsrv
    if ($LASTEXITCODE -ne 0) {
        throw "pdbstr.exe failed for '$PdbFile' with exit code $LASTEXITCODE."
    }

    if (-not (Test-SourceServerStream -PdbStr $PdbStr -PdbFile $PdbFile)) {
        throw "Source server stream verification failed for '$PdbFile'."
    }

    Write-Host "Embedded source server stream. Source Count: $MappedCount"

    if (-not $KeepStreamFiles) {
        Remove-Item -LiteralPath $StreamPath -Force
    } else {
        Write-Host "Stream file kept: $StreamPath"
    }
}

Write-Host "Source server data embedded successfully."
