#include "src/utility/junction_grouping.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>
#include "third_party/libOpenDRIVE/include/Junction.h"
#include "third_party/libOpenDRIVE/include/OpenDriveMap.h"
#include "third_party/libOpenDRIVE/include/Road.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

double DegToRad(double degrees) { return degrees * kPi / 180.0; }

double NormalizeAngle(double angle_rad) {
  while (angle_rad < 0.0) {
    angle_rad += 2.0 * kPi;
  }
  while (angle_rad >= 2.0 * kPi) {
    angle_rad -= 2.0 * kPi;
  }
  return angle_rad;
}

double CircularDistance(double lhs, double rhs) {
  const double diff = std::fabs(NormalizeAngle(lhs) - NormalizeAngle(rhs));
  return std::min(diff, 2.0 * kPi - diff);
}

double CircularDistanceModuloPi(double lhs, double rhs) {
  const double lhs_mod = std::fmod(lhs + kPi, kPi);
  const double rhs_mod = std::fmod(rhs + kPi, kPi);
  const double diff = std::fabs(lhs_mod - rhs_mod);
  return std::min(diff, kPi - diff);
}

double NormalizeAxisAngle(double angle_rad) {
  double normalized = std::fmod(angle_rad, kPi);
  if (normalized < 0.0) {
    normalized += kPi;
  }
  return normalized;
}

struct PointAndAngle {
  odr::Vec3D point{0.0, 0.0, 0.0};
  double angle_rad = 0.0;
};

PointAndAngle GetRoadPointAndDirection(const odr::Road& road, double s,
                                       bool forward) {
  odr::Vec3D tangent{0.0, 0.0, 0.0};
  const odr::Vec3D point =
      road.get_xyz(s, 0.0, 0.0, &tangent, nullptr, nullptr);
  if (!forward) {
    tangent[0] *= -1.0;
    tangent[1] *= -1.0;
    tangent[2] *= -1.0;
  }
  return {point, std::atan2(tangent[1], tangent[0])};
}

bool TryGetIncomingArm(const odr::Road& road, const std::string& junction_id,
                       JunctionArmInfo& arm) {
  if (road.predecessor.type == odr::RoadLink::Type_Junction &&
      road.predecessor.id == junction_id) {
    const PointAndAngle point_and_angle =
        GetRoadPointAndDirection(road, 0.0, true);
    arm.point = point_and_angle.point;
    arm.angle_rad = NormalizeAngle(point_and_angle.angle_rad);
    arm.from_incoming_road = true;
    return true;
  }

  if (road.successor.type == odr::RoadLink::Type_Junction &&
      road.successor.id == junction_id) {
    const PointAndAngle point_and_angle =
        GetRoadPointAndDirection(road, road.length, false);
    arm.point = point_and_angle.point;
    arm.angle_rad = NormalizeAngle(point_and_angle.angle_rad);
    arm.from_incoming_road = true;
    return true;
  }

  return false;
}

bool TryGetExitArm(const odr::Road& road, const std::string& linked_road_id,
                   JunctionArmInfo& arm) {
  if (road.predecessor.type == odr::RoadLink::Type_Road &&
      road.predecessor.id == linked_road_id) {
    const PointAndAngle point_and_angle =
        GetRoadPointAndDirection(road, 0.0, true);
    arm.point = point_and_angle.point;
    arm.angle_rad = NormalizeAngle(point_and_angle.angle_rad);
    arm.from_incoming_road = false;
    return true;
  }

  if (road.successor.type == odr::RoadLink::Type_Road &&
      road.successor.id == linked_road_id) {
    const PointAndAngle point_and_angle =
        GetRoadPointAndDirection(road, road.length, false);
    arm.point = point_and_angle.point;
    arm.angle_rad = NormalizeAngle(point_and_angle.angle_rad);
    arm.from_incoming_road = false;
    return true;
  }

  return false;
}

void ExtendBox(JunctionBox3D& box, const odr::Vec3D& point) {
  if (!box.valid) {
    box.min = point;
    box.max = point;
    box.valid = true;
    return;
  }

  for (int axis = 0; axis < 3; ++axis) {
    box.min[axis] = std::min(box.min[axis], point[axis]);
    box.max[axis] = std::max(box.max[axis], point[axis]);
  }
}

JunctionBox3D ExpandedBox(const JunctionBox3D& box, double padding) {
  JunctionBox3D expanded = box;
  if (!expanded.valid) {
    return expanded;
  }
  for (int axis = 0; axis < 3; ++axis) {
    expanded.min[axis] -= padding;
    expanded.max[axis] += padding;
  }
  return expanded;
}

void MergeBoxInto(JunctionBox3D& dst, const JunctionBox3D& src) {
  if (!src.valid) {
    return;
  }
  ExtendBox(dst, src.min);
  ExtendBox(dst, src.max);
}

std::vector<double> MergeAngles(const std::vector<double>& angles_rad,
                                double threshold_rad, bool modulo_pi) {
  std::vector<double> merged;
  for (double angle : angles_rad) {
    angle = modulo_pi ? NormalizeAxisAngle(angle) : NormalizeAngle(angle);
    bool placed = false;
    for (double& existing : merged) {
      const double distance = modulo_pi
                                  ? CircularDistanceModuloPi(existing, angle)
                                  : CircularDistance(existing, angle);
      if (distance <= threshold_rad) {
        if (modulo_pi) {
          const double x = std::cos(2.0 * existing) + std::cos(2.0 * angle);
          const double y = std::sin(2.0 * existing) + std::sin(2.0 * angle);
          existing = NormalizeAxisAngle(0.5 * std::atan2(y, x));
        } else {
          const double x = std::cos(existing) + std::cos(angle);
          const double y = std::sin(existing) + std::sin(angle);
          existing = NormalizeAngle(std::atan2(y, x));
        }
        placed = true;
        break;
      }
    }
    if (!placed) {
      merged.push_back(angle);
    }
  }
  return merged;
}

bool HasOppositePair(const std::vector<double>& arms, double threshold_rad) {
  for (std::size_t i = 0; i < arms.size(); ++i) {
    for (std::size_t j = i + 1; j < arms.size(); ++j) {
      const double diff = CircularDistance(arms[i], arms[j]);
      if (std::fabs(diff - kPi) <= threshold_rad) {
        return true;
      }
    }
  }
  return false;
}

class DisjointSet {
 public:
  explicit DisjointSet(const std::vector<std::string>& ids) {
    for (const auto& id : ids) {
      parent_[id] = id;
    }
  }

  std::string Find(const std::string& id) {
    auto it = parent_.find(id);
    if (it == parent_.end()) {
      throw std::out_of_range("Unknown disjoint-set id: " + id);
    }
    if (it->second == id) {
      return id;
    }
    it->second = Find(it->second);
    return it->second;
  }

  void Union(const std::string& lhs, const std::string& rhs) {
    const std::string root_lhs = Find(lhs);
    const std::string root_rhs = Find(rhs);
    if (root_lhs != root_rhs) {
      parent_[root_rhs] = root_lhs;
    }
  }

 private:
  std::map<std::string, std::string> parent_;
};

}  // namespace

bool JunctionClusterUtil::BoxesOverlap(const JunctionBox3D& lhs,
                                       const JunctionBox3D& rhs) {
  if (!lhs.valid || !rhs.valid) {
    return false;
  }
  for (int axis = 0; axis < 3; ++axis) {
    if (lhs.max[axis] < rhs.min[axis] || rhs.max[axis] < lhs.min[axis]) {
      return false;
    }
  }
  return true;
}

double JunctionClusterUtil::BoxDistance(const JunctionBox3D& lhs,
                                        const JunctionBox3D& rhs) {
  if (!lhs.valid || !rhs.valid) {
    return std::numeric_limits<double>::infinity();
  }

  const double horizontal = BoxHorizontalDistance(lhs, rhs);
  const double vertical = BoxVerticalDistance(lhs, rhs);
  return std::sqrt(horizontal * horizontal + vertical * vertical);
}

double JunctionClusterUtil::BoxHorizontalDistance(const JunctionBox3D& lhs,
                                                  const JunctionBox3D& rhs) {
  if (!lhs.valid || !rhs.valid) {
    return std::numeric_limits<double>::infinity();
  }

  double squared = 0.0;
  for (int axis : {0, 1}) {
    double gap = 0.0;
    if (lhs.max[axis] < rhs.min[axis]) {
      gap = rhs.min[axis] - lhs.max[axis];
    } else if (rhs.max[axis] < lhs.min[axis]) {
      gap = lhs.min[axis] - rhs.max[axis];
    }
    squared += gap * gap;
  }
  return std::sqrt(squared);
}

double JunctionClusterUtil::BoxVerticalDistance(const JunctionBox3D& lhs,
                                                const JunctionBox3D& rhs) {
  if (!lhs.valid || !rhs.valid) {
    return std::numeric_limits<double>::infinity();
  }
  if (lhs.max[2] < rhs.min[2]) {
    return rhs.min[2] - lhs.max[2];
  }
  if (rhs.max[2] < lhs.min[2]) {
    return lhs.min[2] - rhs.max[2];
  }
  return 0.0;
}

JunctionSemanticType JunctionClusterUtil::ClassifyByAngles(
    const std::vector<double>& angles_rad,
    const JunctionClusterOptions& options) {
  if (angles_rad.empty()) {
    return JunctionSemanticType::kUnknown;
  }

  const double merge_threshold = DegToRad(options.angle_merge_threshold_deg);
  const double opposite_threshold =
      DegToRad(options.opposite_angle_threshold_deg);
  const double right_angle_threshold =
      DegToRad(options.right_angle_threshold_deg);

  const std::vector<double> arms =
      MergeAngles(angles_rad, merge_threshold, false);
  const std::vector<double> axes = MergeAngles(arms, merge_threshold, true);

  if (axes.size() <= 1 || (arms.size() <= 2 && axes.size() <= 1)) {
    return JunctionSemanticType::kUTurnOnly;
  }

  if (arms.size() == 3) {
    return HasOppositePair(arms, opposite_threshold)
               ? JunctionSemanticType::kTIntersection
               : JunctionSemanticType::kThreeWayIntersection;
  }

  if (arms.size() >= 4) {
    if (axes.size() == 2) {
      const double axis_delta = CircularDistanceModuloPi(axes[0], axes[1]);
      if (std::fabs(axis_delta - kPi / 2.0) <= right_angle_threshold) {
        return JunctionSemanticType::kCrossroad;
      }
    }
    return JunctionSemanticType::kIrregularIntersection;
  }

  return JunctionSemanticType::kIrregularIntersection;
}

const char* JunctionClusterUtil::SemanticTypeToString(
    JunctionSemanticType type) {
  switch (type) {
    case JunctionSemanticType::kCrossroad:
      return "crossroad";
    case JunctionSemanticType::kTIntersection:
      return "t_intersection";
    case JunctionSemanticType::kThreeWayIntersection:
      return "three_way_intersection";
    case JunctionSemanticType::kIrregularIntersection:
      return "irregular_intersection";
    case JunctionSemanticType::kUTurnOnly:
      return "u_turn_only";
    case JunctionSemanticType::kUnknown:
    default:
      return "unknown";
  }
}

JunctionClusterResult JunctionClusterUtil::Analyze(
    const odr::OpenDriveMap& map, const JunctionClusterOptions& options) {
  JunctionClusterResult result;

  std::map<std::string, JunctionClusterMember> member_by_id;
  std::vector<std::string> ordered_junction_ids;

  for (const auto& id_and_junction : map.id_to_junction) {
    const auto& junction = id_and_junction.second;
    JunctionClusterMember member;
    member.junction_id = junction.id;
    member.junction_name = junction.name;

    JunctionBox3D raw_box;
    for (const auto& id_and_connection : junction.id_to_connection) {
      const auto& connection = id_and_connection.second;
      if (!connection.incoming_road.empty()) {
        member.incoming_road_ids.insert(connection.incoming_road);
      }
      if (!connection.connecting_road.empty()) {
        member.connecting_road_ids.insert(connection.connecting_road);
      }

      const auto road_it = map.id_to_road.find(connection.incoming_road);
      if (road_it == map.id_to_road.end()) {
        continue;
      }

      JunctionArmInfo arm;
      arm.road_id = connection.incoming_road;
      if (!TryGetIncomingArm(road_it->second, junction.id, arm)) {
        continue;
      }
      member.incoming_arms.push_back(arm);
      ExtendBox(raw_box, arm.point);
    }

    member.incoming_box = ExpandedBox(raw_box, options.incoming_box_padding);
    ordered_junction_ids.push_back(junction.id);
    member_by_id.emplace(junction.id, std::move(member));
  }

  for (const auto& id_and_member : member_by_id) {
    result.junctions.push_back(id_and_member.second);
  }
  std::sort(
      result.junctions.begin(), result.junctions.end(),
      [](const JunctionClusterMember& lhs, const JunctionClusterMember& rhs) {
        return lhs.junction_id < rhs.junction_id;
      });

  DisjointSet disjoint_set(ordered_junction_ids);
  for (std::size_t i = 0; i < result.junctions.size(); ++i) {
    for (std::size_t j = i + 1; j < result.junctions.size(); ++j) {
      const auto& lhs = result.junctions[i];
      const auto& rhs = result.junctions[j];
      if (BoxesOverlap(lhs.incoming_box, rhs.incoming_box) ||
          (BoxHorizontalDistance(lhs.incoming_box, rhs.incoming_box) <=
               options.max_box_gap_xy &&
           BoxVerticalDistance(lhs.incoming_box, rhs.incoming_box) <=
               options.max_box_gap_z)) {
        disjoint_set.Union(lhs.junction_id, rhs.junction_id);
      }
    }
  }

  std::map<std::string, JunctionClusterGroup> groups_by_root;
  for (const auto& member : result.junctions) {
    const std::string root = disjoint_set.Find(member.junction_id);
    auto& group = groups_by_root[root];
    if (group.group_id.empty()) {
      group.group_id = root;
    }
    group.junction_ids.push_back(member.junction_id);
    group.incoming_road_ids.insert(member.incoming_road_ids.begin(),
                                   member.incoming_road_ids.end());
    group.connecting_road_ids.insert(member.connecting_road_ids.begin(),
                                     member.connecting_road_ids.end());
    MergeBoxInto(group.incoming_box, member.incoming_box);
    group.boundary_arms.insert(group.boundary_arms.end(),
                               member.incoming_arms.begin(),
                               member.incoming_arms.end());
  }

  for (auto& root_and_group : groups_by_root) {
    auto& group = root_and_group.second;
    std::sort(group.junction_ids.begin(), group.junction_ids.end());

    const std::set<std::string> group_junction_ids(group.junction_ids.begin(),
                                                   group.junction_ids.end());
    std::map<std::string, JunctionArmInfo> boundary_arms_by_road;
    for (const auto& arm : group.boundary_arms) {
      boundary_arms_by_road[arm.road_id] = arm;
    }

    for (const auto& junction_id : group.junction_ids) {
      const auto junction_it = map.id_to_junction.find(junction_id);
      if (junction_it == map.id_to_junction.end()) {
        continue;
      }

      for (const auto& id_and_connection :
           junction_it->second.id_to_connection) {
        const auto& connection = id_and_connection.second;
        const auto connecting_it =
            map.id_to_road.find(connection.connecting_road);
        if (connecting_it == map.id_to_road.end()) {
          continue;
        }

        const odr::RoadLink far_link =
            connection.contact_point ==
                    odr::JunctionConnection::ContactPoint_Start
                ? connecting_it->second.successor
                : connecting_it->second.predecessor;
        if (far_link.type != odr::RoadLink::Type_Road || far_link.id.empty()) {
          continue;
        }
        if (group.incoming_road_ids.count(far_link.id) > 0 ||
            group.connecting_road_ids.count(far_link.id) > 0) {
          continue;
        }

        const auto far_road_it = map.id_to_road.find(far_link.id);
        if (far_road_it == map.id_to_road.end()) {
          continue;
        }

        JunctionArmInfo exit_arm;
        exit_arm.road_id = far_link.id;
        if (TryGetExitArm(far_road_it->second, connection.connecting_road,
                          exit_arm)) {
          boundary_arms_by_road[exit_arm.road_id] = exit_arm;
          group.external_road_ids.insert(exit_arm.road_id);
        }
      }
    }

    group.boundary_arms.clear();
    std::vector<double> boundary_angles;
    for (const auto& road_and_arm : boundary_arms_by_road) {
      group.boundary_arms.push_back(road_and_arm.second);
      boundary_angles.push_back(road_and_arm.second.angle_rad);
    }
    group.semantic_type = ClassifyByAngles(boundary_angles, options);
  }

  for (auto& root_and_group : groups_by_root) {
    result.groups.push_back(std::move(root_and_group.second));
  }

  std::sort(
      result.groups.begin(), result.groups.end(),
      [](const JunctionClusterGroup& lhs, const JunctionClusterGroup& rhs) {
        if (lhs.junction_ids.size() != rhs.junction_ids.size()) {
          return lhs.junction_ids.size() > rhs.junction_ids.size();
        }
        return lhs.group_id < rhs.group_id;
      });

  for (std::size_t index = 0; index < result.groups.size(); ++index) {
    for (const auto& junction_id : result.groups[index].junction_ids) {
      result.junction_id_to_group_index[junction_id] = index;
    }
  }

  return result;
}
