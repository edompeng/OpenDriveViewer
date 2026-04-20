load("@rules_cc//cc:defs.bzl", "cc_library")
load("//bazel:qt.bzl", "QT_COPTS", "qt_moc_genrule")

qt_moc_genrule(
    name = "moc_files",
    headers = [
        "main_window.h",
    ],
)

cc_library(
    name = "main_window",
    srcs = [
        "main_window.cpp",
        ":moc_files",
        "//translations:OpenDriveViewer_translations",
    ],
    hdrs = [
        "main_window.h",
    ],
    copts = QT_COPTS,
    local_defines = select({
        "@platforms//os:windows": ["GEOVIEWER_BUILD_LIB"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
    deps = [
        "//src:export_header",
        "//bazel:qt6",
        "//src/core",
        "//src/logic",
        "//src/ui/render",
        "//src/ui/widgets",
    ],
)
