param(
    [string]$CrashDumpRoot = "",
    [string]$SymbolServer = "\\172.21.10.28\symbols",
    [string]$SymChk = "",
    [string]$CacheDir = "",
    [switch]$Apply
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Message) {
    Write-Host "[Info] $Message"
}

function Write-WarnLine([string]$Message) {
    Write-Host "[Warn] $Message"
}

function Join-PathMany([string[]]$Parts) {
    $Result = $Parts[0]
    for ($Index = 1; $Index -lt $Parts.Count; ++$Index) {
        $Result = [System.IO.Path]::Combine($Result, $Parts[$Index])
    }
    return $Result
}

function Resolve-Tool([string]$ToolName, [string]$SymbolServer, [string]$ExplicitPath) {
    if ($ExplicitPath -and (Test-Path -LiteralPath $ExplicitPath)) {
        return (Resolve-Path -LiteralPath $ExplicitPath).ProviderPath
    }

    $Candidates = @(
        (Join-PathMany @($SymbolServer, "_tools", "srcsrv", $ToolName)),
        (Join-PathMany @($SymbolServer, "_tools", "Debuggers", "x64", $ToolName)),
        (Join-PathMany @($SymbolServer, "_tools", "Debuggers", "x64", "srcsrv", $ToolName))
    )

    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate) {
            return (Resolve-Path -LiteralPath $Candidate).ProviderPath
        }
    }

    $FromPath = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($FromPath) {
        return $FromPath.Source
    }

    return ""
}

function Get-StoreIndexesFromSymChkOutput([string[]]$Output, [string]$PdbFileName) {
    $Indexes = New-Object System.Collections.Generic.List[string]

    for ($Index = 0; $Index -lt $Output.Count; ++$Index) {
        $Line = $Output[$Index].Trim()
        if ($Line -ne $PdbFileName) {
            continue
        }

        for ($Next = $Index + 1; $Next -lt $Output.Count; ++$Next) {
            $Candidate = $Output[$Next].Trim()
            if (-not $Candidate) {
                continue
            }

            if ($Candidate -match "^[0-9A-Fa-f]{32,}[0-9A-Fa-f]+$") {
                $Indexes.Add($Candidate)
            }

            break
        }
    }

    return $Indexes.ToArray()
}

function Add-IndexFromGuidAndAge(
    [System.Collections.Generic.List[string]]$Indexes,
    [string]$GuidText,
    [string]$AgeText
) {
    if (-not $GuidText -or -not $AgeText) {
        return
    }

    $GuidNoDash = $GuidText.Trim("{}").Replace("-", "").ToUpperInvariant()
    $AgeValue = 0
    if ($AgeText -match "^0x[0-9A-Fa-f]+$") {
        $AgeValue = [Convert]::ToInt32($AgeText, 16)
    } elseif ($AgeText -match "^[0-9]+$") {
        $AgeValue = [Convert]::ToInt32($AgeText, 10)
    } else {
        return
    }

    $Indexes.Add($GuidNoDash + $AgeValue.ToString("x"))
    $Indexes.Add($GuidNoDash + $AgeValue.ToString())
}

function Get-PdbRequirementFromSymChkOutput([string[]]$Output, [string]$ExePath) {
    $PdbFileName = ""
    $GuidText = ""
    $AgeText = ""

    foreach ($Line in $Output) {
        if (-not $PdbFileName -and $Line -match "CV Data:\s+(.+)$") {
            $PdbFileName = [System.IO.Path]::GetFileName($Matches[1].Trim())
        }

        if (-not $GuidText -and $Line -match "PDB7 Sig:\s+(\{[0-9A-Fa-f-]+\})") {
            $GuidText = $Matches[1]
        }

        if (-not $AgeText -and $Line -match "Age:\s+(0x[0-9A-Fa-f]+|[0-9]+)\s*$") {
            $AgeText = $Matches[1]
        }
    }

    if (-not $PdbFileName) {
        $PdbFileName = [System.IO.Path]::GetFileNameWithoutExtension($ExePath) + ".pdb"
    }

    $Indexes = New-Object System.Collections.Generic.List[string]
    foreach ($StoreIndex in (Get-StoreIndexesFromSymChkOutput $Output $PdbFileName)) {
        $Indexes.Add($StoreIndex)
    }

    Add-IndexFromGuidAndAge $Indexes $GuidText $AgeText

    return [pscustomobject]@{
        PdbFileName = $PdbFileName
        StoreIndexes = @($Indexes | Select-Object -Unique)
        Guid = $GuidText
        Age = $AgeText
    }
}

function Find-PdbInSymbolStore([string]$SymbolServer, [object]$Requirement) {
    foreach ($StoreIndex in $Requirement.StoreIndexes) {
        $Candidate = Join-PathMany @($SymbolServer, $Requirement.PdbFileName, $StoreIndex, $Requirement.PdbFileName)
        if (Test-Path -LiteralPath $Candidate) {
            return (Resolve-Path -LiteralPath $Candidate).ProviderPath
        }
    }

    return ""
}

if (-not $CrashDumpRoot) {
    $CrashDumpRoot = Join-Path $SymbolServer "_crashdumps"
}

if (-not $CacheDir) {
    $CacheDir = Join-Path $env:LOCALAPPDATA "KraftonEngine\SymbolsCache"
}

$SymChkPath = Resolve-Tool "symchk.exe" $SymbolServer $SymChk
if (-not $SymChkPath) {
    throw "symchk.exe was not found. Pass -SymChk or place it under '$SymbolServer\_tools\srcsrv'."
}

if (-not (Test-Path -LiteralPath $CrashDumpRoot)) {
    throw "Crash dump root was not found: $CrashDumpRoot"
}

if (-not (Test-Path -LiteralPath $SymbolServer)) {
    throw "Symbol server was not found: $SymbolServer"
}

Write-Step "CrashDumpRoot: $CrashDumpRoot"
Write-Step "SymbolServer : $SymbolServer"
Write-Step "SymChk       : $SymChkPath"
Write-Step "Mode         : $(if ($Apply) { 'Apply' } else { 'Dry-run' })"

$Results = New-Object System.Collections.Generic.List[object]
$DumpFiles = @(Get-ChildItem -LiteralPath $CrashDumpRoot -Recurse -Filter "*.dmp" -File -ErrorAction SilentlyContinue)

foreach ($DumpFile in $DumpFiles) {
    $CrashDir = $DumpFile.Directory
    $ExeFiles = @(Get-ChildItem -LiteralPath $CrashDir.FullName -Filter "*.exe" -File -ErrorAction SilentlyContinue)
    if ($ExeFiles.Count -eq 0) {
        $Results.Add([pscustomobject]@{
            Status = "NoExe"
            CrashDir = $CrashDir.FullName
            Pdb = ""
            RequiredIndex = ""
            Source = ""
        })
        continue
    }

    foreach ($ExeFile in $ExeFiles) {
        $ExpectedPdbPath = Join-Path $CrashDir.FullName ([System.IO.Path]::GetFileNameWithoutExtension($ExeFile.Name) + ".pdb")
        if (Test-Path -LiteralPath $ExpectedPdbPath) {
            $Results.Add([pscustomobject]@{
                Status = "AlreadyPresent"
                CrashDir = $CrashDir.FullName
                Pdb = $ExpectedPdbPath
                RequiredIndex = ""
                Source = "CrashDir"
            })
            continue
        }

        $PreviousErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            $Output = @(& $SymChkPath /v $ExeFile.FullName /s "srv*$CacheDir*$SymbolServer" 2>&1 | ForEach-Object { $_.ToString() })
        } finally {
            $ErrorActionPreference = $PreviousErrorActionPreference
        }

        $Requirement = Get-PdbRequirementFromSymChkOutput $Output $ExeFile.FullName
        $SourcePdb = Find-PdbInSymbolStore $SymbolServer $Requirement

        if ($SourcePdb) {
            if ($Apply) {
                Copy-Item -LiteralPath $SourcePdb -Destination (Join-Path $CrashDir.FullName $Requirement.PdbFileName) -Force
            }

            $Results.Add([pscustomobject]@{
                Status = if ($Apply) { "Copied" } else { "CopyAvailable" }
                CrashDir = $CrashDir.FullName
                Pdb = $Requirement.PdbFileName
                RequiredIndex = ($Requirement.StoreIndexes -join ",")
                Source = $SourcePdb
            })
        } else {
            $Results.Add([pscustomobject]@{
                Status = "MissingFromSymbolStore"
                CrashDir = $CrashDir.FullName
                Pdb = $Requirement.PdbFileName
                RequiredIndex = ($Requirement.StoreIndexes -join ",")
                Source = ""
            })
        }
    }
}

$Results | Format-Table -AutoSize

$Missing = @($Results | Where-Object { $_.Status -eq "MissingFromSymbolStore" })
if ($Missing.Count -gt 0) {
    Write-WarnLine "Some dumps require PDBs that are not in the symbol server. Those dumps cannot recover source until the exact PDB is restored."
}

if (-not $Apply) {
    Write-Step "Dry-run only. Re-run with -Apply to copy recoverable PDBs into crash dump folders."
}
