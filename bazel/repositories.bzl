load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _non_module_deps_impl(ctx):
    http_archive(
        name = "pugixml",
        urls = ["https://github.com/zeux/pugixml/archive/refs/tags/v1.15.tar.gz"],
        strip_prefix = "pugixml-1.15",
        build_file = "//third_party/bazel:pugixml.BUILD",
        patch_args = ["-p1"],
        patches = ["//third_party/patches:pugixml.patch"],
    )
    
    http_archive(
        name = "opendrive",
        urls = ["https://github.com/pageldev/libOpenDRIVE/archive/refs/tags/0.5.0.tar.gz"],
        strip_prefix = "libOpenDRIVE-0.5.0",
        build_file = "//third_party/bazel:libOpenDRIVE.BUILD",
        patch_args = ["-p1"],
        patches = ["//third_party/patches:libOpenDRIVE.patch"],
    )

non_module_deps = module_extension(
    implementation = _non_module_deps_impl,
)
