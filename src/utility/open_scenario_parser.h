#pragma once

#include <optional>
#include <string>
#include <vector>

enum class OpenScenarioPositionType {
  kUnknown = 0,
  kWorld,
  kRoad,
  kLane,
};

struct OpenScenarioPosition {
  OpenScenarioPositionType type = OpenScenarioPositionType::kUnknown;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double h = 0.0;
  std::string road_id;
  double s = 0.0;
  double t = 0.0;
  int lane_id = 0;
  double offset = 0.0;
};

struct OpenScenarioEntity {
  std::string name;
  std::string object_type;
  std::optional<OpenScenarioPosition> initial_position;
  std::vector<OpenScenarioPosition> storyboard_positions;
};

struct OpenScenarioFile {
  std::string path;
  std::string file_name;
  std::string version;
  std::vector<OpenScenarioEntity> entities;
};

bool ParseOpenScenarioFile(const std::string& file_path, OpenScenarioFile* out,
                           std::string* error_message);
