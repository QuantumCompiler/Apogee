<#
.SYNOPSIS
    Apogee installer for Windows.

.DESCRIPTION
    irm https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.ps1 | iex

    With a parameter -- a release other than the latest, another install root:

    & ([scriptblock]::Create((irm https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.ps1))) -Version 0.2.0

    The Windows counterpart to install.sh, and it exists because Windows ships
    binaries from the first tagged release (decided 2026-09-01). install.sh is
    bash and cannot serve this platform, so the alternative was a zip plus
    instructions -- which the parity gate could not verify, weakening the one
    guarantee this whole area is built around.

    Like install.sh, it does NOT download a model, and it does NOT execute
    anything it downloaded other than the binary you asked for. A fresh install
    is keyless and modelless, and that is a state `apogee check` passes. Nor
    does it change your PATH or your profile: where one needs a line, it
    prints the line.

    **The parity rule.** This script does not know what the data directory
    contains. It runs `apogee check --fix`, and the binary creates the layout
    from the single declaration in source/data/contracts/layout.h -- exactly as
    install.sh and `make install` do. None of the three holds a list that could
    go stale.

.PARAMETER Prefix
    Install root. Defaults to $env:LOCALAPPDATA\Programs\Apogee.

.PARAMETER Version
    The release to install: its tag (v0.2.0) or its number (0.2.0), or
    "latest", the default.
#>
[CmdletBinding()]
param(
    [string]$Prefix = "$env:LOCALAPPDATA\Programs\Apogee",
    [string]$Version = 'latest',
    [string]$Repo = 'QuantumCompiler/Apogee'
)

# `irm ... | iex` runs this text in the caller's own scope, so everything below
# runs in a child scope of its own (57l). Run that way, the script once left
# `$ErrorActionPreference = 'Stop'` in the user's window -- every later command
# there stopping at its first error -- and its working variables defined, `$tmp`
# naming a folder already deleted. The parameters above are the one thing iex
# still binds in the caller's scope; run as a file or a script block, nothing is.
& {
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

    # A release's tag carries its v (v0.2.0), and its number alone names the
    # same release (57l), as the Makefile's VERSION does: 0.2.0 asked for a tag
    # that does not exist. Anything else is passed through as given, so a tag
    # that does not exist still fails naming its URL.
    $tag = if (-not $Version -or $Version -eq 'latest') {
        'latest'
    } elseif ($Version.StartsWith('v')) {
        $Version
    } else {
        "v$Version"
    }

    $target  = Get-Target
    $archive = "apogee-$target.zip"
    $url = if ($tag -eq 'latest') {
        "https://github.com/$Repo/releases/latest/download/$archive"
    } else {
        "https://github.com/$Repo/releases/download/$tag/$archive"
    }

    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("apogee-" + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null

    try {
        Write-Host "downloading $archive"
        try {
            Invoke-WebRequest -Uri $url -OutFile (Join-Path $tmp $archive) -UseBasicParsing
        } catch {
            # Named as install.sh names it: the URL is what says which tag.
            throw "could not download $url -- $($_.Exception.Message)"
        }

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

        # Then it arrives ready (51): the runtime sets, the providers and the
        # stored models -- each step said, never fatal. APOGEE_MINIMAL_INSTALL=1
        # skips it; `apogee bootstrap` runs it again any time.
        Write-Host ""
        & (Join-Path $binDir 'apogee.exe') bootstrap
        if ($LASTEXITCODE -ne 0) { Write-Host "note: the bootstrap did not finish -- 'apogee bootstrap' runs it again" }

        # The line it prints adds binDir to the stored User PATH, read from that
        # scope -- never `$env:Path`, the process's machine and user PATH merged
        # and expanded, which copied every machine entry into the user's (57l)
        # -- and to this window's PATH, so `apogee` resolves here at once.
        $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
        if ($userPath -notlike "*$binDir*") {
            Write-Host ""
            Write-Host "note: $binDir is not on your PATH. To add it, for new windows and this one:"
            Write-Host "  [Environment]::SetEnvironmentVariable('Path', `"$binDir;`" + [Environment]::GetEnvironmentVariable('Path', 'User'), 'User'); `$env:Path = `"$binDir;`$env:Path`""
        }
    }
    finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
}
