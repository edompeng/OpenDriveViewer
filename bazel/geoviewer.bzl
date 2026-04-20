load("//bazel:qt.bzl", "QT_COPTS")

def geoviewer_cc_library(name, **kwargs):
    """A wrapper for cc_library that adds GeoViewer-specific flags.
    
    This ensures that internal libraries are correctly marked with GEOVIEWER_BUILD_LIB
    on Windows and depend on the unified export header.
    """
    copts = kwargs.pop("copts", []) + QT_COPTS
    local_defines = kwargs.pop("local_defines", []) + select({
        "@platforms//os:windows": ["GEOVIEWER_BUILD_LIB"],
        "//conditions:default": [],
    })
    
    native.cc_library(
        name = name,
        copts = copts,
        local_defines = local_defines,
        deps = kwargs.pop("deps", []) + ["//src:export_header"],
        **kwargs
    )
