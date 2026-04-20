load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "opendrive",
    srcs = [
        "src/Geometries/Arc.cpp",
        "src/Geometries/CubicSpline.cpp",
        "src/Geometries/Line.cpp",
        "src/Geometries/ParamPoly3.cpp",
        "src/Geometries/RoadGeometry.cpp",
        "src/Geometries/Spiral.cpp",
        "src/Geometries/Spiral/odrSpiral.cpp",
        "src/Junction.cpp",
        "src/Lane.cpp",
        "src/LaneSection.cpp",
        "src/Mesh.cpp",
        "src/OpenDriveMap.cpp",
        "src/RefLine.cpp",
        "src/Road.cpp",
        "src/RoadMark.cpp",
        "src/RoadNetworkMesh.cpp",
        "src/RoadObject.cpp",
        "src/RoadSignal.cpp",
        "src/RoutingGraph.cpp",
    ],
    hdrs = glob(
        [
            "include/**/*.h",
            "include/**/*.hpp",
            "thirdparty/**/*.h",
            "thirdparty/**/*.hpp",
        ],
        allow_empty = True,
    ),
    copts = ["-I."],
    defines = ["_USE_MATH_DEFINES"],
    includes = [
        "include",
        "thirdparty",
    ],
    visibility = ["//visibility:public"],
    deps = ["@pugixml"],
)
