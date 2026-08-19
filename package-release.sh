#!/bin/bash
set -e

BUILD_DIR="build"
RELEASE_NAME="Reverbo-v1.0.0"
RELEASE_DIR="/tmp/$RELEASE_NAME"

echo "📦 Building release: $RELEASE_NAME"

# Clean build
rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR" && cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
cd ..

# Collect binaries
mkdir -p "$RELEASE_DIR/VST3" "$RELEASE_DIR/AU"
cp -r "$BUILD_DIR/Reverbo_artefacts/Release/Standalone/Reverbo.app" "$RELEASE_DIR/"
cp -r ~/Library/Audio/Plug-Ins/VST3/Reverbo.vst3 "$RELEASE_DIR/VST3/"
cp -r ~/Library/Audio/Plug-Ins/Components/Reverbo.component "$RELEASE_DIR/AU/"

# Create a zip
cd /tmp
zip -r "$RELEASE_NAME.zip" "$RELEASE_NAME"
echo "✅ Release ready: /tmp/$RELEASE_NAME.zip"
