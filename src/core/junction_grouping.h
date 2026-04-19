#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "third_party/libOpenDRIVE/include/Math.hpp"

namespace odr {
class OpenDriveMap;
}

enum class JunctionSemanticType {
  kUnknown = 0,
  kCrossroad,
  kTIntersection,
  kThreeWayIntersection,
  kIrregularIntersection,
  kUTurnOnly,
};

struct JunctionBox3D {
  odr::Vec3D min{0.0, 0.0, 0.0};
  odr::Vec3D max{0.0, 0.0, 0.0};
  bool valid = false;
};

struct JunctionClusterOptions {
  double incoming_box_padding = 5.0;
  double max_box_gap_xy = 20.0;
  double max_box_gap_z = 3.0;
  double angle_merge_threshold_deg = 20.0;
  double opposite_angle_threshold_deg = 30.0;
  double right_angle_threshold_deg = 25.0;
};

struct JunctionArmInfo {
  std::string road_id;
  odr::Vec3D point{0.0, 0.0, 0.0};
  double angle_rad = 0.0;
  bool from_incoming_road = false;
};

struct JunctionClusterMember {
  std::string junction_id;
  std::string junction_name;
  std::set<std::string> incoming_road_ids;
  std::set<std::string> connecting_road_ids;
  JunctionBox3D incoming_box;
  std::vector<JunctionArmInfo> incoming_arms;
};

struct JunctionClusterGroup {
  std::string group_id;
  std::vector<std::string> junction_ids;
  std::set<std::string> incoming_road_ids;
  std::set<std::string> connecting_road_ids;
  std::set<std::string> external_road_ids;
  JunctionBox3D incoming_box;
  std::vector<JunctionArmInfo> boundary_arms;
  JunctionSemanticType semantic_type = JunctionSemanticType::kUnknown;
};

struct JunctionClusterResult {
  std::vector<JunctionClusterMember> junctions;
  std::vector<JunctionClusterGroup> groups;
  std::map<std::string, std::size_t> junction_id_to_group_index;
};

class JunctionClusterUtil {
 public:
  static JunctionClusterResult Analyze(
      const odr::OpenDriveMap& map, const JunctionClusterOptions& options = {});

  static JunctionSemanticType ClassifyByAngles(
      const std::vector<double>& angles_rad,
      const JunctionClusterOptions& options = {});

  static const char* SemanticTypeToString(JunctionSemanticType type);

  static bool BoxesOverlap(const JunctionBox3D& lhs, const JunctionBox3D& rhs);
  static double BoxDistance(const JunctionBox3D& lhs, const JunctionBox3D& rhs);
  static double BoxHorizontalDistance(const JunctionBox3D& lhs,
                                      const JunctionBox3D& rhs);
  static double BoxVerticalDistance(const JunctionBox3D& lhs,
                                    const JunctionBox3D& rhs);
};
