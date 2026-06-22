# PowerShell wrapper for build-extension.sh (runs it through Git Bash).
#
# Builds the spine_godot GDExtension shared library (editor + debug/release
# templates) for the current platform into example-v4-extension/bin/. The
# resulting DLL is loaded by a stock Godot editor via the bundled
# spine_godot_extension.gdextension - no custom engine build required.
#
# Requires .\setup-extension.ps1 <godot-version> <dev:true|false> first.
#
# Usage:
#   .\build-extension.ps1            # build for the current platform
#   .\build-extension.ps1 windows    # explicit platform

. "$PSScriptRoot\_spine_bash.ps1"
$argline = ($args | ForEach-Object { "'$_'" }) -join ' '
Invoke-SpineBash "./build-extension.sh $argline"
