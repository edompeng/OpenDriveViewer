#include "src/utility/open_scenario_parser.h"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include "third_party/pugixml/src/pugixml.hpp"

namespace {

bool IsName(const pugi::xml_node& node, const char* expected) {
  return std::string(node.name()) == expected;
}

double AttrDouble(const pugi::xml_node& node, const char* name,
                  double fallback = 0.0) {
  const pugi::xml_attribute attr = node.attribute(name);
  return attr ? attr.as_double(fallback) : fallback;
}

int AttrInt(const pugi::xml_node& node, const char* name, int fallback = 0) {
  const pugi::xml_attribute attr = node.attribute(name);
  return attr ? attr.as_int(fallback) : fallback;
}

std::string AttrString(const pugi::xml_node& node, const char* name) {
  const pugi::xml_attribute attr = node.attribute(name);
  return attr ? std::string(attr.as_string()) : std::string();
}

std::string GuessEntityType(const pugi::xml_node& scenario_object) {
  if (scenario_object.child("Vehicle")) return "Vehicle";
  if (scenario_object.child("Pedestrian")) return "Pedestrian";
  if (scenario_object.child("MiscObject")) return "MiscObject";
  if (scenario_object.child("ExternalObjectReference")) return "ExternalObject";
  if (scenario_object.child("CatalogReference")) return "CatalogReference";
  return "Unknown";
}

std::optional<OpenScenarioPosition> ParsePositionNode(
    const pugi::xml_node& node) {
  if (!node) return std::nullopt;

  if (IsName(node, "Position")) {
    if (const auto child = node.child("WorldPosition")) {
      OpenScenarioPosition p;
      p.type = OpenScenarioPositionType::kWorld;
      p.x = AttrDouble(child, "x");
      p.y = AttrDouble(child, "y");
      p.z = AttrDouble(child, "z");
      p.h = AttrDouble(child, "h");
      return p;
    }
    if (const auto child = node.child("RoadPosition")) {
      OpenScenarioPosition p;
      p.type = OpenScenarioPositionType::kRoad;
      p.road_id = AttrString(child, "road_id");
      p.s = AttrDouble(child, "s");
      p.t = AttrDouble(child, "t");
      return p;
    }
    if (const auto child = node.child("LanePosition")) {
      OpenScenarioPosition p;
      p.type = OpenScenarioPositionType::kLane;
      p.road_id = AttrString(child, "road_id");
      p.s = AttrDouble(child, "s");
      p.offset = AttrDouble(child, "offset");
      p.lane_id = AttrInt(child, "laneId");
      return p;
    }
    return std::nullopt;
  }

  if (IsName(node, "WorldPosition")) {
    OpenScenarioPosition p;
    p.type = OpenScenarioPositionType::kWorld;
    p.x = AttrDouble(node, "x");
    p.y = AttrDouble(node, "y");
    p.z = AttrDouble(node, "z");
    p.h = AttrDouble(node, "h");
    return p;
  }
  if (IsName(node, "RoadPosition")) {
    OpenScenarioPosition p;
    p.type = OpenScenarioPositionType::kRoad;
    p.road_id = AttrString(node, "road_id");
    p.s = AttrDouble(node, "s");
    p.t = AttrDouble(node, "t");
    return p;
  }
  if (IsName(node, "LanePosition")) {
    OpenScenarioPosition p;
    p.type = OpenScenarioPositionType::kLane;
    p.road_id = AttrString(node, "road_id");
    p.s = AttrDouble(node, "s");
    p.offset = AttrDouble(node, "offset");
    p.lane_id = AttrInt(node, "laneId");
    return p;
  }

  return std::nullopt;
}

}  // namespace

bool ParseOpenScenarioFile(const std::string& file_path, OpenScenarioFile* out,
                           std::string* error_message) {
  if (out == nullptr) {
    if (error_message) *error_message = "Output pointer is null.";
    return false;
  }

  pugi::xml_document doc;
  const pugi::xml_parse_result result = doc.load_file(file_path.c_str());
  if (!result) {
    if (error_message) {
      *error_message = std::string("XML parse failed: ") +
                       result.description() +
                       " (offset=" + std::to_string(result.offset) + ")";
    }
    return false;
  }

  const pugi::xml_node root = doc.child("OpenSCENARIO");
  if (!root) {
    if (error_message) {
      *error_message = "Root node <OpenSCENARIO> not found.";
    }
    return false;
  }

  OpenScenarioFile parsed;
  parsed.path = file_path;
  parsed.file_name = std::filesystem::path(file_path).filename().string();
  if (const pugi::xml_node header = root.child("FileHeader")) {
    const std::string rev_major = AttrString(header, "revMajor");
    const std::string rev_minor = AttrString(header, "revMinor");
    if (rev_major.empty() == false || rev_minor.empty() == false) {
      parsed.version = rev_major + "." + rev_minor;
    }
  }

  std::unordered_map<std::string, std::size_t> entity_index_by_name;

  const pugi::xml_node entities_root = root.child("Entities");
  for (const auto& scenario_object : entities_root.children("ScenarioObject")) {
    OpenScenarioEntity entity;
    entity.name = AttrString(scenario_object, "name");
    entity.object_type = GuessEntityType(scenario_object);
    if (entity.name.empty()) continue;

    entity_index_by_name[entity.name] = parsed.entities.size();
    parsed.entities.push_back(std::move(entity));
  }

  auto ensure_entity =
      [&](const std::string& entity_name) -> OpenScenarioEntity* {
    if (entity_name.empty()) return nullptr;
    auto it = entity_index_by_name.find(entity_name);
    if (it == entity_index_by_name.end()) {
      OpenScenarioEntity entity;
      entity.name = entity_name;
      entity.object_type = "Unknown";
      entity_index_by_name[entity_name] = parsed.entities.size();
      parsed.entities.push_back(std::move(entity));
      it = entity_index_by_name.find(entity_name);
    }
    return &parsed.entities[it->second];
  };

  const pugi::xml_node storyboard = root.child("Storyboard");
  std::function<void(const pugi::xml_node&, const std::string&)>
      walk_storyboard;
  walk_storyboard = [&](const pugi::xml_node& node,
                        const std::string& inherited_entity) {
    std::string current_entity = inherited_entity;
    if (IsName(node, "Private")) {
      const std::string entity_ref = AttrString(node, "entityRef");
      if (entity_ref.empty() == false) {
        current_entity = entity_ref;
      }
    }

    if (current_entity.empty() == false) {
      if (const auto parsed_position = ParsePositionNode(node)) {
        OpenScenarioEntity* entity = ensure_entity(current_entity);
        if (entity) {
          if (entity->initial_position.has_value() == false) {
            entity->initial_position = parsed_position;
          }
          entity->storyboard_positions.push_back(*parsed_position);
        }
      }
    }

    for (const auto& child : node.children()) {
      walk_storyboard(child, current_entity);
    }
  };

  if (storyboard) {
    walk_storyboard(storyboard, std::string());
  }

  *out = std::move(parsed);
  return true;
}
