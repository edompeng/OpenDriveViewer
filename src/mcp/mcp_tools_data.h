#pragma once

#include <vector>
#include "src/mcp/mcp_protocol.h"

namespace geoviewer::mcp {

class McpBridge;

std::vector<ToolDefinition> CreateDataTools(McpBridge* bridge);

}  // namespace geoviewer::mcp
