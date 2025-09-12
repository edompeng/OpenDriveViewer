"""Repository rule that configures PROJ from the PROJ_ROOT environment variable.

Creates an external repository (@proj_config) with:
  - A symlink to the PROJ include/ directory.
  - A cc_library target (:proj) with correct include paths and linkopts.
  - A defs.bzl exporting PROJ_ROOT and PROJ_COPTS.

Usage in MODULE.bazel:
    proj_config = use_repo_rule("//bazel:proj_config.bzl", "proj_config")
    proj_config(name = "proj_config")

The only user-facing parameter is PROJ_ROOT (set in .bazelrc.user).
"""

_BUILD_TEMPLATE = """\
package(default_visibility = ["//visibility:public"])
load("@rules_cc//cc:defs.bzl", "cc_library")

exports_files(["defs.bzl"])


# The 'proj' library uses glob with allow_empty=True to handle cases where 
# symlinks might be restricted or unresolved in some environments (like Windows CI),
# while still allowing the compiler to find headers via absolute include paths.
cc_library(
    name = "proj",
    hdrs = glob(["include/**"], allow_empty = True),
    includes = ["include"],
    srcs = select({{
        "@platforms//os:windows": glob(["lib/proj*.lib"], allow_empty = True),
        "//conditions:default": [],
    }}),
    data = select({{
        "@platforms//os:windows": glob(["bin/proj*.dll"], allow_empty = True),
        "//conditions:default": [],
    }}),
    linkopts = select({{
        "@platforms//os:windows": [],
        "//conditions:default": ["-L{proj_root}/lib", "-lproj"],
    }}),
)
"""

_DEFS_TEMPLATE = """\
\"\"\"Auto-generated PROJ configuration. Do not edit.\"\"\"

PROJ_ROOT = "{proj_root}"

# Compiler flags for targets that include PROJ headers.
PROJ_COPTS = {proj_copts}
"""

def _proj_config_impl(repository_ctx):
    proj_root = repository_ctx.os.environ.get("PROJ_ROOT", "").replace("\\", "/")
    if not proj_root:
        fail(
            "PROJ_ROOT environment variable is not set.\n" +
            "Add to .bazelrc.user:\n" +
            "  build --action_env=PROJ_ROOT=/path/to/proj",
        )

    # On Windows, symlink/junction behavior can be inconsistent in CI.
    # We use a robust search via PowerShell to find where proj.h actually lives,
    # then copy that directory into our repository.
    if "windows" in repository_ctx.os.name.lower():
        # Ensure destination is clean
        repository_ctx.execute(["cmd.exe", "/c", "if exist include (rd /s /q include)"])
        repository_ctx.execute(["cmd.exe", "/c", "if exist lib (rd /s /q lib)"])
        repository_ctx.execute(["cmd.exe", "/c", "if exist bin (rd /s /q bin)"])
        
        # Search for proj.h in the current workspace
        # GITHUB_WORKSPACE is the most reliable start point on CI runners.
        search_root = repository_ctx.os.environ.get("GITHUB_WORKSPACE", str(repository_ctx.path("../..")))
        find_cmd = 'powershell.exe -Command "(Get-ChildItem -Path \'{}\' -Filter proj.h -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1).DirectoryName"'.format(search_root)
        
        res = repository_ctx.execute(["cmd.exe", "/c", find_cmd])
        discovered_path = res.stdout.strip()
        
        if not discovered_path:
            # Fallback to the environment variable if search fails
            discovered_path = proj_root.replace("/", "\\") + "\\include"
            
        discovered_lib = discovered_path.replace("\\include", "\\lib")
        discovered_bin = discovered_path.replace("\\include", "\\bin")
            
        # Copy the headers
        repository_ctx.execute([
            "cmd.exe", "/c", 'robocopy "{}" "include" /E /NFL /NDL /NJH /NJS /NC /NS /NP || exit 0'.format(discovered_path)
        ])
        
        # Copy the library files
        repository_ctx.execute([
            "cmd.exe", "/c", 'robocopy "{}" "lib" proj*.lib /NFL /NDL /NJH /NJS /NC /NS /NP || exit 0'.format(discovered_lib)
        ])
        
        # Copy the DLLs
        repository_ctx.execute([
            "cmd.exe", "/c", 'robocopy "{}" "bin" proj*.dll /NFL /NDL /NJH /NJS /NC /NS /NP || exit 0'.format(discovered_bin)
        ])
        
        # Verification
        check = repository_ctx.execute(["cmd.exe", "/c", "if not exist include\\proj.h (exit 1)"])
        if check.return_code != 0:
            fail("Failed to resolve PROJ headers on Windows. Searched in {} and tried {}. Please verify installation.".format(search_root, discovered_path))
    else:
        repository_ctx.symlink(proj_root + "/include", "include")

    # Derive compile flags.
    repo_dir = "external/" + repository_ctx.name
    proj_copts = ["-I{}/include".format(repo_dir)]

    # Generate defs.bzl
    repository_ctx.file("defs.bzl", _DEFS_TEMPLATE.format(
        proj_root = proj_root,
        proj_copts = repr(proj_copts),
    ))

    # Generate BUILD.bazel
    repository_ctx.file("BUILD.bazel", _BUILD_TEMPLATE.format(
        proj_root = proj_root,
    ))

proj_config = repository_rule(
    implementation = _proj_config_impl,
    environ = ["PROJ_ROOT"],
    local = True,
    doc = "Reads PROJ_ROOT and creates a repository with PROJ headers and link flags.",
)
