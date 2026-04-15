"""Repository rule that configures Qt6 from the QT6_ROOT environment variable.

Creates an external repository (@qt6_config) with:
  - Symlinks to the Qt SDK lib/ and include/ directories.
  - A cc_library target (:qt6) with correct linkopts and header declarations.
  - A defs.bzl exporting QT6_ROOT, QT_MODULES, and QT_COPTS.

Usage in MODULE.bazel:
    qt6_config = use_repo_rule("//bazel:qt_config.bzl", "qt6_config")
    qt6_config(
        name = "qt6_config",
        modules = ["QtCore", "QtGui", "QtWidgets"],
    )

The only user-facing parameter is QT6_ROOT (set in .bazelrc.user).
"""

# --- macOS: framework-based layout ---
_MACOS_BUILD = """\
package(default_visibility = ["//visibility:public"])
load("@rules_cc//cc:defs.bzl", "cc_library")

exports_files(["defs.bzl"])

cc_library(
    name = "qt6",
    hdrs = glob({hdrs_globs}, allow_empty = True),
    linkopts = {macos_linkopts},
)
"""

# --- Linux: system-package layout ---
_LINUX_BUILD = """\
package(default_visibility = ["//visibility:public"])
load("@rules_cc//cc:defs.bzl", "cc_library")

exports_files(["defs.bzl"])

cc_library(
    name = "qt6",
    hdrs = glob(["include/**"], allow_empty = True),
    linkopts = {linux_linkopts},
)
"""

# --- Windows: SDK layout ---
_WINDOWS_BUILD = """\
package(default_visibility = ["//visibility:public"])
load("@rules_cc//cc:defs.bzl", "cc_library")

exports_files(["defs.bzl"])

cc_library(
    name = "qt6",
    hdrs = glob(["include/**"], allow_empty = True),
    linkopts = {windows_linkopts},
)
"""

_DEFS_TEMPLATE = """\
\"\"\"Auto-generated Qt6 configuration. Do not edit.\"\"\"

QT6_ROOT = "{qt6_root}"

QT_MODULES = {modules}

# Compiler flags for targets that include Qt headers.
QT_COPTS = {qt_copts}
"""

def _qt6_config_impl(repository_ctx):
    qt6_root = repository_ctx.os.environ.get("QT6_ROOT", "").replace("\\", "/")
    if not qt6_root:
        fail(
            "QT6_ROOT environment variable is not set.\n" +
            "Add to .bazelrc.user:\n" +
            "  build --action_env=QT6_ROOT=/path/to/Qt/6.x.x/macos",
        )

    modules = repository_ctx.attr.modules
    os_name = repository_ctx.os.name.lower()
    repo_dir = "external/" + repository_ctx.name

    if "mac" in os_name:
        _configure_macos(repository_ctx, qt6_root, modules, repo_dir)
    elif "windows" in os_name:
        _configure_windows(repository_ctx, qt6_root, modules, repo_dir)
    else:
        _configure_linux(repository_ctx, qt6_root, modules, repo_dir)

def _configure_macos(repository_ctx, qt6_root, modules, repo_dir):
    """Configure Qt6 for macOS (framework-based layout)."""
    repository_ctx.symlink(qt6_root + "/lib", "lib")
    repository_ctx.symlink(qt6_root + "/include", "include")

    # Compile flags: -F for framework search, -I for each module's Headers
    qt_copts = [
        "-F{}/lib".format(repo_dir),
        "-I{}/include".format(repo_dir),
    ] + [
        "-I{}/lib/{}.framework/Headers".format(repo_dir, m)
        for m in modules
    ]

    # Qt6's qyieldcpu.h may call __yield() on Apple Silicon without pulling in
    # the declaration. Force-include arm_acle.h for macOS arm64 to keep builds
    # stable under strict warning settings (-Werror).
    uname_result = repository_ctx.execute(["uname", "-m"])
    arch = uname_result.stdout.strip().lower() if uname_result.return_code == 0 else ""
    if arch in ["arm64", "aarch64"]:
        qt_copts += ["-include", "arm_acle.h"]

    # Link flags
    macos_linkopts = [
        "-F{}/lib".format(qt6_root),
        "-Wl,-rpath,{}/lib".format(qt6_root),
    ]
    for m in modules:
        macos_linkopts += ["-framework", m]
    macos_linkopts += ["-framework", "OpenGL"]

    # Header globs
    hdrs_globs = ["lib/{}.framework/Headers/**".format(m) for m in modules]

    repository_ctx.file("defs.bzl", _DEFS_TEMPLATE.format(
        qt6_root = qt6_root,
        modules = repr(sorted(modules)),
        qt_copts = repr(qt_copts),
    ))
    repository_ctx.file("BUILD.bazel", _MACOS_BUILD.format(
        hdrs_globs = repr(hdrs_globs),
        macos_linkopts = repr(macos_linkopts),
    ))

def _configure_linux(repository_ctx, qt6_root, modules, repo_dir):
    """Configure Qt6 for Linux (system-package or custom install)."""
    repository_ctx.symlink(qt6_root + "/include", "include")

    # module name without "Qt" prefix for pkg-config style dirs
    # e.g. /usr/include/qt6/QtCore -> QtCore
    qt_copts = ["-I{}/include".format(repo_dir)] + [
        "-I{}/include/{}".format(repo_dir, m)
        for m in modules
    ]

    linux_linkopts = [
        "-L{}/lib".format(qt6_root),
        "-Wl,-rpath,{}/lib".format(qt6_root),
    ] + ["-l" + m.replace("Qt", "Qt6") for m in modules] + ["-lGL"]

    repository_ctx.file("defs.bzl", _DEFS_TEMPLATE.format(
        qt6_root = qt6_root,
        modules = repr(sorted(modules)),
        qt_copts = repr(qt_copts),
    ))
    repository_ctx.file("BUILD.bazel", _LINUX_BUILD.format(
        linux_linkopts = repr(linux_linkopts),
    ))

def _configure_windows(repository_ctx, qt6_root, modules, repo_dir):
    """Configure Qt6 for Windows (MSVC SDK layout)."""
    # Ensure destination is clean
    repository_ctx.execute(["cmd.exe", "/c", "if exist include (rd /s /q include)"])

    # Search for a representative Qt header to find the include directory
    search_root = repository_ctx.os.environ.get("GITHUB_WORKSPACE", str(repository_ctx.path("../..")))
    find_cmd = 'powershell.exe -Command "(Get-ChildItem -Path \'{}\' -Filter qobject.h -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1).DirectoryName"'.format(search_root)
    
    res = repository_ctx.execute(["cmd.exe", "/c", find_cmd])
    discovered_path = res.stdout.strip()
    
    # If discovered_path contains 'QtCore', we want its parent (the main include dir)
    if "QtCore" in discovered_path or "qtcore" in discovered_path.lower():
        # Strip everything from \QtCore onwards
        parts = discovered_path.replace("/", "\\").split("\\")
        if "QtCore" in parts:
            discovered_path = "\\".join(parts[:parts.index("QtCore")])
        elif "qtcore" in [p.lower() for p in parts]:
            # find index of qtcore case-insensitively
            idx = [p.lower() for p in parts].index("qtcore")
            discovered_path = "\\".join(parts[:idx])

    if not discovered_path:
        discovered_path = qt6_root.replace("/", "\\") + "\\include"

    # Copy the headers
    repository_ctx.execute([
        "cmd.exe", "/c", 'robocopy "{}" "include" /E /NFL /NDL /NJH /NJS /NC /NS /NP || exit 0'.format(discovered_path)
    ])

    qt_copts = ["-I{}/include".format(repo_dir)] + [
        "-I{}/include/{}".format(repo_dir, m)
        for m in modules
    ]

    windows_linkopts = [
        "/LIBPATH:{}/lib".format(qt6_root),
    ] + [m.replace("Qt", "Qt6") + ".lib" for m in modules] + ["opengl32.lib"]

    repository_ctx.file("defs.bzl", _DEFS_TEMPLATE.format(
        qt6_root = qt6_root,
        modules = repr(sorted(modules)),
        qt_copts = repr(qt_copts),
    ))
    repository_ctx.file("BUILD.bazel", _WINDOWS_BUILD.format(
        windows_linkopts = repr(windows_linkopts),
    ))

qt6_config = repository_rule(
    implementation = _qt6_config_impl,
    attrs = {
        "modules": attr.string_list(
            doc = "Qt modules to include and link (e.g. QtCore, QtWidgets).",
        ),
    },
    environ = ["QT6_ROOT"],
    local = True,
    doc = "Reads QT6_ROOT and creates a repository with Qt6 headers, includes, and link flags.",
)
