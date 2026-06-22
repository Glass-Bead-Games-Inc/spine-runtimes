# Fast dev rebuild of the GDExtension: builds ONLY the editor library target
# (skips the debug/release export templates that build-extension.ps1 also
# builds), for the quickest edit -> reload loop during development.
#
# Produces example-v4-extension/bin/windows/libspine_godot.windows.editor.x86_64.dll,
# which a stock Godot 4.6 editor loads via spine_godot_extension.gdextension.
#
# Requires .\setup-extension.ps1 4.6-stable false to have been run first.

. "$PSScriptRoot\_spine_bash.ps1"
Invoke-SpineBash 'cd .. && scons -j"$(nproc)" target=editor'
