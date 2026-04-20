load("//bazel:geoviewer.bzl", "geoviewer_cc_library")
load("//bazel:qt.bzl", "qt_moc_genrule")

qt_moc_genrule(
    name = "moc_files",
    headers = [
        "main_window.h",
    ],
)

geoviewer_cc_library(
    name = "main_window",
    srcs = [
        "main_window.cpp",
        ":moc_files",
        "//translations:OpenDriveViewer_translations",
    ],
    hdrs = [
        "main_window.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//bazel:qt6",
        "//src/core",
        "//src/logic",
        "//src/ui/render",
        "//src/ui/widgets",
    ],
)
