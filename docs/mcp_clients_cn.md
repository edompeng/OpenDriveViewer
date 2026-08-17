# MCP 客户端配置

GeoViewer 通过两种标准 MCP 传输提供相同工具：

- Streamable HTTP：`http://127.0.0.1:8080/mcp`（推荐）。先启动 GUI 并保持
  运行，工具会操作这个可见的 Viewer 实例。
- stdio：运行 `OpenDriveViewer --mcp-stdio`。仅在客户端需要自行启动并管理
  独立 GUI 进程时使用。

使用非默认构建路径或端口时，请替换以下示例中的相应内容。

## Codex

```bash
codex mcp add odrviewer --url http://127.0.0.1:8080/mcp
codex mcp list
```

等价的 `~/.codex/config.toml` 配置：

```toml
[mcp_servers.odrviewer]
url = "http://127.0.0.1:8080/mcp"
```

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

不要设置 `"disabled": true`。GUI 工具要求桌面会话存在可用显示器。如果需要
控制已经打开且可见的 GeoViewer 实例，请优先使用 Streamable HTTP。
