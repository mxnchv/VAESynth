#!/bin/bash
# Builds VAESynth on this Mac and installs it for the current user.
# Run from the repository folder:  bash packaging/macos/build-and-install.sh
#
# Needs Xcode (App Store) and CMake (brew install cmake). The first build
# downloads JUCE and takes a few minutes; later builds take seconds.

set -e
cd "$(dirname "$0")/../.."

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
cmake --build build --config Release --parallel "$(sysctl -n hw.ncpu)"

bash packaging/macos/install.sh build/VAESynth_artefacts/Release
