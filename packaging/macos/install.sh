#!/bin/bash
# Installs a VAESynth build for the current user: the one in this folder, or
# the folder given as the first argument.
# Run from Terminal:  bash ~/Downloads/VAESynth-macOS/install.sh
#
# The CI build is not notarised, so after copying each plug-in this clears the
# download quarantine, restores executable permissions (lost in the zip) and
# signs it ad hoc so macOS lets the DAW load it.

cd "${1:-$(dirname "$0")}" || exit 1
PLUGINS="$HOME/Library/Audio/Plug-Ins"

prepare() {
    find "$1/Contents/MacOS" -type f -exec chmod +x {} + 2>/dev/null
    xattr -dr com.apple.quarantine "$1" 2>/dev/null
    codesign --force --deep --sign - "$1" >/dev/null 2>&1
}

install_bundle() {
    local source="$1" folder="$2"
    [ -e "$source" ] || return 0
    mkdir -p "$folder"
    local target="$folder/$(basename "$source")"
    rm -rf "$target"
    cp -R "$source" "$folder/" && prepare "$target" && echo "Installed $target"
}

install_bundle VST3/VAESynth.vst3        "$PLUGINS/VST3"
install_bundle AU/VAESynth.component     "$PLUGINS/Components"
install_bundle CLAP/VAESynth.clap        "$PLUGINS/CLAP"
install_bundle Standalone/VAESynth.app   "/Applications" || install_bundle Standalone/VAESynth.app "$HOME/Applications"

# Make Logic and other AU hosts pick up the new component.
killall -9 AudioComponentRegistrar 2>/dev/null

echo
echo "Done. Restart your DAW and rescan plug-ins."
