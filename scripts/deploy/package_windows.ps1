# --- Configuration ---
$ErrorActionPreference = 'Stop'

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

Write-Host "=== Environment ==="
Write-Host "  QT6_ROOT  = $env:QT6_ROOT"
Write-Host "  PROJ_ROOT = $env:PROJ_ROOT"

$WINDEPLOYQT = "${env:QT6_ROOT}\bin\windeployqt.exe"
if (-not (Test-Path $WINDEPLOYQT)) {
    Write-Error "Error: windeployqt.exe not found at $WINDEPLOYQT"
    exit 1
}

# --- Verify Bazel-built binary exists ---
$BAZEL_BINARY = "bazel-bin\src\app\${BINARY_NAME}.exe"
if (-not (Test-Path $BAZEL_BINARY)) {
    Write-Error "Error: Bazel binary not found at $BAZEL_BINARY. Did 'bazel build //src/app:OpenDriveViewer' run?"
    exit 1
}
Write-Host "  Binary    = $BAZEL_BINARY ($('{0:N2} MB' -f ((Get-Item $BAZEL_BINARY).Length / 1MB)))"

# --- Setup Bundle ---
Write-Host "`n=== Preparing Windows Bundle ==="
Remove-Item -Recurse -Force "${BUNDLE_DIR}" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path "${BUNDLE_DIR}\bin" | Out-Null
New-Item -ItemType Directory -Path "${BUNDLE_DIR}\share\proj" | Out-Null

# Copy binary
Copy-Item $BAZEL_BINARY "${BUNDLE_DIR}\bin\"
Write-Host "  -> Copied $BINARY_NAME.exe"

# Package PDB
Write-Host "`n=== Extracting Debug Symbols ==="
$PDB_FILE = "bazel-bin\src\app\${BINARY_NAME}.pdb"
if (Test-Path $PDB_FILE) {
    Compress-Archive -Path $PDB_FILE -DestinationPath "${BINARY_NAME}_windows_symbols.zip" -Force
    Write-Host "  -> Packaged debug symbols"
} else {
    Write-Warning "  -> PDB file not found at $PDB_FILE"
}

# --- Run windeployqt ---
Write-Host "`n=== Running windeployqt ==="
& "$WINDEPLOYQT" --dir "${BUNDLE_DIR}\bin" --compiler-runtime "${BUNDLE_DIR}\bin\${BINARY_NAME}.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "windeployqt failed with exit code $LASTEXITCODE"
    exit 1
}

# --- Cleanup PDB files (symbols) ---
Write-Host "Removing PDB files..."
Get-ChildItem -Path "${BUNDLE_DIR}" -Filter "*.pdb" -Recurse | Remove-Item -Force

# --- Handle PROJ and Dependencies ---
# Since proj and its dependencies (sqlite3, tiff, curl, etc.) are built via vcpkg,
# they are all conveniently placed in the vcpkg bin directory.
Write-Host "`n=== Bundling Dependencies from vcpkg ==="
$VCPKG_BIN_DIR = Join-Path $env:PROJ_ROOT "bin"

# Diagnostic: show what's in PROJ_ROOT
Write-Host "  Checking PROJ_ROOT directory structure:"
if (Test-Path $env:PROJ_ROOT) {
    Get-ChildItem -Path $env:PROJ_ROOT -Directory | ForEach-Object { Write-Host "    [DIR] $($_.Name)" }
} else {
    Write-Warning "  PROJ_ROOT directory does not exist: $env:PROJ_ROOT"
    # Attempt to find vcpkg_installed anywhere in the workspace
    Write-Host "  Searching workspace for vcpkg_installed..."
    $vcpkgInstalled = Get-ChildItem -Path (Get-Location) -Filter "vcpkg_installed" -Directory -Recurse -Depth 3 -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($vcpkgInstalled) {
        $triplet = "x64-windows"
        $altRoot = Join-Path $vcpkgInstalled.FullName $triplet
        if (Test-Path $altRoot) {
            Write-Host "  Found alternative PROJ_ROOT: $altRoot"
            $env:PROJ_ROOT = $altRoot
            $VCPKG_BIN_DIR = Join-Path $env:PROJ_ROOT "bin"
        }
    }
}

if (Test-Path $VCPKG_BIN_DIR) {
    $dlls = Get-ChildItem -Path $VCPKG_BIN_DIR -Filter "*.dll"
    Write-Host "  Found $($dlls.Count) DLL(s) in $VCPKG_BIN_DIR :"
    $dlls | ForEach-Object { Write-Host "    $($_.Name) ($('{0:N2} MB' -f ($_.Length / 1MB)))" }
    $dlls | Copy-Item -Destination "${BUNDLE_DIR}\bin" -Force
} else {
    Write-Error "Error: vcpkg bin directory not found at $VCPKG_BIN_DIR. Check your PROJ_ROOT setting."
    exit 1
}

# Verify PROJ DLL was actually bundled
$projDll = Get-ChildItem -Path "${BUNDLE_DIR}\bin" -Filter "proj*.dll" -ErrorAction SilentlyContinue
if (-not $projDll) {
    Write-Error "Error: No proj*.dll found in the bundle after copying. The packaged application will not work."
    exit 1
}
Write-Host "  -> PROJ DLL verified in bundle: $($projDll.Name)"

# --- Handle MSVC Runtime ---
Write-Host "`n=== Verifying MSVC Runtime ==="
# windeployqt --compiler-runtime might miss some depending on env. Ensure standard ones are copied.
$msvc_libs = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "vcomp140.dll")
foreach ($lib in $msvc_libs) {
    $target_lib = "${BUNDLE_DIR}\bin\$lib"
    if (-not (Test-Path $target_lib)) {
        # Fallback to copy from Windows System32
        $sys_lib = "C:\Windows\System32\$lib"
        if (Test-Path $sys_lib) {
            Copy-Item $sys_lib "${BUNDLE_DIR}\bin\" -Force -ErrorAction SilentlyContinue
            Write-Host "  -> Copied $lib from System32"
        }
    }
}

# --- Handle PROJ Data (proj.db) ---
# For PROJ to work, it needs its data files, especially proj.db.
# In vcpkg, these are typically located in share\proj.
Write-Host "`n=== Bundling PROJ Data Files ==="
$VCPKG_PROJ_DATA = Join-Path $env:PROJ_ROOT "share\proj"

if (Test-Path $VCPKG_PROJ_DATA) {
    # Copy all data files except heavy geotiff grids to keep package small
    $dataFiles = Get-ChildItem -Path "$VCPKG_PROJ_DATA" -File | Where-Object { $_.Extension -notmatch "\.tif|\.tiff|\.gtiff" }
    $dataFiles | Copy-Item -Destination "${BUNDLE_DIR}\share\proj\" -Force
    Write-Host "  -> Bundled $($dataFiles.Count) PROJ data file(s) from $VCPKG_PROJ_DATA"
} else {
    # Fallback search if path is different (though standard in vcpkg)
    Write-Warning "Standard PROJ data path not found ($VCPKG_PROJ_DATA). Searching for proj.db..."
    $searchPaths = @($env:PROJ_ROOT)
    if ($env:GITHUB_WORKSPACE) { $searchPaths += $env:GITHUB_WORKSPACE }
    $PROJ_DB = $null
    foreach ($searchPath in $searchPaths) {
        $PROJ_DB = Get-ChildItem -Path $searchPath -Filter "proj.db" -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($PROJ_DB) { break }
    }
    if ($PROJ_DB) {
        $PROJ_DATA_DIR = $PROJ_DB.DirectoryName
        $dataFiles = Get-ChildItem -Path "$PROJ_DATA_DIR" -File | Where-Object { $_.Extension -notmatch "\.tif|\.tiff|\.gtiff" }
        $dataFiles | Copy-Item -Destination "${BUNDLE_DIR}\share\proj\" -Force
        Write-Host "  -> Bundled PROJ data (found via search) from $PROJ_DATA_DIR"
    } else {
        Write-Warning "proj.db not found! The application might fail to initialize spatial references."
    }
}

# --- Create Launcher ---
Write-Host "`n=== Creating Launcher ==="
@'
@echo off
set "DIR=%~dp0"
set "PROJ_LIB=%DIR%share\proj"
start "" "%DIR%bin\OpenDriveViewer.exe" %*
'@ | Out-File -FilePath "${BUNDLE_DIR}\run_geoviewer.bat" -Encoding ascii

# --- Final Summary ---
Write-Host "`n=== Bundle Summary ==="
$totalFiles = (Get-ChildItem -Path "${BUNDLE_DIR}" -Recurse -File).Count
$totalSizeMB = '{0:N2}' -f ((Get-ChildItem -Path "${BUNDLE_DIR}" -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB)
Write-Host "  Location : ${BUNDLE_DIR}"
Write-Host "  Files    : $totalFiles"
Write-Host "  Size     : $totalSizeMB MB"
Write-Host "  Run with : ${BUNDLE_DIR}\run_geoviewer.bat"
Write-Host "`nDone!"
