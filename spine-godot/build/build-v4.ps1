# PowerShell wrapper for build-v4.sh (runs it through Git Bash).
#
# Builds the Godot 4.x editor with the spine_godot module. Requires .\setup.ps1
# to have been run first (so ..\godot exists).
#
# Usage:
#   .\build-v4.ps1 [mono:true|false]
# Example:
#   .\build-v4.ps1

. "$PSScriptRoot\_spine_bash.ps1"
$argline = ($args | ForEach-Object { "'$_'" }) -join ' '
Invoke-SpineBash "./build-v4.sh $argline"
