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
    [string]$HcsshimDir,
    [string]$HcsshimRef = '4712998fa57874aa7c28963702ba0c2213e3ebbf',
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

function ConvertTo-WslPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $wslPath = & wsl.exe -e wslpath -a $Path
    if ($LASTEXITCODE -ne 0) {
        throw "wslpath failed for $Path with exit code $LASTEXITCODE"
    }
    return $wslPath.Trim()
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
if (-not $HcsshimDir) {
    $HcsshimDir = Join-Path $BuildDir 'lcow-hcsshim'
}

New-Item -ItemType Directory -Force -Path $InputsDir | Out-Null
$InputsDir = (Resolve-Path $InputsDir).Path

$baseArchive = Join-Path $InputsDir 'base.tar.gz'
$kernelLayer = Join-Path $InputsDir 'linuxkit-kernel-layer.tar.gz'
$kernelExtractDir = Join-Path $InputsDir 'kernel-layer'
$kernelPath = Join-Path $InputsDir 'kernel'
$hypervKernelPath = Join-Path $InputsDir 'kernel-hyperv'

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

if (-not (Test-Path $hypervKernelPath)) {
    $kernelSourceArchive = Join-Path $kernelExtractDir 'linux.tar.xz'
    $kernelDevArchive = Join-Path $kernelExtractDir 'kernel-dev.tar'
    $kernelBuildDir = Join-Path $InputsDir 'linux-hyperv-build'

    if (-not (Test-Path $kernelSourceArchive)) {
        throw "LinuxKit kernel source archive not found: $kernelSourceArchive"
    }
    if (-not (Test-Path $kernelDevArchive)) {
        throw "LinuxKit kernel development archive not found: $kernelDevArchive"
    }

    Remove-Item -Recurse -Force $kernelBuildDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $kernelBuildDir | Out-Null

    $kernelSourceArchiveWslPath = ConvertTo-WslPath -Path (Resolve-Path $kernelSourceArchive).Path
    $kernelDevArchiveWslPath = ConvertTo-WslPath -Path (Resolve-Path $kernelDevArchive).Path
    $kernelBuildDirWslPath = ConvertTo-WslPath -Path (Resolve-Path $kernelBuildDir).Path
    $hypervKernelWslPath = ConvertTo-WslPath -Path $hypervKernelPath

    Write-Host "Building ARM64 LinuxKit kernel with Hyper-V vsock support"
    Invoke-NativeChecked wsl.exe @('-e', 'sh', '-lc', "set -e; for tool in make gcc flex bison bc openssl perl; do command -v `$tool >/dev/null || { echo Required WSL kernel build tool not found: `$tool >&2; exit 1; }; done; tar -xJf '$kernelSourceArchiveWslPath' -C '$kernelBuildDirWslPath' --strip-components=1; config_stage=`$(mktemp -d); tar -xf '$kernelDevArchiveWslPath' -C `$config_stage; cp `$config_stage/usr/src/linux-headers-*/.config '$kernelBuildDirWslPath/.config'; rm -rf `$config_stage; cd '$kernelBuildDirWslPath'; scripts/config --enable HYPERV --enable HYPERV_NET --enable VSOCKETS --enable HYPERV_VSOCKETS --enable VSOCKETS_DIAG --enable VSOCKETS_LOOPBACK --disable NETFILTER; make olddefconfig; grep -Eq '^CONFIG_HYPERV=y$' .config; grep -Eq '^CONFIG_HYPERV_NET=y$' .config; grep -Eq '^CONFIG_HYPERV_VSOCKETS=y$' .config; make -j`$(nproc) Image; cp arch/arm64/boot/Image '$hypervKernelWslPath'")
}

$kernelPath = $hypervKernelPath

if (-not $DeltaArchive) {
    if (-not (Test-Path $HcsshimDir)) {
        Write-Host "Cloning hcsshim at $HcsshimRef"
        Invoke-NativeChecked git.exe @('clone', 'https://github.com/microsoft/hcsshim.git', $HcsshimDir)
    }

    Invoke-NativeChecked git.exe @('-C', $HcsshimDir, 'fetch', '--depth', '1', 'origin', $HcsshimRef)
    Invoke-NativeChecked git.exe @('-C', $HcsshimDir, 'checkout', '--detach', 'FETCH_HEAD')

    $hcsshimWslPath = ConvertTo-WslPath -Path (Resolve-Path $HcsshimDir).Path
    if ($hcsshimWslPath.Contains("'")) {
        throw "hcsshim WSL path cannot contain a single quote: $hcsshimWslPath"
    }
    Write-Host "Building hcsshim LCOW guest delta"
    Invoke-NativeChecked wsl.exe @('-e', 'sh', '-lc', "cd '$hcsshimWslPath' && for tool in make gcc go tar gzip runc; do command -v `$tool >/dev/null || { echo Required WSL tool not found: `$tool >&2; exit 1; }; done && { make USR_MERGED_ROOTFS=1 out/delta.tar.gz || test -s out/delta.tar.gz; }")

    $rawDeltaArchive = Join-Path $HcsshimDir 'out\delta.tar.gz'
    $DeltaArchive = Join-Path $InputsDir 'delta-with-runc.tar.gz'
    $deltaStage = Join-Path $InputsDir 'delta-with-runc'
    Remove-Item -Recurse -Force $deltaStage -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $deltaStage | Out-Null
    $rawDeltaWslPath = ConvertTo-WslPath -Path (Resolve-Path $rawDeltaArchive).Path
    $deltaWslPath = ConvertTo-WslPath -Path $DeltaArchive
    $deltaStageWslPath = ConvertTo-WslPath -Path (Resolve-Path $deltaStage).Path
    Write-Host "Adding ARM64 runc to LCOW guest delta"
    Invoke-NativeChecked wsl.exe @('-e', 'sh', '-lc', "set -e; tar -xzf '$rawDeltaWslPath' -C '$deltaStageWslPath'; install -m 0755 /usr/sbin/runc '$deltaStageWslPath/bin/runc'; tar -zcf '$deltaWslPath' -C '$deltaStageWslPath' .")
    Remove-Item -Recurse -Force $deltaStage
}

if (-not (Test-Path $DeltaArchive)) {
    throw "LCOW guest delta archive not found: $DeltaArchive"
}

$workingDir = Join-Path $BuildDir 'lcow-arm64-work'
Remove-Item -Recurse -Force $workingDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $workingDir | Out-Null
$constructArgs = @(
    'construct',
    '--output', $OutputDir,
    '--archive', $ArchivePath,
    '--base-archive', $baseArchive,
    '--kernel', $kernelPath,
    '--arch', 'arm64',
    '--working-directory', $workingDir,
    '--force',
    '--delta-archive', (Resolve-Path $DeltaArchive).Path
)

Write-Host "Running mkuvm construct"
Invoke-NativeChecked $mkuvm $constructArgs

$baseArchiveWslPath = ConvertTo-WslPath -Path (Resolve-Path $baseArchive).Path
$deltaArchiveWslPath = ConvertTo-WslPath -Path (Resolve-Path $DeltaArchive).Path
$kernelModulesArchive = Join-Path $kernelExtractDir 'kernel.tar'
if (-not (Test-Path $kernelModulesArchive)) {
    throw "LinuxKit kernel modules archive not found: $kernelModulesArchive"
}
$kernelModulesArchiveWslPath = ConvertTo-WslPath -Path (Resolve-Path $kernelModulesArchive).Path
$outputDirWslPath = ConvertTo-WslPath -Path (Resolve-Path $OutputDir).Path
$udhcpcScriptWslPath = ConvertTo-WslPath -Path (Resolve-Path (Join-Path $PSScriptRoot 'lcow\udhcpc-default.script')).Path
$linuxRootfsWslPath = "/tmp/chef-lcow-rootfs-$([Guid]::NewGuid().ToString('N'))"

Write-Host "Repacking initrd in WSL to preserve Linux ownership and modes"
Invoke-NativeChecked wsl.exe @(
    '-u', 'root', '-e', 'sh', '-lc',
    "set -e; mkdir -p '$linuxRootfsWslPath'; tar -xzf '$baseArchiveWslPath' -C '$linuxRootfsWslPath'; tar -xf '$kernelModulesArchiveWslPath' -C '$linuxRootfsWslPath' --keep-directory-symlink; tar -xzf '$deltaArchiveWslPath' -C '$linuxRootfsWslPath' --keep-directory-symlink --no-same-owner; package_dir=`$(mktemp -d); (cd `"`$package_dir`" && apt-get download -qq busybox-static); dpkg-deb --fsys-tarfile `"`$package_dir`"/busybox-static_*.deb | tar -xf - -C '$linuxRootfsWslPath' --keep-directory-symlink; rm -rf `"`$package_dir`"; mkdir -p '$linuxRootfsWslPath/usr/share/udhcpc'; tr -d '\r' < '$udhcpcScriptWslPath' > '$linuxRootfsWslPath/usr/share/udhcpc/default.script'; chmod 0755 '$linuxRootfsWslPath/usr/share/udhcpc/default.script'; for link in bin lib sbin; do test -L '$linuxRootfsWslPath'/`$link || { echo LCOW rootfs /`$link is no longer a usr-merge symlink >&2; exit 1; }; done; for exe in init bin/gcs bin/gcstools bin/vsockexec bin/runc bin/busybox bin/sh lib/ld-linux-aarch64.so.1 usr/share/udhcpc/default.script; do test -x '$linuxRootfsWslPath'/`$exe || { echo LCOW rootfs missing executable /`$exe >&2; exit 1; }; done; chown 0:0 '$linuxRootfsWslPath/init' '$linuxRootfsWslPath/usr/bin/gcs' '$linuxRootfsWslPath/usr/bin/gcstools' '$linuxRootfsWslPath/usr/bin/runc' '$linuxRootfsWslPath/usr/bin/vsockexec' '$linuxRootfsWslPath/usr/bin/wait-paths' '$linuxRootfsWslPath/usr/bin/busybox'; chmod 0755 '$linuxRootfsWslPath/init' '$linuxRootfsWslPath/usr/bin/gcs' '$linuxRootfsWslPath/usr/bin/gcstools' '$linuxRootfsWslPath/usr/bin/runc' '$linuxRootfsWslPath/usr/bin/vsockexec' '$linuxRootfsWslPath/usr/bin/wait-paths' '$linuxRootfsWslPath/usr/bin/busybox'; cd '$linuxRootfsWslPath'; find . -mindepth 1 -print0 | cpio --null -o --format=newc --quiet | gzip -c > '$outputDirWslPath/initrd'; rm -rf '$linuxRootfsWslPath'"
)

Invoke-NativeChecked $mkuvm @(
    'archive',
    '--source', $OutputDir,
    '--archive', $ArchivePath,
    '--force'
)

Get-Item $ArchivePath
Get-ChildItem $OutputDir | Select-Object Name,Length | Format-Table -AutoSize