#!/bin/bash
# Rebuilds the development workspace the tools in DevTools expect, on Linux
# (e.g. a fresh Claude session). Everything goes under /home/claude, exactly
# where it was when the tools were written, so none of them need editing.
#
#   bash DevTools/setup_workspace.sh            the tools, JUCE and pluginval
#   bash DevTools/setup_workspace.sh --audio    ...plus the test audio (about 1 GB, takes a while)
#
# Needs: git, curl, unzip, cmake, a C++17 compiler, JUCE's Linux packages
# (libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev
# libxinerama-dev libxcursor-dev libgl1-mesa-dev), xvfb (for the window tests
# and pluginval), and for --audio: ffmpeg, python3 with numpy, scipy, matplotlib.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
D="$REPO/DevTools"
W=/home/claude

mkdir -p "$W/deps"

# ---- JUCE 9.0.4 and pluginval 1.0.4 -----------------------------------------
if [ ! -d "$W/deps/juce-9.0.4" ]; then
    git clone --depth 1 --branch 9.0.4 https://github.com/juce-framework/JUCE.git "$W/deps/juce-9.0.4"
fi

if [ ! -x "$W/deps/pluginval/extracted/pluginval" ]; then
    mkdir -p "$W/deps/pluginval/extracted"
    curl -fL -o "$W/deps/pluginval/pluginval_Linux.zip" \
        https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Linux.zip
    (cd "$W/deps/pluginval/extracted" && unzip -o ../pluginval_Linux.zip)
fi

# ---- The plugins (a working copy) and the tools, where they expect to be -----
if [ ! -d "$W/EasyPlugins" ]; then
    mkdir -p "$W/EasyPlugins"
    (cd "$REPO" && tar --exclude=.git --exclude=DevTools --exclude=out -cf - .) | (cd "$W/EasyPlugins" && tar -xf -)
fi

place() {   # place <folder in DevTools> <workspace folder>
    mkdir -p "$2"
    cp -a "$D/$1/." "$2/"
}

place Tests            "$W/verify2"
place PresetTuning     "$W/presetcheck"
place Checks/GateEngine "$W/gatecheck"
place Checks/Host      "$W/gatecheck/host"
place Checks/DSEngine  "$W/dscheck"
place Checks/Golden    "$W/golden"
place Compare          "$W/opt/harness2"
place Bench            "$W/bench2"
place FastMathFit      "$W/opt/fit"

echo "Tools in place. (Build them with cmake: see DevTools/README.txt.)"

# ---- The test audio (optional) ----------------------------------------------
if [ "${1:-}" != "--audio" ]; then
    exit 0
fi

S="$W/samples"
mkdir -p "$S"

sparse() {   # sparse <folder> <repository> <commit> <patterns...>
    local dir="$S/$1" url="$2" commit="$3"
    shift 3
    if [ ! -d "$dir/.git" ]; then
        git clone --filter=blob:none --no-checkout "$url" "$dir"
        git -C "$dir" sparse-checkout set --no-cone "$@"
        git -C "$dir" checkout "$commit"
    fi
}

sparse librosa-data https://github.com/librosa/data.git 38f4b06556fa0ff1acda5e677d8ba05d1bc0fff0 \
    '/audio/*.hq.ogg' '/index.json' '/audio/snare-accelerate.ogg'
sparse tonejs-audio https://github.com/Tonejs/audio.git efd8296360f9526e379bfbe5c1698ff54d6a1d34 \
    '/drum-samples/acoustic-kit/*' '/drum-samples/Kit3/*' '/drum-samples/Kit8/*' '/drum-samples/breakbeat8/*' \
    '/drum-samples/breakbeat9/*' '/drum-samples/breakbeat13/*' '/drum-samples/loops/*' '/drum-samples/*.mp3' '/loop/*.mp3'
sparse tonejs-instruments https://github.com/nbrosowsky/tonejs-instruments.git 622c2f1c32c8cfce4158ddc3eb26e518ddef37e5 \
    '/samples/bass-electric/*.wav' '/samples/guitar-acoustic/*.wav' '/samples/guitar-electric/*.wav' \
    '/samples/guitar-nylon/*.wav' '/samples/piano/*.wav' '/samples/saxophone/*.wav' '/samples/trumpet/*.wav' \
    '/samples/trombone/*.wav' '/samples/french-horn/*.wav' '/samples/violin/*.wav' '/samples/cello/*.wav' \
    '/samples/flute/*.wav' '/samples/organ/*.wav' '/samples/harmonium/*.wav' '/samples/contrabass/*.wav'

fetch() {   # fetch <url list> <folder>: each file, flat into the folder (skipping ones already there)
    mkdir -p "$2"
    while read -r url; do
        [ -n "$url" ] || continue
        local file="$2/${url##*/}"
        [ -s "$file" ] || curl -fsL -o "$file" "$url"
    done < "$1"
}

fetch "$D/TestAudio/audiomnist_urls.txt" "$S/audiomnist/wav"
cp "$D/TestAudio/audiomnist_meta.json" "$S/audiomnist/meta.json"
fetch "$D/TestAudio/virtuosity_urls.txt" "$S/virtuosity/flac"

# The arranged tracks the tests and preset tuning use
python3 "$W/presetcheck/make_material.py"
python3 "$W/presetcheck/gate_material.py"
python3 "$W/presetcheck/ds_material.py"
echo "Test audio built in $W/presetcheck/material, gate_material and ds_material."
