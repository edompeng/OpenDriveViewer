# OpenDriveViewer (GeoViewer)

一个高性能、跨平台的 3D 地理空间查看器，专为 OpenDRIVE 和 OpenSCENARIO 地图数据设计。

## 功能

- **3D 地理空间渲染**: 使用 OpenGL 快速渲染复杂的道路网络、交叉口和道路标记。
- **交互式地图组件**: 支持车道几何、交通信号、道路标志和包围盒。
- **射线检测拾取与高亮**: 高精度鼠标交互，支持精确拾取单个地图几何元素（车道、物体、逻辑端点）。
- **测量工具**: 内置交互式 3D 长度和距离测量工具，与 UI 完全解耦。
- **跨平台**: 在 Windows、macOS 和 Linux 上经过严格测试。
- **稳健的软件架构**: 遵循 SOLID 原则和现代 C++17 设计模式，具有模型-视图分离特性。
- **双构建系统**: 原生支持 CMake 和 Bazel，根据项目需求灵活选择。
- **国际化 (i18n)**: 完全支持动态语言切换（简体中文、英文）。

---

## 🛠 前提条件

无论选择哪种构建系统，都必须安装以下依赖项：

1. **C++17 编译器** (GCC, Clang, 或 MSVC)
2. **Qt6** (Widgets, Gui, OpenGL, OpenGLWidgets, Concurrent) _推荐 Qt 6.5+_
3. **PROJ** (地图投影库)
4. **OpenGL** 环境
5. **Catch2** (用于单元测试)

---

## 🚀 构建与测试

本系统同时支持 **CMake** 和 **Bazel**。

### 方式 1: Bazel (推荐)

我们提供了流线型的、密封的 Bazel 配置。它通过环境配置自动解析 Qt 和 PROJ 位置。

#### 1. 配置
在项目根目录创建一个 `.bazelrc.user` 文件，设置本地机器的 SDK 安装路径：

**macOS (Homebrew)**
```bash
build --action_env=QT6_ROOT=/Users/you/Qt/6.9.1/macos
build --action_env=PROJ_ROOT=/opt/homebrew/opt/proj
```

#### 2. 构建与运行
```bash
# 构建应用程序
bazel build //src/app:OpenDriveViewer

# 运行所有测试
bazel test //tests:all

# 更新国际化翻译文件 (.ts)
bazel run //bazel:update_translations
```

---

### 方式 2: CMake (标准方式)

CMake 构建提供原生 IDE 集成（CLion, Visual Studio）和简单的 `make` / `ninja` 构建。

#### 1. 配置
**macOS & Linux**
```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="/path/to/Qt6;/path/to/proj" -DCMAKE_BUILD_TYPE=Release
```

#### 2. 构建与运行
```bash
# 构建
cmake --build . --config Release

# 运行测试
ctest --build-config Release --output-on-failure
```

> **注意**: CMake 构建时会自动触发 `lupdate` 更新 `.ts` 文件。

---

## 第三方库声明

本项目使用了以下开源库，感谢这些优秀项目的作者和维护者：

- **Qt6 Framework**: 跨平台应用程序框架，采用 [LGPL v3](https://www.qt.io/licensing/) 开源协议。
- **PROJ**: 地图投影与坐标转换库，采用 [MIT](https://proj.org/about.html#license) 开源协议。
- **libOpenDRIVE**: OpenDRIVE 地图格式解析库（包含在 `third_party` 中），采用 [MIT](https://github.com/DLR-TS/libOpenDRIVE) 开源协议。
- **pugixml**: 轻量级 C++ XML 处理库（包含在 `third_party` 中），采用 [MIT](https://pugixml.org/) 开源协议。
- **Catch2**: 现代 C++ 测试框架（包含在 `third_party` 中），采用 [BSL-1.0](https://github.com/catchorg/Catch2) 开源协议。
- **OpenGL**: 用于高性能 3D 渲染。

## 📦 贡献指南

所有贡献必须遵循我们的标准：
1. **Google C++ 代码规范**: 使用 `.clang-format` 进行格式化。
2. **SOLID 原则**: 新架构和重构必须强调解耦。
3. **测试要求**: 所有可测试的业务逻辑必须包含基于 Catch2 的单元测试。

## 许可证

本项目采用 MIT 许可证。
