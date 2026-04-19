"""Qt6 and PROJ Bazel build helpers.

All external SDK configuration flows from environment variables:
  - QT6_ROOT  → @qt6_config  → QT_COPTS
  - PROJ_ROOT → @proj_config → PROJ_COPTS

Both are set once in .bazelrc.user; BUILD files import derived constants
from this file.
"""

load("@proj_config//:defs.bzl", _PROJ_COPTS = "PROJ_COPTS", _PROJ_ROOT = "PROJ_ROOT")
load("@qt6_config//:defs.bzl", _QT6_ROOT = "QT6_ROOT", _QT_COPTS = "QT_COPTS", _QT_MODULES = "QT_MODULES")

# Re-export for use in BUILD files.
QT6_ROOT = _QT6_ROOT
QT_MODULES = _QT_MODULES
QT_COPTS = _QT_COPTS
PROJ_ROOT = _PROJ_ROOT
PROJ_COPTS = _PROJ_COPTS

def qt_moc_genrule(name, headers, moc_tool = "//bazel:moc_wrapper"):
    """Generates moc_*.cpp files from Qt headers containing Q_OBJECT.

    Args:
        name: Target name for the generated file group.
        headers: Header file names (relative to package) to process with moc.
        moc_tool: Label of the moc wrapper shell script.
    """
    native.genrule(
        name = name,
        srcs = headers,
        outs = ["moc_" + h.split("/")[-1].replace(".h", ".cpp") for h in headers],
        tools = [moc_tool],
        local = 1,
        cmd = " && ".join([
            "export QT6_ROOT='{qt6_root}' && $(location {tool}) $(location {h}) $(@D)/moc_{cpp_base}".format(
                qt6_root = QT6_ROOT,
                tool = moc_tool,
                h = h,
                cpp_base = h.split("/")[-1].replace(".h", ".cpp"),
            )
            for h in headers
        ]),
    )

def qt_lrelease(name, ts_files, qt_tool = "//bazel:qt_tool_wrapper"):
    """Generates .qm files from .ts files.
    """
    native.genrule(
        name = name,
        srcs = ts_files,
        outs = [f.split(":")[-1].replace(".ts", ".qm") for f in ts_files],
        tools = [qt_tool],
        local = 1,
        cmd = " && ".join([
            "export QT6_ROOT='{qt6_root}' && $(location {tool}) lrelease $(location {ts}) -qm $(@D)/{qm}".format(
                qt6_root = QT6_ROOT,
                tool = qt_tool,
                ts = f,
                qm = f.split(":")[-1].replace(".ts", ".qm"),
            )
            for f in ts_files
        ]),
    )

def qt_resource(name, qrc_file, deps = [], qt_tool = "//bazel:qt_tool_wrapper"):
    """Generates .cpp from .qrc.
    """
    native.genrule(
        name = name,
        srcs = [qrc_file] + deps,
        outs = [name + ".cpp"],
        tools = [qt_tool],
        local = 1,
        cmd = ("export QT6_ROOT='{qt6_root}' && export TOOL=$$(pwd)/$(location {tool}) && " +
               "export OUT=$$(pwd)/$(@D)/{name}.cpp && " +
               "cd $$(dirname $(location {qrc})) && " +
               "$$TOOL rcc -name {name} $$(basename $(location {qrc})) -o $$OUT").format(
            qt6_root = QT6_ROOT,
            tool = qt_tool,
            qrc = qrc_file,
            name = name,
        ),
    )
