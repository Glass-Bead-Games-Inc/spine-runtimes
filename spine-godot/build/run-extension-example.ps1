# Launches the installed Godot 4.6 editor on the GDExtension example project
# (example-v4-extension), which loads the freshly built spine DLL.
#
# Use after .\dev-extension.ps1 / .\build-extension.ps1 for the per-task visual
# checks. Extra args are forwarded to the Godot binary (e.g. pass
# "--headless --quit" to verify the extension loads without opening a window).
#
# Usage:
#   .\run-extension-example.ps1                 # open the project in the editor
#   .\run-extension-example.ps1 --headless --quit

$ErrorActionPreference = 'Stop'
$godot = (Get-Command godot -ErrorAction SilentlyContinue)?.Source
if (-not $godot) { $godot = Join-Path $env:USERPROFILE 'scoop\apps\godot\current\godot.exe' }
if (-not (Test-Path $godot)) {
	throw "Godot 4.6 editor not found. Set `$godot in this script or put godot on PATH."
}
$proj = Resolve-Path (Join-Path $PSScriptRoot '..\example-v4-extension')
if ($args.Count -gt 0) {
	& $godot --path $proj @args
} else {
	& $godot --path $proj --editor
}
