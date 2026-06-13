#include "src/logic/map_topology_validator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "Lane.h"
#include "OpenDriveMap.h"
#include "Road.h"
#include "RoutingGraph.h"

namespace geoviewer::logic {

std::vector<TopologyIssue> MapTopologyValidator::Validate(
    const std::shared_ptr<odr::OpenDriveMap>& map,
    const odr::RoutingGraph* graph) {
  std::vector<TopologyIssue> issues;
  if (!map) return issues;

  // 1. Validate road-level topology
  for (const auto& [road_id, road] : map->id_to_road) {
    // Dangling Ends / Isolated roads
    const bool no_pred = road.predecessor.id.empty() ||
                         road.predecessor.type == odr::RoadLink::Type_None;
    const bool no_succ = road.successor.id.empty() ||
                         road.successor.type == odr::RoadLink::Type_None;

    // Get a coordinate for jump location (middle of road)
    double mid_s = road.length * 0.5;
    odr::Vec3D mid_pt = road.get_xyz(mid_s, 0.0, 0.0);

    if (no_pred && no_succ) {
      TopologyIssue issue;
      issue.severity = TopologySeverity::kError;
      issue.type = "Isolated Road";
      issue.road_id = road_id;
      issue.message =
          "Road has no predecessor and no successor (isolated road)";
      issue.x = mid_pt[0];
      issue.y = mid_pt[1];
      issue.z = mid_pt[2];
      issue.node_type = TreeNodeType::kRoad;
      issues.push_back(issue);
    } else {
      if (no_pred) {
        TopologyIssue issue;
        issue.severity = TopologySeverity::kWarning;
        issue.type = "Dangling Start";
        issue.road_id = road_id;
        issue.message = "Road has no predecessor (dangling start)";
        odr::Vec3D start_pt = road.get_xyz(0.0, 0.0, 0.0);
        issue.x = start_pt[0];
        issue.y = start_pt[1];
        issue.z = start_pt[2];
        issue.node_type = TreeNodeType::kRoad;
        issues.push_back(issue);
      }
      if (no_succ) {
        TopologyIssue issue;
        issue.severity = TopologySeverity::kWarning;
        issue.type = "Dangling End";
        issue.road_id = road_id;
        issue.message = "Road has no successor (dangling end)";
        odr::Vec3D end_pt = road.get_xyz(road.length, 0.0, 0.0);
        issue.x = end_pt[0];
        issue.y = end_pt[1];
        issue.z = end_pt[2];
        issue.node_type = TreeNodeType::kRoad;
        issues.push_back(issue);
      }
    }

    // Invalid Junction References
    if (road.predecessor.type == odr::RoadLink::Type_Junction &&
        !road.predecessor.id.empty()) {
      if (map->id_to_junction.find(road.predecessor.id) ==
          map->id_to_junction.end()) {
        TopologyIssue issue;
        issue.severity = TopologySeverity::kError;
        issue.type = "Invalid Junction Ref";
        issue.road_id = road_id;
        issue.message = "Predecessor junction ID " + road.predecessor.id +
                        " does not exist";
        issue.x = mid_pt[0];
        issue.y = mid_pt[1];
        issue.z = mid_pt[2];
        issue.node_type = TreeNodeType::kRoad;
        issues.push_back(issue);
      }
    }
    if (road.successor.type == odr::RoadLink::Type_Junction &&
        !road.successor.id.empty()) {
      if (map->id_to_junction.find(road.successor.id) ==
          map->id_to_junction.end()) {
        TopologyIssue issue;
        issue.severity = TopologySeverity::kError;
        issue.type = "Invalid Junction Ref";
        issue.road_id = road_id;
        issue.message =
            "Successor junction ID " + road.successor.id + " does not exist";
        issue.x = mid_pt[0];
        issue.y = mid_pt[1];
        issue.z = mid_pt[2];
        issue.node_type = TreeNodeType::kRoad;
        issues.push_back(issue);
      }
    }

    // Link Symmetry Checks
    if (road.successor.type == odr::RoadLink::Type_Road &&
        !road.successor.id.empty()) {
      const auto next_road_it = map->id_to_road.find(road.successor.id);
      if (next_road_it == map->id_to_road.end()) {
        TopologyIssue issue;
        issue.severity = TopologySeverity::kError;
        issue.type = "Broken Road Ref";
        issue.road_id = road_id;
        issue.message =
            "Successor road ID " + road.successor.id + " does not exist";
        issue.x = mid_pt[0];
        issue.y = mid_pt[1];
        issue.z = mid_pt[2];
        issue.node_type = TreeNodeType::kRoad;
        issues.push_back(issue);
      } else {
        const auto& next_road = next_road_it->second;
        const bool links_back =
            (next_road.predecessor.type == odr::RoadLink::Type_Road &&
             next_road.predecessor.id == road_id) ||
            (next_road.successor.type == odr::RoadLink::Type_Road &&
             next_road.successor.id == road_id);
        if (!links_back) {
          TopologyIssue issue;
          issue.severity = TopologySeverity::kWarning;
          issue.type = "Asymmetric Link";
          issue.road_id = road_id;
          issue.message = "Road successor " + road.successor.id +
                          " does not link back to this road";
          issue.x = mid_pt[0];
          issue.y = mid_pt[1];
          issue.z = mid_pt[2];
          issue.node_type = TreeNodeType::kRoad;
          issues.push_back(issue);
        }
      }
    }

    if (road.predecessor.type == odr::RoadLink::Type_Road &&
        !road.predecessor.id.empty()) {
      const auto prev_road_it = map->id_to_road.find(road.predecessor.id);
      if (prev_road_it == map->id_to_road.end()) {
        TopologyIssue issue;
        issue.severity = TopologySeverity::kError;
        issue.type = "Broken Road Ref";
        issue.road_id = road_id;
        issue.message =
            "Predecessor road ID " + road.predecessor.id + " does not exist";
        issue.x = mid_pt[0];
        issue.y = mid_pt[1];
        issue.z = mid_pt[2];
        issue.node_type = TreeNodeType::kRoad;
        issues.push_back(issue);
      } else {
        const auto& prev_road = prev_road_it->second;
        const bool links_back =
            (prev_road.predecessor.type == odr::RoadLink::Type_Road &&
             prev_road.predecessor.id == road_id) ||
            (prev_road.successor.type == odr::RoadLink::Type_Road &&
             prev_road.successor.id == road_id);
        if (!links_back) {
          TopologyIssue issue;
          issue.severity = TopologySeverity::kWarning;
          issue.type = "Asymmetric Link";
          issue.road_id = road_id;
          issue.message = "Road predecessor " + road.predecessor.id +
                          " does not link back to this road";
          issue.x = mid_pt[0];
          issue.y = mid_pt[1];
          issue.z = mid_pt[2];
          issue.node_type = TreeNodeType::kRoad;
          issues.push_back(issue);
        }
      }
    }

    // 2. Validate lane-level connectivity using RoutingGraph
    if (graph) {
      for (const auto& [s0, section] : road.s_to_lanesection) {
        for (const auto& [lane_id, lane] : section.id_to_lane) {
          if (lane_id == 0 || lane.type != "driving") continue;

          odr::LaneKey key(road_id, s0, lane_id);
          auto successors = graph->get_lane_successors(key);
          auto predecessors = graph->get_lane_predecessors(key);

          // Get coordinates for this lane (center of section)
          double s_center = s0 + (road.length - s0) * 0.5;
          auto next_section_it = road.s_to_lanesection.upper_bound(s0);
          if (next_section_it != road.s_to_lanesection.end()) {
            s_center = s0 + (next_section_it->first - s0) * 0.5;
          }
          odr::Vec3D lane_pt = road.get_xyz(s_center, 0.0, 0.0);

          // Broken Lane Successor (if road has successor but lane has no
          // successor)
          if (successors.empty() && !no_succ) {
            TopologyIssue issue;
            issue.severity = TopologySeverity::kError;
            issue.type = "Lane Successor Gap";
            issue.road_id = road_id;
            char buf[64];
            snprintf(buf, sizeof(buf), "%.2f:%d", s0, lane_id);
            issue.lane_id = buf;
            issue.message = std::string("Driving lane ") +
                            std::to_string(lane_id) +
                            " at s=" + std::to_string(s0) +
                            " has no successors, but road continues";
            issue.x = lane_pt[0];
            issue.y = lane_pt[1];
            issue.z = lane_pt[2];
            issue.node_type = TreeNodeType::kLane;
            issues.push_back(issue);
          }

          // Broken Lane Predecessor (if road has predecessor but lane has no
          // predecessor)
          if (predecessors.empty() && !no_pred) {
            TopologyIssue issue;
            issue.severity = TopologySeverity::kError;
            issue.type = "Lane Predecessor Gap";
            issue.road_id = road_id;
            char buf[64];
            snprintf(buf, sizeof(buf), "%.2f:%d", s0, lane_id);
            issue.lane_id = buf;
            issue.message = std::string("Driving lane ") +
                            std::to_string(lane_id) +
                            " at s=" + std::to_string(s0) +
                            " has no predecessors, but road has predecessor";
            issue.x = lane_pt[0];
            issue.y = lane_pt[1];
            issue.z = lane_pt[2];
            issue.node_type = TreeNodeType::kLane;
            issues.push_back(issue);
          }

          // Junction connecting road lanes disconnections
          const bool is_in_junction =
              !road.junction.empty() && road.junction != "-1";
          if (is_in_junction && (successors.empty() || predecessors.empty())) {
            TopologyIssue issue;
            issue.severity = TopologySeverity::kError;
            issue.type = "Junction Lane Break";
            issue.road_id = road_id;
            char buf[64];
            snprintf(buf, sizeof(buf), "%.2f:%d", s0, lane_id);
            issue.lane_id = buf;
            issue.message = std::string("Driving lane ") +
                            std::to_string(lane_id) +
                            " inside junction connecting road " + road_id +
                            " is disconnected at one end";
            issue.x = lane_pt[0];
            issue.y = lane_pt[1];
            issue.z = lane_pt[2];
            issue.node_type = TreeNodeType::kLane;
            issues.push_back(issue);
          }
        }
      }
    }
  }

  return issues;
}

}  // namespace geoviewer::logic
