# PowerShell wrapper for setup.sh (runs it through Git Bash).
#
# Clones Godot next to the repo and copies spine-cpp into the module. Run once
# (or when changing Godot version). Editor module build path.
#
# Usage:
#   .\setup.ps1 <godot-branch-or-tag> <dev:true|false> [mono:true|false] [godot-repo]
# Example:
#   .\setup.ps1 4.3-stable true

. "$PSScriptRoot\_spine_bash.ps1"
$argline = ($args | ForEach-Object { "'$_'" }) -join ' '
Invoke-SpineBash "./setup.sh $argline"
