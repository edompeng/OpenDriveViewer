load("//bazel:qt.bzl", "QT_COPTS")

def geoviewer_cc_library(name, **kwargs):
    """A wrapper for native.cc_library that adds GeoViewer-specific flags."""
    
    # Merge copts
    kwargs["copts"] = kwargs.get("copts", []) + QT_COPTS
    
    # Merge local_defines with platform selection
    kwargs["local_defines"] = kwargs.get("local_defines", []) + select({
        "@platforms//os:windows": ["GEOVIEWER_BUILD_LIB"],
        "//conditions:default": [],
    })
    
    # Merge dependencies
    kwargs["deps"] = kwargs.get("deps", []) + ["//src:export_header"]
    
    native.cc_library(
        name = name,
        **kwargs
    )
