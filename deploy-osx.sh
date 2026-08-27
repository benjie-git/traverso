#!/bin/bash
set -e

###                                                    ###
#    This script is used to create a bundle for OS X     #
###                                                    ###

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Find traverso binary
BIN_PATH=""
if [ -n "$1" ] && [ -f "$1" ]; then
    BIN_PATH="$1"
elif [ -f "build/bin/traverso" ]; then
    BIN_PATH="build/bin/traverso"
elif [ -f "bin/traverso" ]; then
    BIN_PATH="bin/traverso"
else
    echo "Error: traverso binary not found. Please build the project first."
    exit 1
fi

echo "Using traverso binary: $BIN_PATH"

# Find macdeployqt
MACDEPLOYQT=""
if command -v macdeployqt6 >/dev/null 2>&1; then
    MACDEPLOYQT="$(command -v macdeployqt6)"
else
    echo "Error: macdeployqt not found."
    exit 1
fi

echo "Using macdeployqt: $MACDEPLOYQT"

# Clean up previous bundle to prevent permission conflicts
if [ -d "Traverso.app" ]; then
    echo "Removing previous Traverso.app..."
    chmod -R u+w Traverso.app 2>/dev/null || true
    rm -rf Traverso.app
fi

mkdir -p Traverso.app/Contents/MacOS
mkdir -p Traverso.app/Contents/Resources

cp "$BIN_PATH" Traverso.app/Contents/MacOS/traverso
chmod +x Traverso.app/Contents/MacOS/traverso

if [ -f "resources/images/traverso_mac.icns" ]; then
    cp resources/images/traverso_mac.icns Traverso.app/Contents/Resources/Traverso.icns
fi

if [ -f "resources/Info.plist" ]; then
    cp resources/Info.plist Traverso.app/Contents/Info.plist
fi

EXTRA_EXEC_FLAGS=()

# Locate and include optional helper binaries (sox, cdrdao)
SOX_BIN=""
if command -v sox >/dev/null 2>&1; then
    SOX_BIN="$(command -v sox)"
elif [ -x "/opt/homebrew/bin/sox" ]; then
    SOX_BIN="/opt/homebrew/bin/sox"
elif [ -x "/usr/local/bin/sox" ]; then
    SOX_BIN="/usr/local/bin/sox"
fi

if [ -n "$SOX_BIN" ]; then
    echo "Bundling sox from: $SOX_BIN"
    cp "$SOX_BIN" Traverso.app/Contents/MacOS/sox
    chmod +x Traverso.app/Contents/MacOS/sox
    EXTRA_EXEC_FLAGS+=("-executable=Traverso.app/Contents/MacOS/sox")
fi

CDRDAO_BIN=""
if command -v cdrdao >/dev/null 2>&1; then
    CDRDAO_BIN="$(command -v cdrdao)"
elif [ -x "/opt/homebrew/bin/cdrdao" ]; then
    CDRDAO_BIN="/opt/homebrew/bin/cdrdao"
elif [ -x "/usr/local/bin/cdrdao" ]; then
    CDRDAO_BIN="/usr/local/bin/cdrdao"
fi

if [ -n "$CDRDAO_BIN" ]; then
    echo "Bundling cdrdao from: $CDRDAO_BIN"
    cp "$CDRDAO_BIN" Traverso.app/Contents/MacOS/cdrdao
    chmod +x Traverso.app/Contents/MacOS/cdrdao
    EXTRA_EXEC_FLAGS+=("-executable=Traverso.app/Contents/MacOS/cdrdao")
fi

# Run macdeployqt to deploy Qt frameworks, plugins and dylib dependencies
echo "Running macdeployqt..."
"$MACDEPLOYQT" ./Traverso.app -verbose=1 "${EXTRA_EXEC_FLAGS[@]}"

# Re-sign the bundle (required on macOS ARM64/Apple Silicon after install_name_tool modifications)
SIGN_IDENTITY="${CODE_SIGN_IDENTITY:--}"
echo "Signing Traverso.app with identity '${SIGN_IDENTITY}'..."
codesign --force --deep -s "$SIGN_IDENTITY" ./Traverso.app

echo "Verifying code signature..."
codesign -vvv --deep --strict ./Traverso.app

echo "Successfully built and signed Traverso.app"
