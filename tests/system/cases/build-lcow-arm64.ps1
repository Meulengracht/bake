param(
    [string]$BuildDir,
    [string]$Configuration = 'Debug',
    [string]$OutputDir,
    [string]$ArchivePath,
    [string]$InputsDir,
    [string]$UbuntuVersion = '24',
    [string]$UbuntuRelease = '3',
    [string]$KernelImage = 'linuxkit/kernel:6.12.59-0ef72d722190ecfe0b3b37711f9a871a696e301a',
    [string]$DeltaArchive,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

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

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE"
    }
}

function Get-DockerHubToken {
    param([Parameter(Mandatory = $true)][string]$Repository)

    return (Invoke-RestMethod "https://auth.docker.io/token?service=registry.docker.io&scope=repository:${Repository}:pull").token
}

function Get-RegistryJson {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Repository,
        [Parameter(Mandatory = $true)]
        [string]$Reference,
        [Parameter(Mandatory = $true)]
        [string]$Accept
    )

    $token = Get-DockerHubToken -Repository $Repository
    return Invoke-RestMethod -Headers @{ Authorization = "Bearer $token"; Accept = $Accept } "https://registry-1.docker.io/v2/$Repository/manifests/$Reference"
}

function Save-RegistryBlob {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Repository,
        [Parameter(Mandatory = $true)]
        [string]$Digest,
        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    $token = Get-DockerHubToken -Repository $Repository
    Invoke-NativeChecked curl.exe @('-fL', '--progress-bar', '-H', "Authorization: Bearer $token", '--output', $Destination, "https://registry-1.docker.io/v2/$Repository/blobs/$Digest")
}

$repoRoot = Resolve-RepoRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $repoRoot 'build'
}
$BuildDir = (Resolve-Path $BuildDir).Path

$binDir = Join-Path $BuildDir 'bin'
$configBinDir = Join-Path $binDir $Configuration
$mkuvm = Resolve-Exe 'mkuvm' @(
    (Join-Path $configBinDir 'mkuvm.exe'),
    (Join-Path $binDir 'mkuvm.exe')
)

if (-not $InputsDir) {
    $InputsDir = Join-Path $BuildDir 'lcow-arm64-inputs'
}
if (-not $OutputDir) {
    $OutputDir = Join-Path $BuildDir 'lcow-arm64'
}
if (-not $ArchivePath) {
    $ArchivePath = Join-Path $BuildDir 'lcow-arm64.tar.xz'
}

New-Item -ItemType Directory -Force -Path $InputsDir | Out-Null
$InputsDir = (Resolve-Path $InputsDir).Path

$baseArchive = Join-Path $InputsDir 'base.tar.gz'
$kernelLayer = Join-Path $InputsDir 'linuxkit-kernel-layer.tar.gz'
$kernelExtractDir = Join-Path $InputsDir 'kernel-layer'
$kernelPath = Join-Path $InputsDir 'kernel'

if ($Force) {
    Remove-Item -Recurse -Force $OutputDir, $ArchivePath -ErrorAction SilentlyContinue
}

if (-not (Test-Path $baseArchive)) {
    $baseUrl = "https://cdimage.ubuntu.com/ubuntu-base/releases/$UbuntuVersion.04/release/ubuntu-base-$UbuntuVersion.04.$UbuntuRelease-base-arm64.tar.gz"
    Write-Host "Downloading Ubuntu base: $baseUrl"
    Invoke-NativeChecked curl.exe @('-fL', '--progress-bar', '--output', $baseArchive, $baseUrl)
}

if (-not (Test-Path $kernelPath)) {
    $parts = $KernelImage.Split(':', 2)
    if ($parts.Count -ne 2) {
        throw "KernelImage must be in repository:tag form: $KernelImage"
    }
    $repository = $parts[0]
    $tag = $parts[1]

    $index = Get-RegistryJson -Repository $repository -Reference $tag -Accept 'application/vnd.oci.image.index.v1+json, application/vnd.docker.distribution.manifest.list.v2+json'
    $manifestDigest = ($index.manifests | Where-Object { $_.platform.os -eq 'linux' -and $_.platform.architecture -eq 'arm64' } | Select-Object -First 1).digest
    if (-not $manifestDigest) {
        throw "No linux/arm64 manifest found for $KernelImage"
    }

    $manifest = Get-RegistryJson -Repository $repository -Reference $manifestDigest -Accept 'application/vnd.oci.image.manifest.v1+json, application/vnd.docker.distribution.manifest.v2+json'
    $layerDigest = ($manifest.layers | Select-Object -First 1).digest
    if (-not $layerDigest) {
        throw "No layer found for $KernelImage manifest $manifestDigest"
    }

    Write-Host "Downloading LinuxKit kernel layer: $layerDigest"
    Save-RegistryBlob -Repository $repository -Digest $layerDigest -Destination $kernelLayer

    Remove-Item -Recurse -Force $kernelExtractDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $kernelExtractDir | Out-Null
    Invoke-NativeChecked tar.exe @('-xf', $kernelLayer, '-C', $kernelExtractDir)

    $candidate = @((Join-Path $kernelExtractDir 'kernel'), (Join-Path $kernelExtractDir 'vmlinux')) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $candidate) {
        throw "No kernel or vmlinux file found in $kernelLayer"
    }
    Copy-Item -Path $candidate -Destination $kernelPath -Force
}

$workingDir = Join-Path $BuildDir 'lcow-arm64-work'
$constructArgs = @(
    'construct',
    '--output', $OutputDir,
    '--archive', $ArchivePath,
    '--base-archive', $baseArchive,
    '--kernel', $kernelPath,
    '--arch', 'arm64',
    '--working-directory', $workingDir,
    '--force'
)
if ($DeltaArchive) {
    $constructArgs += @('--delta-archive', (Resolve-Path $DeltaArchive).Path)
}

Write-Host "Running mkuvm construct"
Invoke-NativeChecked $mkuvm $constructArgs

Get-Item $ArchivePath
Get-ChildItem $OutputDir | Select-Object Name,Length | Format-Table -AutoSize