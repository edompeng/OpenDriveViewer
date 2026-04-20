load("@rules_cc//cc:defs.bzl", "cc_library")
load("//bazel:qt.bzl", "QT_COPTS", "qt_moc_genrule")

qt_moc_genrule(
    name = "moc_files",
    headers = [
        "async_map_loader.h",
        "coordinate_points_widget.h",
        "favorites_widget.h",
        "floating_panel_widget.h",
        "geo_viewer.h",
        "layer_control_widget.h",
        "loading_progress_widget.h",
        "routing_widget.h",
    ],
)

cc_library(
    name = "widgets",
    srcs = [
        "async_map_loader.cpp",
        "coordinate_points_widget.cpp",
        "favorites_widget.cpp",
        "floating_panel_widget.cpp",
        "geo_viewer.cpp",
        "geo_viewer_scene.cpp",
        "layer_control_widget.cpp",
        "layer_tree_model.cpp",
        "loading_progress_widget.cpp",
        "routing_widget.cpp",
        ":moc_files",
    ],
    hdrs = [
        "async_map_loader.h",
        "coordinate_points_widget.h",
        "favorites_widget.h",
        "floating_panel_widget.h",
        "geo_viewer.h",
        "layer_control_widget.h",
        "layer_tree_model.h",
        "loading_progress_widget.h",
        "routing_widget.h",
        "widgets_export.h",
    ],
    copts = QT_COPTS,
    visibility = ["//visibility:public"],
    deps = [
        "//bazel:qt6",
        "//src/core",
        "//src/logic",
        "//src/ui/render",
    ],
)
