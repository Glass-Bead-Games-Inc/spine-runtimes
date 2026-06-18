# Launches the freshly built dev editor on the spine-godot example project.
#
# Use after .\build-v4.ps1 to run the per-task visual checks from the
# implementation plan. Extra args are forwarded to the Godot binary.
#
# Usage:
#   .\run-example.ps1                 # open the example project in the editor
#   .\run-example.ps1 --path ../example scene.tscn

$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot '..\godot\bin\godot.windows.editor.dev.x86_64.exe'
if (-not (Test-Path $exe)) {
	throw "Editor not built yet: $exe`nRun .\build-v4.ps1 first (and .\setup.ps1 before that)."
}
$proj = Resolve-Path (Join-Path $PSScriptRoot '..\example')
if ($args.Count -gt 0) {
	& $exe @args
} else {
	& $exe --path $proj --editor
}
