$ErrorActionPreference = "Stop"

Write-Host "Windows system tests"

$buildDir = Join-Path $env:GITHUB_WORKSPACE "build"
if (-not (Test-Path $buildDir)) {
    throw "Build directory not found: $buildDir"
}

$hypervEnabled = $false
try {
    $hv = Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All -ErrorAction Stop
    if ($hv.State -eq "Enabled") {
        $hypervEnabled = $true
    }
} catch {
    $hypervEnabled = $false
}

$vmcompute = $null
try {
    $vmcompute = Get-Service vmcompute -ErrorAction Stop
} catch {
    $vmcompute = $null
}

if (-not $hypervEnabled -or -not $vmcompute) {
    Write-Host "Hyper-V not available; skipping HCS/HNS smoke checks."
    exit 0
}

if ($vmcompute.Status -ne "Running") {
    try {
        Start-Service vmcompute
    } catch {
        Write-Host "vmcompute service not running and could not be started; skipping."
        exit 0
    }
}

try {
    Import-Module HNS -ErrorAction Stop | Out-Null
    $nets = Get-HnsNetwork
    $count = 0
    if ($nets) { $count = $nets.Count }
    Write-Host "HNS networks detected: $count"
} catch {
    Write-Host "HNS module not available; skipping HNS checks."
    exit 0
}

Write-Host "Attempting HCS container create if supported"
try {
    $newContainerCmd = Get-Command New-Container -ErrorAction SilentlyContinue
    if (-not $newContainerCmd) {
        Write-Host "New-Container cmdlet not available; skipping HCS create test."
        Write-Host "Windows system tests completed"
        exit 0
    }

    $images = Get-ContainerImage -ErrorAction SilentlyContinue
    if (-not $images -or $images.Count -eq 0) {
        Write-Host "No container images available; skipping HCS create test."
        Write-Host "Windows system tests completed"
        exit 0
    }

    $imageName = $images[0].Name
    $testName = "chef-ci-hcs-" + [Guid]::NewGuid().ToString("N")
    Write-Host "Creating container $testName from $imageName"

    New-Container -Name $testName -ContainerImageName $imageName -ErrorAction Stop | Out-Null
    Start-Container -Name $testName -ErrorAction Stop | Out-Null
    Stop-Container -Name $testName -Force -ErrorAction SilentlyContinue | Out-Null
    Remove-Container -Name $testName -Force -ErrorAction SilentlyContinue | Out-Null
} catch {
    Write-Host "HCS container create test failed: $($_.Exception.Message)"
    throw
}

Write-Host "Windows system tests completed"
exit 0
