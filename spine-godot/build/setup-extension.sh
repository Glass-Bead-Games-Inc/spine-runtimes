#!/bin/bash
set -e

dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"
pushd "$dir" > /dev/null

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    echo "Usage: ./setup-extension.sh <Godot version> <dev:true|false>"
    echo
    echo "e.g.:"
    echo "       ./setup-extension.sh 4.2.2-stable true"

    exit 1
fi

godot_branch=${1%/}
dev=${2%/}
mono=false
godot_cpp_repo=https://github.com/godotengine/godot-cpp.git
godot_repo=https://github.com/godotengine/godot.git

if [[ $# -eq 3 ]]; then
    mono=${3%/}
fi

if [ "$dev" != "true" ] && [ "$dev" != "false" ]; then
    echo "Invalid value for the 'dev' argument. It should be either 'true' or 'false'."
    exit 1
fi

if [ "$mono" != "true" ] && [ "$mono" != "false" ]; then
    echo "Invalid value for the 'mono' argument. It should be either 'true' or 'false'."
    exit 1
fi

godot_cpp_branch=$(echo $godot_branch | cut -d. -f1-2 | cut -d- -f1)

# godot-cpp lags the engine: it has no 4.6/4.7 branch yet (latest is 4.5). When no
# version-matched branch exists, build against the latest stable godot-cpp *tag*
# rather than the moving 'master'. A 4.5-built extension loads in 4.6/4.7 via
# forward-compatibility (compatibility_minimum=4.1), it's reproducible, and it
# avoids master's breaking changes. The exact-match path auto-selects a real
# 4.6/4.7 branch once godot-cpp ships one.
godot_cpp_fallback_tag="godot-4.5-stable"

if ! git ls-remote --exit-code --heads $godot_cpp_repo $godot_cpp_branch > /dev/null 2>&1; then
    echo "godot-cpp branch '$godot_cpp_branch' not found, falling back to tag '$godot_cpp_fallback_tag'"
    godot_cpp_branch="$godot_cpp_fallback_tag"
fi

cpus=2
if [ "$OSTYPE" == "msys" ]; then
	cpus=$NUMBER_OF_PROCESSORS
elif [[ "$OSTYPE" == "darwin"* ]]; then
	cpus=$(sysctl -n hw.logicalcpu)
else
	cpus=$(grep -c ^processor /proc/cpuinfo)
fi

echo "godot-cpp branch: $godot_cpp_branch"
echo "godot branch: $godot_branch"
echo "dev: $dev"
echo "mono: $mono"
echo "cpus: $cpus"

pushd ..

rm -rf godot-cpp
git clone --depth 1 $godot_cpp_repo -b $godot_cpp_branch

rm -rf example-v4-extension/bin
mkdir -p example-v4-extension/bin

if [ $dev == "true" ]; then
    echo "Dev build, creating godot-cpp/dev"
    touch godot-cpp/dev
    rm -rf godot
    git clone --depth 1 $godot_repo -b $godot_branch
    pushd godot
    scons target=editor dev_build=true optimize=debug --jobs=$cpus
    popd
fi

cp spine_godot_extension.gdextension example-v4-extension/bin
# Ship the editor icons next to the .gdextension (referenced relatively in its [icons] section)
mkdir -p example-v4-extension/bin/icons
cp spine_godot/icons/Spine*.svg example-v4-extension/bin/icons/
rm -rf spine_godot/spine-cpp
cp -r ../spine-cpp spine_godot

popd
popd > /dev/null