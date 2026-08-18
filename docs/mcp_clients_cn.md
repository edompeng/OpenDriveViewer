# MCP 客户端配置

GeoViewer 通过两种标准 MCP 传输提供相同工具：

- Streamable HTTP：`http://127.0.0.1:8080/mcp`（推荐）。先启动 GUI 并保持
  运行，工具会操作这个可见的 Viewer 实例。
- stdio：运行 `OpenDriveViewer --mcp-stdio`。仅在客户端需要自行启动并管理
  独立 GUI 进程时使用。

使用非默认构建路径或端口时，请替换以下示例中的相应内容。

服务端会协商主流客户端仍在使用的旧版 MCP 初始化协议，同时实现当前的
`server/discover` 能力探测；不会按客户端产品名称写特殊分支。排查客户端配置前，
可先访问 `http://127.0.0.1:8080/health`，确认正在运行的程序版本、端点、传输方式
以及支持的协议版本。

## Codex

在 `~/.codex/config.toml` 中加入以下配置（也可放在受信任项目的
`.codex/config.toml` 中）：

```toml
[mcp_servers.odrviewer]
url = "http://127.0.0.1:8080/mcp"
startup_timeout_sec = 10
```

修改配置后重启 Codex。先运行 `codex mcp list`，再在 Codex TUI 中使用 `/mcp`，
确认服务已初始化并暴露工具列表。

## Claude Code

```bash
claude mcp add --transport http odrviewer http://127.0.0.1:8080/mcp
claude mcp list
```

等价的项目级 `.mcp.json` 配置：

```json
{
  "mcpServers": {
    "odrviewer": {
      "type": "http",
      "url": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

运行 `claude mcp list`，并在 Claude Code 中打开 `/mcp`，确认服务状态为已连接。

## Google Antigravity

将以下配置加入全局 `~/.gemini/config/mcp_config.json` 或工作区
`.agents/mcp_config.json`：

```json
{
  "mcpServers": {
    "odrviewer": {
      "serverUrl": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

Antigravity 的远程 MCP 必须使用 `serverUrl`，不支持 `url` 或 `httpUrl`。

## OpenCode

将以下配置加入 `opencode.json` 或 `opencode.jsonc`：

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "odrviewer": {
      "type": "remote",
      "url": "http://127.0.0.1:8080/mcp",
      "enabled": true
    }
  }
}
```

使用 `opencode mcp list` 验证连接状态。

OpenCode V2 需要将服务放在 `mcp.servers` 下，并使用 `disabled` 而不是
`enabled`：

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "servers": {
      "odrviewer": {
        "type": "remote",
        "url": "http://127.0.0.1:8080/mcp"
      }
    }
  }
}
```

## 通用 stdio 配置

使用常见 `mcpServers` stdio 配置结构的客户端可以采用：

```json
{
  "mcpServers": {
    "odrviewer": {
      "command": "/OpenDriveViewer/的绝对路径",
      "args": ["--mcp-stdio"]
    }
  }
}
```

不要设置 `"disabled": true`。stdio 模式仍会创建 Viewer，因此 GUI 工具要求桌面
会话存在可用显示器。如果需要控制已经打开且可见的 GeoViewer 实例，请优先使用
Streamable HTTP。HTTP 服务只监听 `127.0.0.1`；如需远程访问，应使用安全隧道或
反向代理，不要直接暴露未认证的本地端点。
