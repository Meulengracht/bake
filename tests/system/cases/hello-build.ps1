param(
    [string]$BuildDir,
    [string]$Configuration = 'Debug',
    [string]$WorkDir,
    [string]$LcowUvmUrl,
    [string]$LcowUvmArchive,
    [string]$LcowUvmDir,
    [switch]$PrepareLcowUvm,
    [switch]$KeepWorkDir
)

$ErrorActionPreference = 'Stop'
if (Get-Variable PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir '..\..\..')).Path
}

function Resolve-Exe {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "$Name executable not found. Checked: $($Candidates -join ', ')"
}

function Invoke-ChefCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [string]$LogPath
    )

    $previousLocation = Get-Location
    $previousNativeErrorPreference = $null
    if (Get-Variable PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
        $previousNativeErrorPreference = $PSNativeCommandUseErrorActionPreference
        $PSNativeCommandUseErrorActionPreference = $false
    }
    try {
        if ($WorkingDirectory) {
            Set-Location $WorkingDirectory
        }

        if ($LogPath) {
            & $FilePath @Arguments *> $LogPath
        } else {
            & $FilePath @Arguments
        }

        return $LASTEXITCODE
    }
    finally {
        if ($null -ne $previousNativeErrorPreference) {
            $PSNativeCommandUseErrorActionPreference = $previousNativeErrorPreference
        }
        Set-Location $previousLocation
    }
}

function Test-HostPrerequisites {
    if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
        throw 'This smoke test must be run on Windows.'
    }

    $vmcompute = Get-Service vmcompute -ErrorAction SilentlyContinue
    if (-not $vmcompute) {
        throw 'The vmcompute service is not installed. Enable Hyper-V/Containers before running LCOW smoke tests.'
    }

    if ($vmcompute.Status -ne 'Running') {
        try {
            Start-Service vmcompute
            $vmcompute.WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
        }
        catch {
            throw "The vmcompute service is installed but not running, and this shell could not start it. Start vmcompute from an elevated PowerShell session, then rerun this smoke test. $($_.Exception.Message)"
        }
    }

    foreach ($tool in @('curl.exe', 'tar.exe')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "$tool is required to fetch and unpack LCOW/Ubuntu base archives."
        }
    }
}

function Get-ChefArchitecture {
    switch ($env:PROCESSOR_ARCHITECTURE) {
        'AMD64' { return 'amd64' }
        'ARM64' { return 'arm64' }
        'x86' { return 'i386' }
        default { return $env:PROCESSOR_ARCHITECTURE.ToLowerInvariant() }
    }
}

function Set-JsonProperty {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Object,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [object]$Value
    )

    if ($Object.PSObject.Properties.Name -contains $Name) {
        $Object.$Name = $Value
    } else {
        Add-Member -InputObject $Object -MemberType NoteProperty -Name $Name -Value $Value
    }
}

function Set-BakeLcowUvmUrl {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Url
    )

    $configDir = Join-Path $env:APPDATA 'chef'
    $configPath = Join-Path $configDir 'bake.json'
    $backupPath = $null

    New-Item -ItemType Directory -Force -Path $configDir | Out-Null
    if (Test-Path $configPath) {
        $backupPath = Join-Path ([System.IO.Path]::GetTempPath()) ("chef-bake-json-" + [Guid]::NewGuid().ToString('N'))
        Copy-Item -Path $configPath -Destination $backupPath -Force
        $config = Get-Content $configPath -Raw | ConvertFrom-Json
    } else {
        $config = [pscustomobject]@{}
    }

    if (-not ($config.PSObject.Properties.Name -contains 'lcow') -or -not $config.lcow) {
        Set-JsonProperty -Object $config -Name 'lcow' -Value ([pscustomobject]@{})
    }
    Set-JsonProperty -Object $config.lcow -Name 'uvm-url' -Value $Url
    $config | ConvertTo-Json -Depth 16 | Set-Content -Path $configPath -Encoding UTF8

    return [pscustomobject]@{
        Path = $configPath
        BackupPath = $backupPath
        Created = -not $backupPath
    }
}

function Restore-BakeConfig {
    param([pscustomobject]$State)

    if (-not $State) {
        return
    }

    if ($State.BackupPath) {
        Copy-Item -Path $State.BackupPath -Destination $State.Path -Force
        Remove-Item -Force $State.BackupPath -ErrorAction SilentlyContinue
    } elseif ($State.Created -and (Test-Path $State.Path)) {
        Remove-Item -Force $State.Path -ErrorAction SilentlyContinue
    }
}

function ConvertTo-FileUrl {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return ([System.Uri](Resolve-Path $Path).Path).AbsoluteUri
}

function Wait-CvdReady {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [string]$LogPath,
        [int]$TimeoutSeconds = 15
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ($Process.HasExited) {
            throw "cvd exited early with code $($Process.ExitCode). See $LogPath"
        }

        if (Test-Path $LogPath) {
            $logText = Get-Content $LogPath -Raw -ErrorAction SilentlyContinue
            if ($logText -match 'cvd server ready') {
                return
            }
        }

        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "cvd did not report readiness before timeout. See $LogPath"
}

$repoRoot = Resolve-RepoRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $repoRoot 'build'
}
$BuildDir = (Resolve-Path $BuildDir).Path

$binDir = Join-Path $BuildDir 'bin'
$configBinDir = Join-Path $binDir $Configuration
$cvdDir = Join-Path $BuildDir 'daemons\cvd'
$configCvdDir = Join-Path $cvdDir $Configuration

$bake = Resolve-Exe 'bake' @(
    (Join-Path $configBinDir 'bake.exe'),
    (Join-Path $binDir 'bake.exe')
)
$mkuvm = Resolve-Exe 'mkuvm' @(
    (Join-Path $configBinDir 'mkuvm.exe'),
    (Join-Path $binDir 'mkuvm.exe')
)
$cvd = Resolve-Exe 'cvd' @(
    (Join-Path $configCvdDir 'cvd.exe'),
    (Join-Path $cvdDir 'cvd.exe'),
    (Join-Path $configBinDir 'cvd.exe'),
    (Join-Path $binDir 'cvd.exe')
)

$recipeDir = Join-Path $repoRoot 'examples\recipes'
$recipePath = Join-Path $recipeDir 'hello.yaml'
$recipeSource = Join-Path $recipeDir 'hello-world'
if (-not (Test-Path $recipePath)) {
    throw "Recipe file not found: $recipePath"
}
if (-not (Test-Path $recipeSource)) {
    throw "Recipe source directory not found: $recipeSource"
}

Test-HostPrerequisites

if (-not $WorkDir) {
    $WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) ("chef-hello-build-" + [Guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$WorkDir = (Resolve-Path $WorkDir).Path

$cvdLog = Join-Path $WorkDir 'cvd.log'
$cvdErrorLog = Join-Path $WorkDir 'cvd.err.log'
$bakeLog = Join-Path $WorkDir 'bake-build.log'
$preparedUvmDir = Join-Path $WorkDir 'lcow-uvm'
$bakeConfigState = $null

try {
    Write-Host '=== hello-build-windows ==='
    Write-Host "Repository: $repoRoot"
    Write-Host "Work dir:   $WorkDir"
    Write-Host "bake:       $bake"
    Write-Host "cvd:        $cvd"
    Write-Host "mkuvm:      $mkuvm"

    if ($LcowUvmDir) {
        if ($LcowUvmUrl -or $LcowUvmArchive) {
            throw 'Use only one of -LcowUvmUrl, -LcowUvmArchive, or -LcowUvmDir.'
        }

        $LcowUvmArchive = Join-Path $WorkDir 'lcow-uvm.tar.xz'
        Write-Host "Archiving LCOW UVM bundle from $LcowUvmDir"
        $archiveCode = Invoke-ChefCommand -FilePath $mkuvm -Arguments @('archive', '--source', (Resolve-Path $LcowUvmDir).Path, '--archive', $LcowUvmArchive, '--force')
        if ($archiveCode -ne 0) {
            throw "mkuvm archive failed with exit code $archiveCode"
        }
    }

    if ($LcowUvmArchive) {
        if ($LcowUvmUrl) {
            throw 'Use only one of -LcowUvmUrl and -LcowUvmArchive.'
        }
        $LcowUvmUrl = ConvertTo-FileUrl -Path $LcowUvmArchive
    }

    if ($PrepareLcowUvm) {
        if (-not $LcowUvmUrl) {
            $arch = Get-ChefArchitecture
            $LcowUvmUrl = "https://chef-store-eu-basic.s3.de.io.cloud.ovh.net/build-bases/windows-lcow-$arch.tar.xz"
        }

        Write-Host "Preparing LCOW UVM bundle from $LcowUvmUrl"
        $mkuvmCode = Invoke-ChefCommand -FilePath $mkuvm -Arguments @('fetch', '--url', $LcowUvmUrl, '--output', $preparedUvmDir, '--force')
        if ($mkuvmCode -ne 0) {
            throw "mkuvm fetch failed with exit code $mkuvmCode"
        }
    }

    if ($LcowUvmUrl) {
        Write-Host "Using LCOW UVM URL for bake: $LcowUvmUrl"
        $bakeConfigState = Set-BakeLcowUvmUrl -Url $LcowUvmUrl
    }

    Copy-Item -Path $recipePath -Destination (Join-Path $WorkDir 'hello.yaml') -Force
    Copy-Item -Path $recipeSource -Destination (Join-Path $WorkDir 'hello-world') -Recurse -Force

    Write-Host 'Starting cvd...'
    $cvdProcess = Start-Process -FilePath $cvd -ArgumentList @('-vv') -RedirectStandardOutput $cvdLog -RedirectStandardError $cvdErrorLog -PassThru -WindowStyle Hidden
    try {
        Wait-CvdReady -Process $cvdProcess -LogPath $cvdLog

        Write-Host 'Building hello.yaml with bake...'
        $bakeCode = Invoke-ChefCommand -FilePath $bake -Arguments @('-v', 'build', 'hello.yaml') -WorkingDirectory $WorkDir -LogPath $bakeLog
        if ($bakeCode -ne 0) {
            throw "bake build failed with exit code $bakeCode. See $bakeLog"
        }
    }
    finally {
        if ($cvdProcess -and -not $cvdProcess.HasExited) {
            Stop-Process -Id $cvdProcess.Id -Force
            $cvdProcess.WaitForExit()
        }
    }

    $packFiles = Get-ChildItem -Path $WorkDir -Filter '*.pack' -File
    if (-not $packFiles) {
        throw "bake completed but no .pack artifact was produced in $WorkDir. See $bakeLog"
    }

    foreach ($packFile in $packFiles) {
        if ($packFile.Length -le 0) {
            throw "Empty .pack artifact: $($packFile.FullName)"
        }
        Write-Host "artifact: $($packFile.Name) ($($packFile.Length) bytes)"
    }

    Write-Host 'PASS: hello-build-windows'
}
catch {
    Write-Host "FAIL: $($_.Exception.Message)"
    if (Test-Path $cvdLog) {
        Write-Host "--- cvd stdout ---"
        Get-Content $cvdLog -Tail 200
    }
    if (Test-Path $cvdErrorLog) {
        Write-Host "--- cvd stderr ---"
        Get-Content $cvdErrorLog -Tail 200
    }
    if (Test-Path $bakeLog) {
        Write-Host "--- bake-build.log ---"
        Get-Content $bakeLog -Tail 200
    }
    throw
}
finally {
    Restore-BakeConfig -State $bakeConfigState

    if (-not $KeepWorkDir) {
        Remove-Item -Recurse -Force $WorkDir -ErrorAction SilentlyContinue
    } else {
        Write-Host "Kept work directory: $WorkDir"
    }
}
