#include "src/mcp/mcp_protocol_version.h"

namespace geoviewer::mcp {

namespace {
constexpr char kModernVersion[] = "2026-07-28";
constexpr char kLatestLegacyVersion[] = "2025-11-25";
}  // namespace

QStringList McpProtocolVersionPolicy::SupportedVersions() {
  return {kModernVersion, kLatestLegacyVersion, "2025-06-18", "2025-03-26",
          "2024-11-05"};
}

QString McpProtocolVersionPolicy::NegotiateLegacyVersion(
    const QString& requested_version) {
  if (!requested_version.isEmpty() && IsSupported(requested_version) &&
      !IsModern(requested_version)) {
    return requested_version;
  }
  return kLatestLegacyVersion;
}

bool McpProtocolVersionPolicy::IsSupported(const QString& version) {
  return SupportedVersions().contains(version);
}

bool McpProtocolVersionPolicy::IsModern(const QString& version) {
  return version == kModernVersion;
}

}  // namespace geoviewer::mcp
