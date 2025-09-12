# OpenDriveViewer (GeoViewer)

A high-performance, cross-platform 3D geospatial viewer designed for OpenDRIVE and OpenSCENARIO map data.

## Features

- **3D Geospatial Rendering**: Fast rendering of complex road networks, junctions, and roadmarks using OpenGL.
- **Interactive Map Components**: Supports lane geometries, traffic signals, road signs, and bounding boxes.
- **Ray-cast Picking & Highlighting**: High-precision mouse interaction, enabling precise picking of individual map geometry elements (lanes, objects, logic endpoints).
- **Measurement Tool**: Built-in interactive 3D length and distance measurement tools completely decoupled from the UI.
- **Cross-Platform**: Tested and strictly verified on Windows, macOS, and Linux.
- **Robust Software Architecture**: Driven by SOLID principles and modern C++17 design patterns, featuring model-view separation.
- **Dual Build Systems**: Native support for both CMake and Bazel depending on your project needs.

---

## 🛠 Prerequisites

Regardless of your build system choice, you must have the following dependencies installed:

1. **C++17 Compiler** (GCC, Clang, or MSVC)
2. **Qt6** (Widgets, Gui, OpenGL, OpenGLWidgets, Concurrent) _Tested on Qt 6.5+_
3. **PROJ** (Cartographic Projections library)
4. **OpenGL** context (system-provided on most platforms)
5. **Catch2** (for unit testing, pulled automatically via Bazel or checked inside `third_party`)

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
```

---

### Option 2: CMake (Standard Community Approach)

CMake builds provide native IDE integrations (CLion, Visual Studio) and simple `make` / `ninja` generator builds.

#### 1. Configuration
You can configure paths via CMake toolchains or prefix path parameters:

**macOS & Linux**
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

## Third-Party Libraries

This project leverages several high-quality open-source libraries. We are grateful to the authors and maintainers of these projects:

- **Qt6 Framework**: Cross-platform application development framework. Licensed under [LGPL v3](https://www.qt.io/licensing/).
- **PROJ**: Cartographic Projections library. Licensed under [MIT](https://proj.org/about.html#license).
- **libOpenDRIVE**: OpenDRIVE map format parser (bundled in `third_party`). Licensed under [MIT](https://github.com/DLR-TS/libOpenDRIVE).
- **pugixml**: Light-weight C++ XML processing library (bundled in `third_party`). Licensed under [MIT](https://pugixml.org/).
- **Catch2**: Modern C++ test framework (bundled in `third_party`). Licensed under [BSL-1.0](https://github.com/catchorg/Catch2).
- **OpenGL**: For high-performance 3D rendering.

## 📦 Contributing

All contributions must follow our open-source standards:
1. **Google C++ Style Guide**: Ensure you format with `.clang-format`.
2. **SOLID Principles**: New architectures and refactors must emphasize decoupling (e.g. interfaces, Strategy/Facade patterns). 
3. **Tests Required**: Include Catch2 based unit tests for all testable business and operational logic. UI-dependent code should abstract side-effects to `interfaces` to enable decoupled state testing.

## License

This project is licensed under the MIT License.
