param(
    [Parameter(Mandatory = $true)]
    [string]$MkwbasePath,
    [string]$Base = 'windows:ltsc2022',
    [string]$Image = 'mcr.microsoft.com/windows/servercore:ltsc2022',
    [string]$OutputDir = 'C:\temp\windows-base',
    [string]$ArchivePath = 'C:\temp\windows-base.tar.xz'
)

$ErrorActionPreference = 'Stop'

function New-TarXzArchive {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDir,
        [Parameter(Mandatory = $true)]
        [string]$ArchivePath
    )

    $resolvedSourceDir = (Resolve-Path $SourceDir).Path
    $archiveDir = Split-Path -Parent $ArchivePath
    if ($archiveDir) {
        New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
    }

    if (Test-Path $ArchivePath) {
        Remove-Item -Force $ArchivePath
    }

    $sourceParent = Split-Path -Parent $resolvedSourceDir
    $sourceLeaf = Split-Path -Leaf $resolvedSourceDir
    $tar = Get-Command tar.exe -ErrorAction SilentlyContinue

    if ($tar) {
        & $tar.Source -c -f $ArchivePath --xz -C $sourceParent $sourceLeaf
        if ($LASTEXITCODE -eq 0 -and (Test-Path $ArchivePath)) {
            return
        }

        if (Test-Path $ArchivePath) {
            Remove-Item -Force $ArchivePath
        }
    }

    $sevenZip = @(
        (Get-Command 7z.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
        'C:\Program Files\7-Zip\7z.exe'
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1

    if (-not $sevenZip) {
        throw 'Unable to create an xz archive because tar.exe xz support and 7-Zip are unavailable.'
    }

    if ($ArchivePath.EndsWith('.xz', [System.StringComparison]::OrdinalIgnoreCase)) {
        $tarPath = $ArchivePath.Substring(0, $ArchivePath.Length - 3)
    } else {
        $tarPath = "$ArchivePath.tar"
    }

    if (Test-Path $tarPath) {
        Remove-Item -Force $tarPath
    }

    Push-Location $sourceParent
    try {
        & $sevenZip a -ttar $tarPath $sourceLeaf
        if ($LASTEXITCODE -ne 0) {
            throw '7-Zip failed to create the intermediate tar archive.'
        }

        & $sevenZip a -txz $ArchivePath $tarPath
        if ($LASTEXITCODE -ne 0) {
            throw '7-Zip failed to create the xz archive.'
        }
    }
    finally {
        Pop-Location
        if (Test-Path $tarPath) {
            Remove-Item -Force $tarPath
        }
    }
}

if (-not (Test-Path $MkwbasePath)) {
    throw "mkwbase executable not found: $MkwbasePath"
}

$resolvedMkwbasePath = (Resolve-Path $MkwbasePath).Path
$outputParent = Split-Path -Parent $OutputDir

if ($outputParent) {
    New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
}

if (Test-Path $OutputDir) {
    Remove-Item -Recurse -Force $OutputDir
}

$dockerCommand = Get-Command docker -ErrorAction SilentlyContinue
if (-not $dockerCommand) {
    throw 'docker.exe is not available in PATH.'
}

docker version
if ($LASTEXITCODE -ne 0) {
    $dockerService = Get-Service docker, com.docker.service -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $dockerService) {
        throw 'Docker daemon is unavailable and no Docker service was found.'
    }

    if ($dockerService.Status -ne 'Running') {
        Start-Service $dockerService.Name
        $dockerService.WaitForStatus('Running', [TimeSpan]::FromSeconds(30))
    }

    docker version
    if ($LASTEXITCODE -ne 0) {
        throw 'Docker daemon is unavailable after service startup.'
    }
}

Write-Host "Running mkwbase construct for $Base from $Image"
& $resolvedMkwbasePath construct --base $Base --image $Image --output $OutputDir --force
if ($LASTEXITCODE -ne 0) {
    throw "mkwbase construct failed with exit code $LASTEXITCODE"
}

$expectedPaths = @(
    (Join-Path $OutputDir 'base.json'),
    (Join-Path $OutputDir 'windowsfilter'),
    (Join-Path $OutputDir 'windowsfilter\layerchain.json')
)

foreach ($expectedPath in $expectedPaths) {
    if (-not (Test-Path $expectedPath)) {
        throw "Expected output is missing: $expectedPath"
    }
}

New-TarXzArchive -SourceDir $OutputDir -ArchivePath $ArchivePath

if (-not (Test-Path $ArchivePath)) {
    throw "Archive was not created: $ArchivePath"
}

Write-Host "Created archive at $ArchivePath"

if ($env:GITHUB_OUTPUT) {
    "archive_path=$ArchivePath" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "output_dir=$OutputDir" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
}