# PowerShell wrapper for setup-extension.sh (runs it through Git Bash).
#
# Clones godot-cpp next to the repo for the GDExtension build configuration.
#
# Usage:
#   .\setup-extension.ps1 <godot-version> <dev:true|false> [mono:true|false]
# Example:
#   .\setup-extension.ps1 4.3-stable true

. "$PSScriptRoot\_spine_bash.ps1"
$argline = ($args | ForEach-Object { "'$_'" }) -join ' '
Invoke-SpineBash "./setup-extension.sh $argline"
