#pragma once

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef GEOVIEWER_SOURCE_DIR
#  define GEOVIEWER_SOURCE_DIR "."
#endif

namespace geoviewer::test {

inline std::string FindTestData(const std::string& file_name) {
  std::vector<std::string> search_paths = {
      std::string(GEOVIEWER_SOURCE_DIR) + "/" + file_name,
      std::string(GEOVIEWER_SOURCE_DIR) + "/_main/" + file_name,
      std::string(GEOVIEWER_SOURCE_DIR) + "/geoviewer/" + file_name,
      file_name,
      "../" + file_name};

  if (const char* test_srcdir = std::getenv("TEST_SRCDIR")) {
    std::string src_dir = test_srcdir;
    search_paths.push_back(src_dir + "/geoviewer/" + file_name);
    search_paths.push_back(src_dir + "/_main/" + file_name);
    if (const char* test_workspace = std::getenv("TEST_WORKSPACE")) {
      search_paths.push_back(src_dir + "/" + test_workspace + "/" + file_name);
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
        if (runfile_path.size() >= file_name.size() &&
            runfile_path.compare(runfile_path.size() - file_name.size(),
                                 file_name.size(), file_name) == 0) {
          std::ifstream f(abs_path);
          if (f.good()) return abs_path;
        }
      }
    }
  }

  throw std::runtime_error("Could not find test data file: " + file_name);
}

}  // namespace geoviewer::test
