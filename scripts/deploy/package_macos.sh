#!/bin/bash
set -e

# --- Configuration ---
BINARY_NAME="OpenDriveViewer"
TARGET="//src/app:OpenDriveViewer"
DIST_DIR="dist/mac"
APP_BUNDLE="${DIST_DIR}/${BINARY_NAME}.app"
DMG_FILE="${DIST_DIR}/${BINARY_NAME}.dmg"

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
rm -rf "${DIST_DIR}"
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
find "${APP_BUNDLE}/Contents/Frameworks" -type f -name "*.dylib" -exec strip -x {} + 2>/dev/null || true

# --- Handle PROJ ---
echo "Bundling PROJ..."
# Detect exact PROJ linkage path from binary
PROJ_LIB_ORIGINAL=$(otool -L "${APP_BUNDLE}/Contents/MacOS/${BINARY_NAME}" | grep libproj | awk '{print $1}' | xargs)

if [ -n "$PROJ_LIB_ORIGINAL" ]; then
    echo "Found libproj linkage at: $PROJ_LIB_ORIGINAL"
    
    # Use global PROJ_ROOT to find the actual library file to copy
    # We look for the base name but prefer the one matching the original path suffix if possible
    PROJ_LIB_NAME=$(basename "$PROJ_LIB_ORIGINAL")
    PROJ_LIB_REAL_PATH=$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "${PROJ_ROOT}/lib/${PROJ_LIB_NAME}")
    
    cp "${PROJ_LIB_REAL_PATH}" "${APP_BUNDLE}/Contents/Frameworks/${PROJ_LIB_NAME}"
    chmod +w "${APP_BUNDLE}/Contents/Frameworks/${PROJ_LIB_NAME}"
    
    # Fixup libproj ID and refs
    install_name_tool -id "@executable_path/../Frameworks/${PROJ_LIB_NAME}" "${APP_BUNDLE}/Contents/Frameworks/${PROJ_LIB_NAME}"
    install_name_tool -change "${PROJ_LIB_ORIGINAL}" "@executable_path/../Frameworks/${PROJ_LIB_NAME}" "${APP_BUNDLE}/Contents/MacOS/${BINARY_NAME}"
else
    echo "Warning: libproj linkage not found in binary. Linking might be static or missing."
fi

# Copy PROJ data files
mkdir -p "${APP_BUNDLE}/Contents/Resources/proj"
cp "${PROJ_ROOT}/share/proj/proj.db" "${APP_BUNDLE}/Contents/Resources/proj/"
cp "${PROJ_ROOT}/share/proj/proj.ini" "${APP_BUNDLE}/Contents/Resources/proj/"
# Other small metadata/jsons
find "${PROJ_ROOT}/share/proj/" -maxdepth 1 -type f -not -name "*.tif" -not -name "*.tiff" -not -name "*.gtiff" -exec cp {} "${APP_BUNDLE}/Contents/Resources/proj/" \;

# --- Metadata ---
echo "Updating Info.plist..."
cat > "${APP_BUNDLE}/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>${BINARY_NAME}</string>
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
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
EOF

# Signing inside-out is mandatory for Apple Silicon
echo "Signing components..."
# 1. Sign Frameworks (as bundles)
find "${APP_BUNDLE}/Contents/Frameworks" -name "*.framework" -type d -exec codesign --force --sign - --timestamp=none {} \; 2>/dev/null || true
# 2. Sign standalone dylibs (regular files only, skip symlinks)
find "${APP_BUNDLE}/Contents/Frameworks" "${APP_BUNDLE}/Contents/PlugIns" -name "*.dylib" -type f -exec codesign --force --sign - --timestamp=none {} \; 2>/dev/null || true
# 3. Sign any other executable or plugin files
find "${APP_BUNDLE}/Contents/PlugIns" -type f -not -name "*.dylib" -exec codesign --force --sign - --timestamp=none {} \; 2>/dev/null || true
# 4. Sign the main binary and the app bundle
codesign --force --sign - --timestamp=none "${APP_BUNDLE}/Contents/MacOS/${BINARY_NAME}"
codesign --force --sign - --timestamp=none "${APP_BUNDLE}"

# Verify signature
echo "Verifying signature..."
codesign --verify --verbose "${APP_BUNDLE}"

# --- Create DMG ---
echo "Creating DMG..."
rm -f "${DMG_FILE}"
# Create a temporary folder for the DMG content to avoids including script artifacts if any
DMG_TEMP="dist/dmg_temp"
rm -rf "${DMG_TEMP}"
mkdir -p "${DMG_TEMP}"
ln -s /Applications "${DMG_TEMP}/Applications"
cp -R "${APP_BUNDLE}" "${DMG_TEMP}/"

hdiutil create -volname "${BINARY_NAME}" -srcfolder "${DMG_TEMP}" -ov -format UDZO "${DMG_FILE}"
rm -rf "${DMG_TEMP}"

echo "Done! DMG is available at ${DMG_FILE}"
