# MCP client configuration

GeoViewer exposes the same tools over two standard MCP transports:

- Streamable HTTP: `http://127.0.0.1:8080/mcp` (recommended). Start the GUI
  first and keep it running so tools operate on the visible viewer instance.
- stdio: run `OpenDriveViewer --mcp-stdio`. Use this only when the client should
  launch and own a separate GUI process.

Replace paths and ports below when using a non-default build or port.

The server negotiates legacy MCP initialization revisions used by established
clients and also implements the current `server/discover` capability probe. It
does not identify or special-case clients by product name. Check
`http://127.0.0.1:8080/health` to confirm the running application version,
endpoint, transport, and supported protocol revisions before debugging a
client configuration.

## Codex

Add this `~/.codex/config.toml` entry (or use a trusted project's
`.codex/config.toml`):

```toml
[mcp_servers.odrviewer]
url = "http://127.0.0.1:8080/mcp"
startup_timeout_sec = 10
```

Restart Codex after changing the configuration. Run `codex mcp list`, then use
`/mcp` in the Codex TUI to verify that the server initialized and exposed its
tools.

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

Run `claude mcp list` and open `/mcp` inside Claude Code to confirm the server
shows as connected.

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

OpenCode V2 nests servers under `mcp.servers` and uses `disabled` instead of
`enabled`:

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

Do not set `disabled` to `true`. The stdio executable still creates the viewer,
so GUI-based tools require a desktop session with an available display. Prefer
Streamable HTTP when an existing visible GeoViewer instance should be
controlled. The HTTP listener binds only to `127.0.0.1`; use a secured tunnel
or reverse proxy instead of exposing the unauthenticated local endpoint.
