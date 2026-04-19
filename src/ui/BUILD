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
    ],
    hdrs = [
        "main_window.h",
    ],
    copts = QT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "//bazel:qt6",
        "//src/core",
        "//src/logic",
        "//src/renderer",
        "//src/ui/widgets",
        "//src/app:app",
    ],
)
