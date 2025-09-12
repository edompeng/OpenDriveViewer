#include "src/utility/junction_grouping.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "third_party/Catch2/src/catch2/catch_approx.hpp"
#include "third_party/Catch2/src/catch2/catch_test_macros.hpp"
#include "third_party/libOpenDRIVE/include/OpenDriveMap.h"

#include <fstream>

namespace {

constexpr double kPi = 3.14159265358979323846;

double DegToRad(double degrees) { return degrees * kPi / 180.0; }

std::vector<std::string> FindGroup(const JunctionClusterResult& result,
                                   const std::string& junction_id) {
  const auto it = result.junction_id_to_group_index.find(junction_id);
  REQUIRE(it != result.junction_id_to_group_index.end());
  return result.groups.at(it->second).junction_ids;
}

std::string FindTestData(const std::string& filename) {
  std::vector<std::string> search_paths = {
      std::string(GEOVIEWER_SOURCE_DIR) + "/" + filename,
      std::string(GEOVIEWER_SOURCE_DIR) + "/_main/" + filename,
      std::string(GEOVIEWER_SOURCE_DIR) + "/geoviewer/" + filename,
      filename,
      "../" + filename
  };

  if (const char* test_srcdir = std::getenv("TEST_SRCDIR")) {
      std::string srcdir = test_srcdir;
      search_paths.push_back(srcdir + "/geoviewer/" + filename);
      search_paths.push_back(srcdir + "/_main/" + filename);
      if (const char* test_workspace = std::getenv("TEST_WORKSPACE")) {
          search_paths.push_back(srcdir + "/" + test_workspace + "/" + filename);
      }
  }
  
  for (const auto& path : search_paths) {
      std::ifstream f(path);
      if (f.good()) {
          return path;
      }
  }
  
  if (const char* manifest_path = std::getenv("RUNFILES_MANIFEST_FILE")) {
      std::ifstream manifest(manifest_path);
      std::string line;
      while (std::getline(manifest, line)) {
          if (!line.empty() && line.back() == '\r') {
              line.pop_back();
          }
          auto space_pos = line.find(' ');
          if (space_pos != std::string::npos) {
              std::string runfile_path = line.substr(0, space_pos);
              std::string abs_path = line.substr(space_pos + 1);
              if (runfile_path.size() >= filename.size() && 
                  runfile_path.compare(runfile_path.size() - filename.size(), filename.size(), filename) == 0) {
                  std::ifstream f(abs_path);
                  if (f.good()) return abs_path;
              }
          }
      }
  }
  
  throw std::runtime_error("Could not find test data file: " + filename);
}

}  // namespace

TEST_CASE("Split junctions are merged into physical groups",
          "[junction-grouping]") {
  odr::OpenDriveMap map(FindTestData("data/test2.xodr"));
  const JunctionClusterResult result = JunctionClusterUtil::Analyze(map);

  REQUIRE(result.junctions.size() == map.id_to_junction.size());
  REQUIRE(result.groups.size() < result.junctions.size());

  CHECK(FindGroup(result, "13901") ==
        std::vector<std::string>{"13901", "13982", "13988"});
  CHECK(FindGroup(result, "13908") ==
        std::vector<std::string>{"13908", "14005"});
  CHECK(FindGroup(result, "13873") == std::vector<std::string>{"13873"});

  const auto group_it = result.junction_id_to_group_index.find("13901");
  REQUIRE(group_it != result.junction_id_to_group_index.end());
  CHECK(result.groups.at(group_it->second).semantic_type ==
        JunctionSemanticType::kCrossroad);
}

TEST_CASE("Semantic labels are derived from arm angles",
          "[junction-grouping]") {
  CHECK(JunctionClusterUtil::ClassifyByAngles(
            {0.0, kPi / 2.0, kPi, 3.0 * kPi / 2.0}) ==
        JunctionSemanticType::kCrossroad);
  CHECK(JunctionClusterUtil::ClassifyByAngles({0.0, kPi / 2.0, kPi}) ==
        JunctionSemanticType::kTIntersection);
  CHECK(JunctionClusterUtil::ClassifyByAngles(
            {0.0, 2.0 * kPi / 3.0, 4.0 * kPi / 3.0}) ==
        JunctionSemanticType::kThreeWayIntersection);
  CHECK(JunctionClusterUtil::ClassifyByAngles({0.0, kPi}) ==
        JunctionSemanticType::kUTurnOnly);
  CHECK(JunctionClusterUtil::ClassifyByAngles(
            {0.0, DegToRad(40.0), DegToRad(170.0), DegToRad(260.0)}) ==
        JunctionSemanticType::kIrregularIntersection);
}

TEST_CASE("Box gap uses horizontal and vertical thresholds",
          "[junction-grouping]") {
  JunctionBox3D a;
  a.valid = true;
  a.min = {0.0, 0.0, 0.0};
  a.max = {1.0, 1.0, 1.0};

  JunctionBox3D b;
  b.valid = true;
  b.min = {2.5, 0.0, 4.5};
  b.max = {3.5, 1.0, 5.5};

  CHECK(JunctionClusterUtil::BoxHorizontalDistance(a, b) == Catch::Approx(1.5));
  CHECK(JunctionClusterUtil::BoxVerticalDistance(a, b) == Catch::Approx(3.5));
}
