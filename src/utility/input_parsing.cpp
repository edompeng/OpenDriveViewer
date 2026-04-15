#include "src/utility/input_parsing.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

std::string Trim(const std::string& input) {
  std::size_t begin = 0;
  while (begin < input.size() &&
         std::isspace(static_cast<unsigned char>(input[begin]))) {
    ++begin;
  }
  std::size_t end = input.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  return input.substr(begin, end - begin);
}

std::vector<std::string> Split(const std::string& input, char delim) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= input.size()) {
    const std::size_t end = input.find(delim, start);
    const std::string piece = Trim(input.substr(start, end - start));
    if (piece.empty() == false) {
      parts.push_back(piece);
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

std::vector<std::string> SplitBySpace(const std::string& input) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : input) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (current.empty() == false) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (current.empty() == false) {
    parts.push_back(current);
  }
  return parts;
}

bool ParseDouble(const std::string& text, double* out) {
  if (out == nullptr) {
    return false;
  }
  char* end = nullptr;
  *out = std::strtod(text.c_str(), &end);
  return end != nullptr && *end == '\0';
}

bool ParseInt(const std::string& text, int* out) {
  if (out == nullptr) {
    return false;
  }
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  *out = static_cast<int>(value);
  return true;
}

}  // namespace

std::vector<ParsedUserPoint> CoordinateInputParser::ParseUserPoints(
    const std::string& text) {
  std::vector<ParsedUserPoint> points;
  for (std::string point : Split(text, ';')) {
    point = Trim(point);
    if (point.empty()) {
      continue;
    }
    if (point.front() == '(') {
      point.erase(point.begin());
    }
    if (point.empty() == false && point.back() == ')') {
      point.pop_back();
    }

    const auto coords = Split(point, ',');
    if (coords.size() == 2) {
      ParsedUserPoint parsed;
      if (ParseDouble(coords[0], &parsed.x) &&
          ParseDouble(coords[1], &parsed.y)) {
        points.push_back(parsed);
      }
      continue;
    }

    if (coords.size() == 3) {
      ParsedUserPoint parsed;
      double z = 0.0;
      if (ParseDouble(coords[0], &parsed.x) &&
          ParseDouble(coords[1], &parsed.y) && ParseDouble(coords[2], &z)) {
        parsed.z = z;
        points.push_back(parsed);
      }
    }

  }

  return points;
}

std::optional<ParsedJumpLocation> CoordinateInputParser::ParseJumpLocation(
    const std::string& text) {
  const std::string trimmed = Trim(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> coords;
  if (trimmed.find(',') != std::string::npos) {
    coords = Split(trimmed, ',');
  } else {
    coords = SplitBySpace(trimmed);
  }

  if (coords.size() < 2 || coords.size() > 3) {
    return std::nullopt;
  }

  ParsedJumpLocation parsed;
  if (ParseDouble(coords[0], &parsed.x) == false ||
      ParseDouble(coords[1], &parsed.y) == false) {
    return std::nullopt;
  }
  if (coords.size() == 3 && ParseDouble(coords[2], &parsed.z) == false) {
    return std::nullopt;
  }


  return parsed;
}

std::optional<odr::LaneKey> CoordinateInputParser::ParseLaneKey(
    const std::string& text) {
  const auto parts = Split(Trim(text), '/');
  if (parts.size() != 3) {
    return std::nullopt;
  }

  double s0 = 0.0;
  int lane_id = 0;
  if (ParseDouble(parts[1], &s0) == false ||
      ParseInt(parts[2], &lane_id) == false) {
    return std::nullopt;
  }
  return odr::LaneKey(parts[0], s0, lane_id);
}
