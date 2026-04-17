#include "src/app/open_scenario_parser.h"

#include <QFileInfo>
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

QString AttrQString(const pugi::xml_node& node, const char* name) {
  const pugi::xml_attribute attr = node.attribute(name);
  return attr ? QString::fromUtf8(attr.as_string()) : QString();
}

QString GuessEntityType(const pugi::xml_node& scenario_object) {
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
      p.road_id = AttrQString(child, "road_id");
      p.s = AttrDouble(child, "s");
      p.t = AttrDouble(child, "t");
      return p;
    }
    if (const auto child = node.child("LanePosition")) {
      OpenScenarioPosition p;
      p.type = OpenScenarioPositionType::kLane;
      p.road_id = AttrQString(child, "road_id");
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
    p.road_id = AttrQString(node, "road_id");
    p.s = AttrDouble(node, "s");
    p.t = AttrDouble(node, "t");
    return p;
  }
  if (IsName(node, "LanePosition")) {
    OpenScenarioPosition p;
    p.type = OpenScenarioPositionType::kLane;
    p.road_id = AttrQString(node, "road_id");
    p.s = AttrDouble(node, "s");
    p.offset = AttrDouble(node, "offset");
    p.lane_id = AttrInt(node, "laneId");
    return p;
  }

  return std::nullopt;
}

}  // namespace

bool ParseOpenScenarioFile(const QString& file_path, OpenScenarioFile* out,
                           QString* error_message) {
  if (!out) {
    if (error_message) *error_message = "Output pointer is null.";
    return false;
  }

  pugi::xml_document doc;
  const pugi::xml_parse_result result =
      doc.load_file(file_path.toStdString().c_str());
  if (!result) {
    if (error_message) {
      *error_message = QString("XML parse failed: %1 (offset=%2)")
                           .arg(QString::fromUtf8(result.description()))
                           .arg((int)result.offset);
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
  parsed.file_name = QFileInfo(file_path).fileName();
  if (const pugi::xml_node header = root.child("FileHeader")) {
    const QString rev_major = AttrQString(header, "revMajor");
    const QString rev_minor = AttrQString(header, "revMinor");
    if (!rev_major.isEmpty() || !rev_minor.isEmpty()) {
      parsed.version = QString("%1.%2").arg(rev_major).arg(rev_minor);
    }
  }

  std::unordered_map<std::string, std::size_t> entity_index_by_name;

  const pugi::xml_node entities_root = root.child("Entities");
  for (const auto& scenario_object : entities_root.children("ScenarioObject")) {
    OpenScenarioEntity entity;
    entity.name = AttrQString(scenario_object, "name");
    entity.object_type = GuessEntityType(scenario_object);
    if (entity.name.isEmpty()) continue;

    entity_index_by_name[entity.name.toStdString()] = parsed.entities.size();
    parsed.entities.push_back(std::move(entity));
  }

  auto ensure_entity = [&](const QString& entity_name) -> OpenScenarioEntity* {
    if (entity_name.isEmpty()) return nullptr;
    const std::string key = entity_name.toStdString();
    auto it = entity_index_by_name.find(key);
    if (it == entity_index_by_name.end()) {
      OpenScenarioEntity entity;
      entity.name = entity_name;
      entity.object_type = "Unknown";
      entity_index_by_name[key] = parsed.entities.size();
      parsed.entities.push_back(std::move(entity));
      it = entity_index_by_name.find(key);
    }
    return &parsed.entities[it->second];
  };

  const pugi::xml_node storyboard = root.child("Storyboard");
  std::function<void(const pugi::xml_node&, const QString&)> walk_storyboard;
  walk_storyboard = [&](const pugi::xml_node& node,
                        const QString& inherited_entity) {
    QString current_entity = inherited_entity;
    if (IsName(node, "Private")) {
      const QString entity_ref = AttrQString(node, "entityRef");
      if (!entity_ref.isEmpty()) {
        current_entity = entity_ref;
      }
    }

    if (!current_entity.isEmpty()) {
      if (const auto parsed_position = ParsePositionNode(node)) {
        OpenScenarioEntity* entity = ensure_entity(current_entity);
        if (entity) {
          if (!entity->initial_position.has_value()) {
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
    walk_storyboard(storyboard, QString());
  }

  *out = std::move(parsed);
  return true;
}
