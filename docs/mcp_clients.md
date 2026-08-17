# MCP client configuration

GeoViewer exposes the same tools over two standard MCP transports:

- Streamable HTTP: `http://127.0.0.1:8080/mcp` (recommended). Start the GUI
  first and keep it running so tools operate on the visible viewer instance.
- stdio: run `OpenDriveViewer --mcp-stdio`. Use this only when the client should
  launch and own a separate GUI process.

Replace paths and ports below when using a non-default build or port.

## Codex

```bash
codex mcp add odrviewer --url http://127.0.0.1:8080/mcp
codex mcp list
```

Equivalent `~/.codex/config.toml` entry:

```toml
[mcp_servers.odrviewer]
url = "http://127.0.0.1:8080/mcp"
```

## Claude Code

```bash
claude mcp add --transport http odrviewer http://127.0.0.1:8080/mcp
claude mcp list
```

Equivalent project `.mcp.json` entry:

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

Add this entry to the global `~/.gemini/config/mcp_config.json` or workspace
`.agents/mcp_config.json` file:

```json
{
  "mcpServers": {
    "odrviewer": {
      "serverUrl": "http://127.0.0.1:8080/mcp"
    }
  }
}
```

Antigravity requires `serverUrl` for remote MCP servers; `url` and `httpUrl`
are not accepted.

## OpenCode

Add this entry to `opencode.json` or `opencode.jsonc`:

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

Verify it with `opencode mcp list`.

## Generic stdio configuration

Clients using the common `mcpServers` stdio schema can use:

```json
{
  "mcpServers": {
    "odrviewer": {
      "command": "/absolute/path/to/OpenDriveViewer",
      "args": ["--mcp-stdio"]
    }
  }
}
```

Do not set `disabled` to `true`. GUI-based tools require a desktop session with
an available display. Prefer Streamable HTTP when an existing visible
GeoViewer instance should be controlled.
