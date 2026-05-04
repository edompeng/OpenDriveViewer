"""Repository rule that dynamically detects tcmalloc on the host machine."""

def _tcmalloc_config_impl(repository_ctx):
    has_tcmalloc = False
    is_windows = "windows" in repository_ctx.os.name.lower()
    is_mac = "mac" in repository_ctx.os.name.lower()

    # 1. Detect tcmalloc based on OS
    if is_windows:
        proj_root = repository_ctx.os.environ.get("PROJ_ROOT", "").replace("\\", "/")
        if proj_root:
            res = repository_ctx.execute(["cmd.exe", "/c", 'dir "{}/lib/*tcmalloc*.lib"'.format(proj_root)])
            if res.return_code == 0:
                has_tcmalloc = True
    elif is_mac:
        res = repository_ctx.execute(["brew", "--prefix", "gperftools"])
        if res.return_code == 0:
            has_tcmalloc = True
    else:
        # Check ldconfig cache for libtcmalloc
        res = repository_ctx.execute(["ldconfig", "-p"])
        if "libtcmalloc" in res.stdout:
            has_tcmalloc = True
        else:
            # Fallback path checks
            if repository_ctx.execute(["test", "-f", "/usr/lib/x86_64-linux-gnu/libtcmalloc.so"]).return_code == 0:
                has_tcmalloc = True

    # 2. Generate BUILD.bazel with conditional linkage
    build_content = """\
package(default_visibility = ["//visibility:public"])
load("@rules_cc//cc:defs.bzl", "cc_library")

"""
    if has_tcmalloc:
        if is_windows:
            # On Windows, we still let the CI pass the absolute path via --linkopt in ci.yml,
            # or if it's local development, the user should provide the path or rely on vcpkg integration.
            # We don't hardcode it here because MSVC linker doesn't support globbing in linkopts easily.
            build_content += """\
cc_library(
    name = "tcmalloc",
    linkopts = [],
)
"""
        elif is_mac:
            # Check standard Homebrew prefix
            prefix = repository_ctx.execute(["brew", "--prefix", "gperftools"]).stdout.strip()
            if prefix:
                build_content += """\
cc_library(
    name = "tcmalloc",
    linkopts = ["-L{}/lib", "-ltcmalloc"],
)
""".format(prefix)
            else:
                build_content += """\
cc_library(
    name = "tcmalloc",
    linkopts = ["-ltcmalloc"],
)
"""
        else:
            build_content += """\
cc_library(
    name = "tcmalloc",
    linkopts = ["-ltcmalloc"],
)
"""
    else:
        build_content += """\
cc_library(
    name = "tcmalloc",
    linkopts = [],
)
"""

    repository_ctx.file("BUILD.bazel", build_content)

tcmalloc_config = repository_rule(
    implementation = _tcmalloc_config_impl,
    environ = ["PROJ_ROOT"],
    local = True,
    doc = "Dynamically detects if tcmalloc is available on the host machine.",
)
