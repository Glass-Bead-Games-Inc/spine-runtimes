# Shared helper for the spine-godot PowerShell build wrappers.
#
# Two Windows-specific problems these wrappers solve, without touching the
# upstream .sh scripts (so they stay in sync with the project's CI):
#
#   1. The `bash.exe` Windows ships in System32 is WSL, which has no MSVC
#      toolchain. We resolve *Git Bash* explicitly instead.
#   2. The Microsoft Store build of Python installs console scripts (scons.exe)
#      into a sandboxed per-user dir that is not on PATH. We locate it and
#      prepend it to PATH for the build subprocess only (not persisted).

$ErrorActionPreference = 'Stop'

function Get-GitBash {
	$candidates = @()
	$gitCmd = Get-Command git -ErrorAction SilentlyContinue
	if ($gitCmd) {
		# git is at <GitRoot>\cmd\git.exe or <GitRoot>\bin\git.exe; bash is at <GitRoot>\bin\bash.exe
		$gitRoot = Split-Path -Parent (Split-Path -Parent $gitCmd.Source)
		$candidates += (Join-Path $gitRoot 'bin\bash.exe')
	}
	$candidates += (Join-Path $env:ProgramFiles 'Git\bin\bash.exe')
	if (${env:ProgramFiles(x86)}) { $candidates += (Join-Path ${env:ProgramFiles(x86)} 'Git\bin\bash.exe') }
	foreach ($c in $candidates) {
		if ($c -and (Test-Path $c)) { return $c }
	}
	throw "Git Bash not found. Install Git for Windows from https://git-scm.com/download/win. (Do NOT use the System32 bash.exe - that is WSL.)"
}

function Add-SconsToPath {
	# No-op if the scons console script is already resolvable.
	if (Get-Command scons -ErrorAction SilentlyContinue) { return }
	$userSite = (& python -c "import site; print(site.getusersitepackages())" 2>$null)
	if ($userSite) {
		$scriptsDir = Join-Path (Split-Path -Parent $userSite) 'Scripts'
		if (Test-Path (Join-Path $scriptsDir 'scons.exe')) {
			$env:PATH = "$scriptsDir;$env:PATH"
			Write-Host "[scons] added to PATH for this build: $scriptsDir" -ForegroundColor DarkGray
			return
		}
	}
	throw "scons not found on PATH and could not be auto-located. Install it with: python -m pip install scons==4.7.0"
}

function Invoke-SpineBash([string]$Command) {
	$bash = Get-GitBash
	Add-SconsToPath
	$buildDir = ($PSScriptRoot -replace '\\', '/')
	Write-Host "[git-bash] $Command" -ForegroundColor Cyan
	# Non-login shell so the PATH we inherit (incl. the scons dir above) is used as-is.
	& $bash -c "cd '$buildDir' && $Command"
	if ($LASTEXITCODE -ne 0) { throw "Build step failed (exit $LASTEXITCODE): $Command" }
}
