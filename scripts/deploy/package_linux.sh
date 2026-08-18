#!/bin/bash
set -e

# --- Configuration ---
BINARY_NAME="OpenDriveViewer"
GEOVIEWER_VERSION="${GEOVIEWER_VERSION:-0.0.0-dev}"
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
printf '%s\n' "${GEOVIEWER_VERSION}" > "${BUNDLE_DIR}/VERSION"

# Extract symbols
echo "Extracting debug symbols..."
objcopy --only-keep-debug "${BUNDLE_DIR}/bin/${BINARY_NAME}" "${DIST_DIR}/${BINARY_NAME}.debug"
strip --strip-unneeded "${BUNDLE_DIR}/bin/${BINARY_NAME}"
objcopy --add-gnu-debuglink="${DIST_DIR}/${BINARY_NAME}.debug" "${BUNDLE_DIR}/bin/${BINARY_NAME}"

# Package symbols
tar -czvf "${BINARY_NAME}_linux_symbols.tar.gz" -C "${DIST_DIR}" "${BINARY_NAME}.debug"

# --- Detect musl vs glibc ---
INTERP=$(readelf -l "${BUNDLE_DIR}/bin/${BINARY_NAME}" 2>/dev/null | grep "interpreter:" | sed 's/.*: \(.*\)]/\1/')
IS_MUSL=false
if echo "$INTERP" | grep -q "ld-musl"; then
    IS_MUSL=true
    echo "Detected musl-linked binary (interpreter: ${INTERP})"
fi

# --- Collect ALL shared library dependencies ---
echo "Collecting shared libraries..."
DEPS_FILE=$(mktemp)

# Helper: extract shared lib paths from ldd output
collect_deps() {
    ldd "$1" 2>/dev/null | grep "=> /" | awk '{print $3}'
}

# 1. Collect deps from main binary
collect_deps "${BUNDLE_DIR}/bin/${BINARY_NAME}" >> "$DEPS_FILE"

# 2. Copy Qt plugins first so we can scan their deps too
echo "Copying Qt plugins..."
cp -R "${QT6_ROOT}/plugins/"* "${BUNDLE_DIR}/lib/plugins/"

# 3. Copy Mesa DRI drivers and vendor libraries (if musl) so we can scan their deps too
if [ "$IS_MUSL" = true ]; then
    MESA_DRI_DIR=""
    for dir in /usr/lib/xorg/modules/dri /usr/lib/dri; do
        if [ -d "$dir" ]; then
            MESA_DRI_DIR="$dir"
            break
        fi
    done

    if [ -n "$MESA_DRI_DIR" ]; then
        echo "Bundling Mesa DRI drivers from ${MESA_DRI_DIR}..."
        mkdir -p "${BUNDLE_DIR}/lib/dri"
        cp -a "${MESA_DRI_DIR}/"*.so "${BUNDLE_DIR}/lib/dri/" 2>/dev/null || true
    fi

    # Copy Mesa GLX/EGL vendor libraries (libGLX_mesa and libEGL_mesa) if they exist,
    # as they are loaded via dlopen by libGL/libEGL and not detected by ldd on the binary.
    for libpath in /usr/lib/libGLX_mesa.so* /usr/lib/libEGL_mesa.so*; do
        if [ -e "$libpath" ]; then
            echo "Bundling Mesa vendor library: $libpath"
            cp -a "$libpath" "${BUNDLE_DIR}/lib/"
            collect_deps "$libpath" >> "$DEPS_FILE"
        fi
    done

    # Copy EGL vendor JSON configuration files
    if [ -d "/usr/share/glvnd/egl_vendor.d" ]; then
        echo "Bundling EGL vendor configuration files..."
        mkdir -p "${BUNDLE_DIR}/share/glvnd/egl_vendor.d"
        cp -a /usr/share/glvnd/egl_vendor.d/*.json "${BUNDLE_DIR}/share/glvnd/egl_vendor.d/" 2>/dev/null || true
    fi
fi

# 4. Collect deps from all Qt plugins (they dlopen additional libs at runtime)
find "${BUNDLE_DIR}/lib/plugins" -name "*.so" 2>/dev/null | while read -r plugin; do
    collect_deps "$plugin" >> "$DEPS_FILE"
done

# 5. Collect deps from all Mesa DRI drivers (if musl)
if [ "$IS_MUSL" = true ] && [ -d "${BUNDLE_DIR}/lib/dri" ]; then
    find "${BUNDLE_DIR}/lib/dri" -name "*.so" 2>/dev/null | while read -r driver; do
        collect_deps "$driver" >> "$DEPS_FILE"
    done
fi

# De-duplicate
UNIQUE_DEPS=$(sort -u "$DEPS_FILE")
rm -f "$DEPS_FILE"

# Copy libraries with platform-appropriate exclusion
while read -r lib; do
    [ -z "$lib" ] && continue
    lib_name=$(basename "$lib")

    if [ "$IS_MUSL" = true ]; then
        # Musl build: bundle EVERYTHING except the dynamic linker itself
        # (handled separately below). This ensures the entire process uses
        # only musl-linked libraries, avoiding glibc/musl ABI conflicts.
        case "$lib_name" in
            ld-musl*)
                continue
                ;;
        esac
    else
        # Glibc build: skip standard system libraries to avoid startup crashes
        case "$lib_name" in
            ld-linux*|libc.*|libpthread.*|libdl.*|libm.*|librt.*|libgcc_s.*|libstdc++.*|libresolv.*|libutil.*|\
            libGL.*|libEGL.*|libGLdispatch.*|libGLX.*|libOpenGL.*|libdrm.*|libglapi.*|libgbm.*|\
            libxcb*|libX11*|libX11-xcb*|libwayland*|libasound*|libfontconfig*|libfreetype*|libdbus*|libuuid*|libudev*|libz.*|\
            libglib-*|libgobject-*|libgthread-*|libgmodule-*|libgio-*)
                continue
                ;;
        esac
    fi

    # Skip if already bundled
    [ -f "${BUNDLE_DIR}/lib/$lib_name" ] && continue

    cp "$lib" "${BUNDLE_DIR}/lib/"
    if [ ! -L "${BUNDLE_DIR}/lib/$lib_name" ]; then
        chmod +w "${BUNDLE_DIR}/lib/$lib_name"
        # Never strip musl libc — it's also the dynamic linker
        case "$lib_name" in
            libc.musl*)
                ;;
            *)
                strip --strip-unneeded "${BUNDLE_DIR}/lib/$lib_name" 2>/dev/null || true
                ;;
        esac
    fi
done <<< "$UNIQUE_DEPS"

# --- Musl-specific: bundle dynamic linker ---
if [ "$IS_MUSL" = true ]; then
    # Bundle the musl dynamic linker
    if [ -n "$INTERP" ] && [ -f "$INTERP" ]; then
        echo "Bundling musl dynamic linker: ${INTERP}"
        cp "$INTERP" "${BUNDLE_DIR}/lib/"
        # Do NOT strip or patchelf the linker
    fi
fi

# Strip Qt plugins
find "${BUNDLE_DIR}/lib/plugins" -type f -name "*.so" -exec strip --strip-unneeded {} + 2>/dev/null || true

# Strip Mesa DRI drivers (if musl)
if [ "$IS_MUSL" = true ] && [ -d "${BUNDLE_DIR}/lib/dri" ]; then
    find "${BUNDLE_DIR}/lib/dri" -type f -name "*.so" -exec strip --strip-unneeded {} + 2>/dev/null || true
fi

# --- Copy PROJ Data (selective) ---
echo "Copying PROJ data..."
find "${PROJ_ROOT}/share/proj/" -maxdepth 1 -type f -not -name "*.tif" -not -name "*.tiff" -not -name "*.gtiff" -exec cp {} "${BUNDLE_DIR}/share/proj/" \;

# --- Relink ---
echo "Adjusting rpaths..."
if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/../lib' "${BUNDLE_DIR}/bin/${BINARY_NAME}"
    for lib in "${BUNDLE_DIR}/lib/"*.so*; do
        if [ ! -L "$lib" ]; then
            lib_name=$(basename "$lib")
            # Never patchelf the musl dynamic linker or libc
            case "$lib_name" in
                ld-musl*|libc.musl*)
                    continue
                    ;;
            esac
            patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true
        fi
    done

    # Adjust rpaths for Mesa DRI drivers to look in '$ORIGIN/..' (which is the lib folder)
    if [ "$IS_MUSL" = true ] && [ -d "${BUNDLE_DIR}/lib/dri" ]; then
        for driver in "${BUNDLE_DIR}/lib/dri/"*.so*; do
            if [ -f "$driver" ] && [ ! -L "$driver" ]; then
                patchelf --set-rpath '$ORIGIN/..' "$driver" 2>/dev/null || true
            fi
        done
    fi
else
    echo "Warning: patchelf not found. Binary might not find bundled libs automatically."
fi

# --- Create Launcher ---
echo "Creating launcher script..."
if [ "$IS_MUSL" = true ]; then
    INTERP_NAME=$(basename "$INTERP")
    cat > "${BUNDLE_DIR}/run_geoviewer.sh" <<EOF
#!/bin/sh
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
export LD_LIBRARY_PATH="\$DIR/lib:\${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="\$DIR/lib/plugins"
export PROJ_LIB="\$DIR/share/proj"
export XDG_SESSION_TYPE=x11
# Use bundled Mesa DRI drivers to avoid loading glibc-linked host drivers
export LIBGL_DRIVERS_PATH="\$DIR/lib/dri"
# Point EGL loaders to the bundled vendor configuration files
export __EGL_VENDOR_LIBRARY_DIRS="\$DIR/share/glvnd/egl_vendor.d"
# Force software rendering for maximum compatibility on glibc hosts
export LIBGL_ALWAYS_SOFTWARE=1
# Force Qt to use EGL instead of GLX to avoid GLX/fbconfig mismatches under software rendering
export QT_XCB_GL_INTEGRATION=xcb_egl
exec "\$DIR/lib/${INTERP_NAME}" --library-path "\$DIR/lib" "\$DIR/bin/${BINARY_NAME}" "\$@"
EOF
else
    cat > "${BUNDLE_DIR}/run_geoviewer.sh" <<EOF
#!/bin/bash
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
export LD_LIBRARY_PATH="\$DIR/lib:\${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="\$DIR/lib/plugins"
export PROJ_LIB="\$DIR/share/proj"
export XDG_SESSION_TYPE=x11
exec "\$DIR/bin/${BINARY_NAME}" "\$@"
EOF
fi
chmod +x "${BUNDLE_DIR}/run_geoviewer.sh"

echo ""
echo "Done! Linux package is available at ${BUNDLE_DIR}"
echo "Run it with: ./${BINARY_NAME}_linux_x64/run_geoviewer.sh"
