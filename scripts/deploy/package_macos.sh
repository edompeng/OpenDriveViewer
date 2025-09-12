#!/bin/bash
set -e

# --- Configuration ---
BINARY_NAME="OpenDriveViewer"
TARGET="//src/app:OpenDriveViewer"
DIST_DIR="dist/mac"
APP_BUNDLE="${DIST_DIR}/${BINARY_NAME}.app"

# --- Check Environment ---
if [ -z "$QT6_ROOT" ]; then
    echo "Error: QT6_ROOT environment variable is not set."
    exit 1
fi

if [ -z "$PROJ_ROOT" ]; then
    echo "Error: PROJ_ROOT environment variable is not set."
    exit 1
fi

MACDEPLOYQT="${QT6_ROOT}/bin/macdeployqt"
if [ ! -f "$MACDEPLOYQT" ]; then
    echo "Error: macdeployqt not found at $MACDEPLOYQT"
    exit 1
fi

# --- Setup Bundle ---
echo "Preparing App Bundle..."
rm -rf "${APP_BUNDLE}"
mkdir -p "${APP_BUNDLE}/Contents/MacOS"
mkdir -p "${APP_BUNDLE}/Contents/Resources"
mkdir -p "${APP_BUNDLE}/Contents/Frameworks"

# Copy binary
cp "bazel-bin/src/app/${BINARY_NAME}" "${APP_BUNDLE}/Contents/MacOS/"
chmod +w "${APP_BUNDLE}/Contents/MacOS/${BINARY_NAME}"
chmod +x "${APP_BUNDLE}/Contents/MacOS/${BINARY_NAME}"
strip -x "${APP_BUNDLE}/Contents/MacOS/${BINARY_NAME}"

# --- Run macdeployqt ---
echo "Running macdeployqt..."
# This will fix up Qt frameworks and dylibs
"${MACDEPLOYQT}" "${APP_BUNDLE}" -verbose=1

# Strip all dylibs/frameworks in the bundle to reduce size
find "${APP_BUNDLE}/Contents/Frameworks" -type f -name "*.dylib" -exec strip -x {} +

# Helper function for realpath (readlink -f is not portable on macOS)
get_realpath() {
    python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$1"
}

# --- Handle PROJ ---
echo "Bundling PROJ..."
# Copy PROJ library (find the actual dylib, resolve symlinks)
PROJ_LIB_SRC=$(get_realpath "${PROJ_ROOT}/lib/libproj.dylib")
PROJ_LIB_NAME=$(basename "${PROJ_LIB_SRC}")
cp "${PROJ_LIB_SRC}" "${APP_BUNDLE}/Contents/Frameworks/"
chmod +w "${APP_BUNDLE}/Contents/Frameworks/${PROJ_LIB_NAME}"
strip -x "${APP_BUNDLE}/Contents/Frameworks/${PROJ_LIB_NAME}"

# Fixup libproj ID and refs
install_name_tool -id "@executable_path/../Frameworks/${PROJ_LIB_NAME}" "${APP_BUNDLE}/Contents/Frameworks/${PROJ_LIB_NAME}"
install_name_tool -change "${PROJ_LIB_SRC}" "@executable_path/../Frameworks/${PROJ_LIB_NAME}" "${APP_BUNDLE}/Contents/MacOS/${BINARY_NAME}"

# Copy PROJ data files (being selective to save space)
mkdir -p "${APP_BUNDLE}/Contents/Resources/proj"
# Essential databases and config
cp "${PROJ_ROOT}/share/proj/proj.db" "${APP_BUNDLE}/Contents/Resources/proj/"
cp "${PROJ_ROOT}/share/proj/proj.ini" "${APP_BUNDLE}/Contents/Resources/proj/"
# Other small metadata/jsons
find "${PROJ_ROOT}/share/proj/" -maxdepth 1 -type f -not -name "*.tif" -not -name "*.tiff" -not -name "*.gtiff" -exec cp {} "${APP_BUNDLE}/Contents/Resources/proj/" \;

# Note: Many PROJ geoid grid files (.tif) are huge (100MB+ each). 
# We exclude them by default above. If specific grids are needed, 
# they should be manually added.

# Create a shell wrapper or Info.plist to set PROJ_LIB
# For simplicity, we create a script that sets the env var
cat > "${APP_BUNDLE}/Contents/MacOS/launcher.sh" <<EOF
#!/bin/bash
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
export PROJ_LIB="\$DIR/../Resources/proj"
exec "\$DIR/${BINARY_NAME}" "\$@"
EOF
chmod +x "${APP_BUNDLE}/Contents/MacOS/launcher.sh"

# Create Info.plist
cat > "${APP_BUNDLE}/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>launcher.sh</string>
    <key>CFBundleIconFile</key>
    <string></string>
    <key>CFBundleIdentifier</key>
    <string>com.geoviewer.opendrive</string>
    <key>CFBundleName</key>
    <string>${BINARY_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>0.1.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
</dict>
</plist>
EOF

# --- Code Signing (Crucial for Apple Silicon) ---
echo "Re-signing bundle..."
# Ad-hoc sign everything to fix broken signatures after install_name_tool/macdeployqt
codesign --force --deep --sign - "${APP_BUNDLE}"

echo "Done! Package is available at ${APP_BUNDLE}"
