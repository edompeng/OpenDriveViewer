#!/bin/bash
set -e

# --- Configuration ---
BINARY_NAME="OpenDriveViewer"
DIST_DIR="dist/linux"
BUNDLE_DIR="${DIST_DIR}/${BINARY_NAME}_linux_x64"

# --- Check Environment ---
: "${QT6_ROOT:?Error: QT6_ROOT environment variable is not set.}"
: "${PROJ_ROOT:?Error: PROJ_ROOT environment variable is not set.}"

# --- Setup Bundle ---
echo "Preparing Linux Bundle..."
rm -rf "${BUNDLE_DIR}"
mkdir -p "${BUNDLE_DIR}/bin"
mkdir -p "${BUNDLE_DIR}/lib"
mkdir -p "${BUNDLE_DIR}/lib/plugins"
mkdir -p "${BUNDLE_DIR}/share/proj"

# Copy binary
if [ -f "bazel-bin/src/app/${BINARY_NAME}" ]; then
    cp "bazel-bin/src/app/${BINARY_NAME}" "${BUNDLE_DIR}/bin/"
elif [ -f "build/bin/${BINARY_NAME}" ]; then
    cp "build/bin/${BINARY_NAME}" "${BUNDLE_DIR}/bin/"
else
    echo "Error: Binary ${BINARY_NAME} not found in bazel-bin/ or build/bin/."
    exit 1
fi
chmod +w "${BUNDLE_DIR}/bin/${BINARY_NAME}"

# Extract symbols
echo "Extracting debug symbols..."
objcopy --only-keep-debug "${BUNDLE_DIR}/bin/${BINARY_NAME}" "${DIST_DIR}/${BINARY_NAME}.debug"
strip --strip-unneeded "${BUNDLE_DIR}/bin/${BINARY_NAME}"
objcopy --add-gnu-debuglink="${DIST_DIR}/${BINARY_NAME}.debug" "${BUNDLE_DIR}/bin/${BINARY_NAME}"

# Package symbols
tar -czvf "${BINARY_NAME}_linux_symbols.tar.gz" -C "${DIST_DIR}" "${BINARY_NAME}.debug"

# --- Copy Dependencies ---
echo "Collecting shared libraries..."
# Copy found libraries, skipping standard system libraries to avoid startup crashes
while read -r lib; do
    lib_name=$(basename "$lib")
    case "$lib_name" in
        ld-linux*|ld-musl*|libc.*|libpthread.*|libdl.*|libm.*|librt.*|libgcc_s.*|libstdc++.*|libresolv.*|libutil.*|\
        libGL.*|libEGL.*|libGLdispatch.*|libGLX.*|libOpenGL.*|libdrm.*|libglapi.*|libgbm.*|\
        libxcb*|libX11*|libX11-xcb*|libwayland*|libasound*|libfontconfig*|libfreetype*|libdbus*|libuuid*|libudev*|libz.*|\
        libglib-*|libgobject-*|libgthread-*|libgmodule-*|libgio-*)
            continue
            ;;
    esac

    cp "$lib" "${BUNDLE_DIR}/lib/"
    if [ ! -L "${BUNDLE_DIR}/lib/$lib_name" ]; then
        chmod +w "${BUNDLE_DIR}/lib/$lib_name"
        strip --strip-unneeded "${BUNDLE_DIR}/lib/$lib_name" 2>/dev/null || true
    fi
done < <(ldd "${BUNDLE_DIR}/bin/${BINARY_NAME}" | grep "=> /" | awk '{print $3}')

# --- Copy Qt Plugins (Essential for display/platform) ---
echo "Copying Qt plugins..."
cp -R "${QT6_ROOT}/plugins/"* "${BUNDLE_DIR}/lib/plugins/"
# Strip plugins too
find "${BUNDLE_DIR}/lib/plugins" -type f -name "*.so" -exec strip --strip-unneeded {} + 2>/dev/null || true

# --- Copy PROJ Data (selective) ---
echo "Copying PROJ data..."
find "${PROJ_ROOT}/share/proj/" -maxdepth 1 -type f -not -name "*.tif" -not -name "*.tiff" -not -name "*.gtiff" -exec cp {} "${BUNDLE_DIR}/share/proj/" \;

# --- Relink ---
echo "Adjusting rpaths..."
# Check if patchelf is available
if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/../lib' "${BUNDLE_DIR}/bin/${BINARY_NAME}"
    # Also fixup libraries themselves
    for lib in "${BUNDLE_DIR}/lib/"*.so*; do
        if [ ! -L "$lib" ]; then
            patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true
        fi
    done
else
    echo "Warning: patchelf not found. Binary might not find bundled libs automatically."
fi

# --- Create Launcher ---
echo "Creating launcher script..."
cat > "${BUNDLE_DIR}/run_geoviewer.sh" <<EOF
#!/bin/bash
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
export LD_LIBRARY_PATH="\$DIR/lib:\${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="\$DIR/lib/plugins"
export PROJ_LIB="\$DIR/share/proj"
export XDG_SESSION_TYPE=x11 # Potential workaround for some wayland issues
exec "\$DIR/bin/${BINARY_NAME}" "\$@"
EOF
chmod +x "${BUNDLE_DIR}/run_geoviewer.sh"

echo "Done! Linux package is available at ${BUNDLE_DIR}"
echo "Run it with: ./${BINARY_NAME}_linux_x64/run_geoviewer.sh"
