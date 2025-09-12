# --- Configuration ---
$BINARY_NAME = "OpenDriveViewer"
$TARGET = "//src/app:OpenDriveViewer"
$DIST_DIR = "dist\windows"
$BUNDLE_DIR = "$DIST_DIR\${BINARY_NAME}_win_x64"

# --- Check Environment ---
if (-not $env:QT6_ROOT) {
    Write-Error "Error: QT6_ROOT environment variable is not set."
    exit 1
}

if (-not $env:PROJ_ROOT) {
    Write-Error "Error: PROJ_ROOT environment variable is not set."
    exit 1
}

$WINDEPLOYQT = "${env:QT6_ROOT}\bin\windeployqt.exe"
if (-not (Test-Path $WINDEPLOYQT)) {
    Write-Error "Error: windeployqt.exe not found at $WINDEPLOYQT"
    exit 1
}

# --- Setup Bundle ---
Write-Host "Preparing Windows Bundle..."
Remove-Item -Recurse -Force "${BUNDLE_DIR}" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path "${BUNDLE_DIR}\bin"
New-Item -ItemType Directory -Path "${BUNDLE_DIR}\share\proj"

# Copy binary
Copy-Item "bazel-bin\src\app\${BINARY_NAME}.exe" "${BUNDLE_DIR}\bin\"

# --- Run windeployqt ---
Write-Host "Running windeployqt..."
# --no-compiler-runtime is removed to keep runtime libs as requested earlier, 
# but specifically adding flags to minimize package if needed.
& "$WINDEPLOYQT" --dir "${BUNDLE_DIR}\bin" --compiler-runtime "${BUNDLE_DIR}\bin\${BINARY_NAME}.exe"

# --- Cleanup PDB files (symbols) ---
Write-Host "Removing PDB files..."
Get-ChildItem -Path "${BUNDLE_DIR}" -Filter "*.pdb" -Recurse | Remove-Item -Force

# --- Handle PROJ and Dependencies ---
Write-Host "Bundling PROJ and related libraries (proj_9, sqlite3, tiff, libcurl, etc.)..."

$required_dlls = @("proj*.dll", "sqlite3.dll", "tiff*.dll", "libtiff*.dll", "libcurl*.dll", "zlib*.dll", "zstd*.dll", "liblzma*.dll")
$search_paths = @()

if ($env:PROJ_ROOT -and (Test-Path "$env:PROJ_ROOT\bin")) { $search_paths += "$env:PROJ_ROOT\bin" }
if ($env:PROJ_ROOT) { $search_paths += $env:PROJ_ROOT }
if ($env:VCPKG_ROOT -and (Test-Path "$env:VCPKG_ROOT\installed\x64-windows\bin")) { $search_paths += "$env:VCPKG_ROOT\installed\x64-windows\bin" }
if (Test-Path "bazel-bin\src\app") { $search_paths += "$PWD\bazel-bin\src\app" }

# Add common system PATH locations prioritizing those that look like dev environments
foreach ($p in ($env:PATH -split ';')) {
    if (Test-Path $p) { $search_paths += $p }
}
$search_paths = $search_paths | Select-Object -Unique

foreach ($dll in $required_dlls) {
    $found = $false
    foreach ($path in $search_paths) {
        $candidates = Get-ChildItem -Path $path -Filter $dll -File -ErrorAction SilentlyContinue
        if ($candidates) {
            foreach ($c in $candidates) {
                $targetFile = "${BUNDLE_DIR}\bin\$($c.Name)"
                if (-not (Test-Path $targetFile)) {
                    Copy-Item $c.FullName $targetFile -Force -ErrorAction SilentlyContinue
                    Write-Host "  -> Bundled $($c.Name) from $path"
                }
            }
            $found = $true
            # Don't break immediately, just to be safe if multiple versions are scattered, 
            # but we won't overwrite due to the (-not Test-Path) check.
        }
    }
    if (-not $found) {
        Write-Warning "Could not find $dll in PROJ_ROOT, VCPKG_ROOT, or PATH!"
    }
}

# --- Handle MSVC Runtime ---
Write-Host "Verifying and Bundling MSVC Runtime..."
# windeployqt --compiler-runtime might miss some depending on env. Ensure standard ones are copied.
$msvc_libs = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "vcomp140.dll")
foreach ($lib in $msvc_libs) {
    $target_lib = "${BUNDLE_DIR}\bin\$lib"
    if (-not (Test-Path $target_lib)) {
        # Fallback to copy from Windows System32
        $sys_lib = "C:\Windows\System32\$lib"
        if (Test-Path $sys_lib) {
            Copy-Item $sys_lib "${BUNDLE_DIR}\bin\" -Force -ErrorAction SilentlyContinue
        }
    }
}

# Copy PROJ data dynamically by finding where proj.db is
$PROJ_DB = Get-ChildItem -Path "${env:PROJ_ROOT}" -Filter "proj.db" -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
if ($PROJ_DB) {
    $PROJ_DATA_DIR = $PROJ_DB.DirectoryName
    Get-ChildItem -Path "$PROJ_DATA_DIR" -File | Where-Object { $_.Extension -notmatch "\.tif|\.tiff|\.gtiff" } | Copy-Item -Destination "${BUNDLE_DIR}\share\proj\" -Force
} else {
    Write-Warning "proj.db not found in ${env:PROJ_ROOT}! The application might fail to initialize spatial references."
}

# --- Create Launcher ---
Write-Host "Creating launcher batch script..."
@'
@echo off
set "DIR=%~dp0"
set "PROJ_LIB=%DIR%share\proj"
start "" "%DIR%bin\OpenDriveViewer.exe" %*
'@ | Out-File -FilePath "${BUNDLE_DIR}\run_geoviewer.bat" -Encoding ascii

Write-Host "Done! Windows package is available at ${BUNDLE_DIR}"
Write-Host "Run it with: ${BUNDLE_DIR}\run_geoviewer.bat"
