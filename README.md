# OpenDriveViewer

A high-performance, cross-platform 3D geospatial viewer designed for OpenDRIVE map data.

[中文说明](./readme_cn.md)

## Features

- **3D Geospatial Rendering**: Fast rendering of complex road networks, junctions, and roadmarks using OpenGL.
- **Model Context Protocol (MCP) Integration**: Built-in MCP server supporting stdio and HTTP JSON-RPC transports for seamless interaction with AI agents and external tools.
- **Interactive Map Components**: Supports lane geometries, traffic signals, road signs, and bounding boxes.
- **Ray-cast Picking & Highlighting**: High-precision mouse interaction, enabling precise picking of individual map geometry elements (lanes, objects, logic endpoints).
- **Measurement Tool**: Built-in interactive 3D length and distance measurement tools completely decoupled from the UI.
- **Data Accessibility**: Rich right-click context menus across all UI panels to easily copy coordinate and element information.
- **Internationalization (i18n)**: Full dynamic language switching support (English, Simplified Chinese).
- **Cross-Platform**: Tested and strictly verified on Windows, macOS, and Linux.

---

## 🛠 Prerequisites

Regardless of your build system choice, you must have the following dependencies installed:

1. **C++17 Compiler** (GCC, Clang, or MSVC)
2. **Qt6** (Widgets, Gui, OpenGL, OpenGLWidgets, Concurrent) _Tested on Qt 6.5+_
3. **PROJ** (Cartographic Projections library)
4. **OpenGL** context (system-provided on most platforms)
5. **GoogleTest** (for unit testing, pulled automatically via Bazel or CMake)
6. **gperftools** (optional, for tcmalloc memory optimization)

---

## 🚀 Building & Testing

This project concurrently supports **CMake** and **Bazel** across three major platforms (macOS, Linux, Windows).
Choose your preferred build system below.

### Option 1: Bazel (Recommended for Scalability)

We provide a streamlined, hermetic Bazel configuration. It auto-resolves Qt and PROJ locations through environment configuration.

#### 1. Configuration
Create a `.bazelrc.user` file in the project root to set the SDK installation paths for your local machine:

**macOS (Homebrew)**
```bash
build --action_env=QT6_ROOT=/Users/you/Qt/6.9.1/macos
build --action_env=PROJ_ROOT=/opt/homebrew/opt/proj
```

**Linux (System Packages)**
```bash
build --action_env=QT6_ROOT=/usr
build --action_env=PROJ_ROOT=/usr
```

**Windows (MSVC)**
```bash
build --action_env=QT6_ROOT=C:/Qt/6.9.1/msvc2022_64
build --action_env=PROJ_ROOT=C:/OSGeo4W
```

#### 2. Build & Run
```bash
# Build the application
bazel build //src/app:OpenDriveViewer

# Run all test suites
bazel test //tests:all

# Update translation files (.ts)
bazel run //bazel:update_translations
```

---

### Option 2: CMake (Standard Community Approach)

CMake builds provide native IDE integrations (CLion, Visual Studio) and simple `make` / `ninja` generator builds.

#### 1. Configuration
You can configure paths via CMake toolchains or prefix path parameters:

**macOS**
```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="/Users/you/Qt/6.9.1/macos;/opt/homebrew/opt/proj" -DCMAKE_BUILD_TYPE=Release
```

**Linux**
```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="/path/to/Qt6;/path/to/proj" -DCMAKE_BUILD_TYPE=Release
```

**Windows (MSVC)**
```cmd
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\6.9.1\msvc2022_64;C:\OSGeo4W" -A x64
```

> **Note**: For `vcpkg` users on Windows, you can define your toolchain during configuration:
> `-DCMAKE_TOOLCHAIN_FILE="[vcpkg root]/scripts/buildsystems/vcpkg.cmake"`

#### 2. Build & Run
```bash
# Build
cmake --build . --config Release

# Run tests
ctest --build-config Release --output-on-failure
```

---

## 🤖 Model Context Protocol (MCP) Integration

OpenDriveViewer features a built-in MCP server, allowing AI agents (such as Claude Desktop, Gemini Antigravity, or custom LLM clients) to query map structures, inspect geometries, control camera viewports, and capture screenshots programmatically.

### Launching MCP Modes via CLI

- **Stdio Mode** (standard I/O for LLM subprocess integration):
  ```bash
  ./OpenDriveViewer --mcp-stdio
  ```
- **HTTP Mode** (JSON-RPC HTTP server, default port 8080):
  ```bash
  ./OpenDriveViewer --mcp-http 8080
  ```

### Key MCP Tools Supported

- **Map Management**: `load_map`, `get_map_info`
- **Data Inspection**: `get_roads`, `get_road_detail`, `get_lane_geometry`, `get_junctions`, `get_signals`, `get_objects`, `query_point`
- **Viewport & Camera**: `set_camera`, `jump_to_location`, `highlight_element`, `set_layer_visibility`, `set_view_mode`, `take_screenshot`
- **Navigation & Utilities**: `add_routing_path`, `clear_routing_paths`, `add_user_points`, `clear_user_points`, `coordinate_transform`

---

## Third-Party Libraries

This project leverages several high-quality open-source libraries. We are grateful to the authors and maintainers of these projects:

- **Qt6 Framework**: Cross-platform application development framework. Licensed under [LGPL v3](https://www.qt.io/licensing/).
- **PROJ**: Cartographic Projections library. Licensed under [MIT](https://proj.org/about.html#license).
- **libOpenDRIVE**: OpenDRIVE map format parser (bundled in `third_party`). Licensed under [MIT](https://github.com/DLR-TS/libOpenDRIVE).
- **pugixml**: Light-weight C++ XML processing library (bundled in `third_party`). Licensed under [MIT](https://pugixml.org/).
- **GoogleTest**: Google's C++ test framework. Licensed under [BSD-3-Clause](https://github.com/google/googletest).
- **gperftools**: Fast, multi-threaded malloc() and nifty performance analysis tools. Licensed under [BSD-3-Clause](https://github.com/gperftools/gperftools).
- **OpenGL**: For high-performance 3D rendering.

## Repository Layout

```text
.
├── src/                    # Main C++ source code
│   ├── app/                # Program entry and app bootstrap
│   ├── core/               # Core domain/infrastructure modules
│   ├── logic/              # Business logic and interaction logic
│   ├── mcp/                # Model Context Protocol (MCP) server & transport logic
│   └── ui/                 # Qt UI and rendering layer
├── tests/                  # GoogleTest test cases
├── data/                   # Sample OpenDRIVE files and test data
├── scripts/                # Packaging and helper scripts
├── bazel/                  # Bazel rules and helper scripts
├── third_party/            # Vendored third-party dependencies
├── CMakeLists.txt          # CMake entry
├── BUILD.bazel             # Bazel entry
```

## Code Location Guide

- App entry: `src/app/main.cpp`
- Main window and viewer UI: `src/ui/main_window.*`, `src/ui/widgets/geo_viewer.*`
- Core data/model modules: `src/core/`
- Domain and interaction logic: `src/logic/`
- MCP server & tools: `src/mcp/`
- Unit tests: `tests/*_test.cpp`
- Build scripts:
  - CMake entry: `CMakeLists.txt`
  - Bazel entry: `BUILD.bazel`, `MODULE.bazel`

## 📦 Contributing

Please read:

- [CONTRIBUTING.md](./CONTRIBUTING.md)
- [CODE_OF_CONDUCT.md](./CODE_OF_CONDUCT.md)
- [SECURITY.md](./SECURITY.md)

## License

This project is licensed under the MIT License.
