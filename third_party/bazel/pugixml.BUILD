load("@rules_cc//cc:defs.bzl", "cc_library")
cc_library(
    name = "pugixml",
    srcs = ["src/pugixml.cpp"],
    hdrs = [
        "src/pugiconfig.hpp",
        "src/pugixml.hpp",
    ],
    copts = ["-w"],
    include_prefix = "pugixml",
    strip_include_prefix = "src",
    visibility = ["//visibility:public"],
)
