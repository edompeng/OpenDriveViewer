# OpenDriveViewer

一个高性能、跨平台的 3D 地理空间查看器，专为 OpenDRIVE 地图数据设计。

[English README](./README.md)

## 功能

- **3D 地理空间渲染**: 使用 OpenGL 快速渲染复杂的道路网络、交叉口和道路标记。
- **Model Context Protocol (MCP) 集成**: 内置 MCP 服务端，支持 Stdio 和 HTTP JSON-RPC 传输模式，实现与 AI Agent 和外部工具的无缝交互。
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

> **注意**: 对于 Windows 的 `vcpkg` 用户，可以在配置时指定 toolchain。

#### 2. 构建与运行
```bash
# 构建
cmake --build . --config Release

# 运行测试
ctest --build-config Release --output-on-failure
```

---

## 🤖 Model Context Protocol (MCP) 服务集成

OpenDriveViewer 内置了 MCP 服务端，允许 AI Agent（如 Claude Desktop、Gemini Antigravity 或自定义 LLM 客户端）通过代码操控地图，进行地图结构查询、几何检验、视角控制以及视口截图抓取。

### 命令行启动 MCP 模式

- **Stdio 模式**（标准输入输出，适用于作为 LLM 子进程启动）：
  ```bash
  ./OpenDriveViewer --mcp-stdio
  ```
- **HTTP 模式**（JSON-RPC HTTP 服务，默认端口 8080）：
  ```bash
  ./OpenDriveViewer --mcp-http 8080
  ```

### 支持的核心 MCP 工具

- **地图管理**: `load_map`, `get_map_info`
- **数据查询**: `get_roads`, `get_road_detail`, `get_lane_geometry`, `get_junctions`, `get_signals`, `get_objects`, `query_point`
- **视角相机操控**: `set_camera`, `jump_to_location`, `highlight_element`, `set_layer_visibility`, `set_view_mode`, `take_screenshot`
- **辅助工具**: `add_routing_path`, `clear_routing_paths`, `add_user_points`, `clear_user_points`, `coordinate_transform`
#### mcp配置
- stdio方式
```json
{
  "mcpServers": {
    "odrviewer": {
      "args": [
        "--mcp-stdio"
      ],
      "command": "/Users/edom/code/c++/geoviewer/bazel-bin/src/app/OpenDriveViewer",
      "disabled": true
    }
  }
}
```
- http方式(推荐)
```json http方式
{
  "mcpServers": {
    "odrviewer_web": {
      "headers": {
        "Content-Type": "application/json"
      },
      "serverUrl": "http://localhost:8080/"
    }
  }
}
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
│   ├── mcp/                # Model Context Protocol (MCP) 服务与传输逻辑
│   └── ui/                 # Qt UI 与渲染层
├── tests/                  # GoogleTest 单元测试
├── data/                   # 示例 OpenDRIVE 与测试数据
├── scripts/                # 打包与辅助脚本
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
- MCP 服务与工具：`src/mcp/`
- 单元测试：`tests/*_test.cpp`

## 📦 贡献指南

请先阅读：

- [CONTRIBUTING.md](./CONTRIBUTING.md)
- [CODE_OF_CONDUCT.md](./CODE_OF_CONDUCT.md)
- [SECURITY.md](./SECURITY.md)

## 许可证

本项目采用 MIT 许可证。
