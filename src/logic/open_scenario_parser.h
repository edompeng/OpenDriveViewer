#pragma once

#include <QString>
#include <optional>
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
  QString road_id;
  double s = 0.0;
  double t = 0.0;
  int lane_id = 0;
  double offset = 0.0;
};

struct OpenScenarioEntity {
  QString name;
  QString object_type;
  std::optional<OpenScenarioPosition> initial_position;
  std::vector<OpenScenarioPosition> storyboard_positions;
};

struct OpenScenarioFile {
  QString path;
  QString file_name;
  QString version;
  std::vector<OpenScenarioEntity> entities;
};

bool ParseOpenScenarioFile(const QString& file_path, OpenScenarioFile* out,
                           QString* error_message);
