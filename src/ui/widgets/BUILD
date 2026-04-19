load("@rules_cc//cc:defs.bzl", "cc_library")
load("//bazel:qt.bzl", "QT_COPTS", "qt_moc_genrule")

qt_moc_genrule(
    name = "moc_files",
    headers = [
        "geo_viewer.h",
        "geo_viewer_widget.h",
        "coordinate_points_widget.h",
        "favorites_widget.h",
        "floating_panel_widget.h",
        "layer_control_widget.h",
        "loading_progress_widget.h",
        "open_scenario_widget.h",
        "routing_widget.h",
    ],
)

cc_library(
    name = "widgets",
    srcs = [
        "coordinate_points_widget.cpp",
        "favorites_widget.cpp",
        "floating_panel_widget.cpp",
        "geo_viewer.cpp",
        "geo_viewer_scene.cpp",
        "geo_viewer_widget.cpp",
        "layer_control_widget.cpp",
        "loading_progress_widget.cpp",
        "open_scenario_widget.cpp",
        "routing_widget.cpp",
        ":moc_files",
    ],
    hdrs = [
        "coordinate_points_widget.h",
        "favorites_widget.h",
        "floating_panel_widget.h",
        "geo_viewer.h",
        "geo_viewer_widget.h",
        "layer_control_widget.h",
        "loading_progress_widget.h",
        "open_scenario_widget.h",
        "routing_widget.h",
    ],
    copts = QT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "//bazel:qt6",
        "//src/app:app_core",
        "//src/core",
        "//src/logic",
        "//src/ui/renderer",
    ],
)
