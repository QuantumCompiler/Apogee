<#
.SYNOPSIS
    Apogee installer for Windows.

.DESCRIPTION
    irm https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.ps1 | iex

    The Windows counterpart to install.sh, and it exists because Windows ships
    binaries from the first tagged release (decided 2026-09-01). install.sh is
    bash and cannot serve this platform, so the alternative was a zip plus
    instructions -- which the parity gate could not verify, weakening the one
    guarantee this whole area is built around.

    Like install.sh, it does NOT download a model, and it does NOT execute
    anything it downloaded other than the binary you asked for. A fresh install
    is keyless and modelless, and that is a state `apogee check` passes.

    **The parity rule.** This script does not know what the data directory
    contains. It runs `apogee check --fix`, and the binary creates the layout
    from the single declaration in source/harness/layout.h -- exactly as
    install.sh and `make install` do. None of the three holds a list that could
    go stale.

.PARAMETER Prefix
    Install root. Defaults to $env:LOCALAPPDATA\Programs\Apogee.

.PARAMETER Version
    Release tag, or "latest".
#>
[CmdletBinding()]
param(
    [string]$Prefix = "$env:LOCALAPPDATA\Programs\Apogee",
    [string]$Version = 'latest',
    [string]$Repo = 'QuantumCompiler/Apogee'
)

$ErrorActionPreference = 'Stop'

function Get-Target {
    # PROCESSOR_ARCHITECTURE is the process's view and reads x86 under WOW64;
    # the OS architecture is what decides which binary to fetch.
    $arch = switch ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture) {
        'Arm64' { 'arm64' }
        'X64'   { 'x64' }
        default { throw "unsupported architecture: $_" }
    }
    return "windows-$arch"
}

$target  = Get-Target
$archive = "apogee-$target.zip"
$url = if ($Version -eq 'latest') {
    "https://github.com/$Repo/releases/latest/download/$archive"
} else {
    "https://github.com/$Repo/releases/download/$Version/$archive"
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("apogee-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp -Force | Out-Null

try {
    Write-Host "downloading $archive"
    Invoke-WebRequest -Uri $url -OutFile (Join-Path $tmp $archive) -UseBasicParsing

    Expand-Archive -Path (Join-Path $tmp $archive) -DestinationPath $tmp -Force

    $binary = Join-Path $tmp 'apogee.exe'
    if (-not (Test-Path $binary)) { throw "archive did not contain apogee.exe" }

    $binDir = Join-Path $Prefix 'bin'
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null
    Copy-Item $binary (Join-Path $binDir 'apogee.exe') -Force
    Write-Host "installed $binDir\apogee.exe"

    # Completions, where PowerShell will look for them.
    $completionSource = Join-Path $tmp 'completions\apogee.ps1'
    if (Test-Path $completionSource) {
        $completionDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PowerShell\Completions'
        New-Item -ItemType Directory -Path $completionDir -Force | Out-Null
        Copy-Item $completionSource (Join-Path $completionDir 'apogee.ps1') -Force
        Write-Host "installed completions to $completionDir"
        Write-Host "add to your profile:  . `"$completionDir\apogee.ps1`""
    }

    # The binary seeds its own layout, then verifies it. See the parity note.
    Write-Host ""
    & (Join-Path $binDir 'apogee.exe') check --fix
    if ($LASTEXITCODE -ne 0) { throw "post-install check failed" }

    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($userPath -notlike "*$binDir*") {
        Write-Host ""
        Write-Host "note: $binDir is not on your PATH. To add it:"
        Write-Host "  [Environment]::SetEnvironmentVariable('Path', `"$binDir;`$env:Path`", 'User')"
    }
}
finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
