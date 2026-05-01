# OpenDriveViewer

一个高性能、跨平台的 3D 地理空间查看器，专为 OpenDRIVE 地图数据设计。

[English README](./README.md)

## 功能

- **3D 地理空间渲染**: 使用 OpenGL 快速渲染复杂的道路网络、交叉口和道路标记。
- **交互式地图组件**: 支持车道几何、交通信号、道路标志和包围盒。
- **射线检测拾取与高亮**: 高精度鼠标交互，支持精确拾取单个地图几何元素（车道、物体、逻辑端点）。
- **测量工具**: 内置交互式 3D 长度和距离测量工具，与 UI 完全解耦。
- **数据便捷访问**: 在所有 UI 面板支持丰富的右键上下文菜单，方便快速复制坐标与元素信息。
- **国际化 (i18n)**: 完全支持动态语言切换（简体中文、英文）。
- **跨平台**: 在 Windows、macOS 和 Linux 上经过严格测试。

---

## 🛠 前提条件

无论选择哪种构建系统，都必须安装以下依赖项：

1. **C++17 编译器** (GCC, Clang, 或 MSVC)
2. **Qt6** (Widgets, Gui, OpenGL, OpenGLWidgets, Concurrent) _推荐 Qt 6.5+_
3. **PROJ** (地图投影库)
4. **OpenGL** 环境
5. **GoogleTest** (用于单元测试，通过 Bazel/CMake 自动获取)
6. **gperftools** (可选，用于 tcmalloc 内存优化)

---

## 🚀 构建与测试

本系统同时支持 **CMake** 和 **Bazel**，跨三大主流平台（macOS, Linux, Windows）。

### 方式 1: Bazel (推荐)

我们提供了流线型的、密封的 Bazel 配置。它通过环境配置自动解析 Qt 和 PROJ 位置。

#### 1. 配置
在项目根目录创建一个 `.bazelrc.user` 文件，设置本地机器的 SDK 安装路径：

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
你可以通过 CMake 工具链或前缀路径参数进行配置：

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

> **注意**: CMake 构建时会自动触发 `lupdate` 更新 `.ts` 文件。对于 Windows 的 `vcpkg` 用户，可以在配置时指定 toolchain。

#### 2. 构建与运行
```bash
# 构建
cmake --build . --config Release

# 运行测试
ctest --build-config Release --output-on-failure
```

---

## 第三方库声明

本项目使用了以下开源库，感谢这些优秀项目的作者和维护者：

- **Qt6 Framework**: 跨平台应用程序框架，采用 [LGPL v3](https://www.qt.io/licensing/) 开源协议。
- **PROJ**: 地图投影与坐标转换库，采用 [MIT](https://proj.org/about.html#license) 开源协议。
- **libOpenDRIVE**: OpenDRIVE 地图格式解析库（包含在 `third_party` 中），采用 [MIT](https://github.com/DLR-TS/libOpenDRIVE) 开源协议。
- **pugixml**: 轻量级 C++ XML 处理库（包含在 `third_party` 中），采用 [MIT](https://pugixml.org/) 开源协议。
- **GoogleTest**: Google C++ 测试框架，采用 [BSD-3-Clause](https://github.com/google/googletest) 开源协议。
- **gperftools**: 快速的多线程 malloc() 和性能分析工具，采用 [BSD-3-Clause](https://github.com/gperftools/gperftools) 开源协议。
- **OpenGL**: 用于高性能 3D 渲染。

## 仓库目录结构

```text
.
├── src/                    # 主体 C++ 源码
│   ├── app/                # 程序入口与启动逻辑
│   ├── core/               # 核心领域与基础设施模块
│   ├── logic/              # 业务逻辑与交互逻辑
│   └── ui/                 # Qt UI 与渲染层
├── tests/                  # GoogleTest 单元测试
├── data/                   # 示例 OpenDRIVE 与测试数据
├── scripts/                # 打包与辅助脚本
├── cmake/                  # CMake 辅助脚本
├── bazel/                  # Bazel 规则与辅助脚本
├── third_party/            # 内置第三方依赖
├── CMakeLists.txt          # CMake 入口
├── BUILD.bazel             # Bazel 入口
```

## 代码位置索引

- 程序入口：`src/app/main.cpp`
- 主窗口与核心视图：`src/ui/main_window.*`、`src/ui/widgets/geo_viewer.*`
- 核心数据与模型：`src/core/`
- 业务与交互逻辑：`src/logic/`
- 单元测试：`tests/*_test.cpp`

## 📦 贡献指南

请先阅读：

- [CONTRIBUTING.md](./CONTRIBUTING.md)
- [CODE_OF_CONDUCT.md](./CODE_OF_CONDUCT.md)
- [SECURITY.md](./SECURITY.md)

## 许可证

本项目采用 MIT 许可证。
