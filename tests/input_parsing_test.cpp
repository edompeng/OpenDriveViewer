#include "src/utility/input_parsing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ============================================================
// CoordinateInputParser::ParseUserPoints
// ============================================================

TEST_CASE("ParseUserPoints - empty input returns empty vector",
          "[input-parsing]") {
  const auto result = CoordinateInputParser::ParseUserPoints("");
  REQUIRE(result.empty());
}

TEST_CASE("ParseUserPoints - single 2D point parsed correctly",
          "[input-parsing]") {
  const auto result =
      CoordinateInputParser::ParseUserPoints("116.3912, 39.9073");
  REQUIRE(result.size() == 1);
  CHECK(result[0].lon == Catch::Approx(116.3912));
  CHECK(result[0].lat == Catch::Approx(39.9073));
  CHECK(!result[0].alt.has_value());
}

TEST_CASE("ParseUserPoints - 2D point with parentheses parsed correctly",
          "[input-parsing]") {
  const auto result =
      CoordinateInputParser::ParseUserPoints("(116.3912, 39.9073)");
  REQUIRE(result.size() == 1);
  CHECK(result[0].lon == Catch::Approx(116.3912));
  CHECK(result[0].lat == Catch::Approx(39.9073));
}

TEST_CASE("ParseUserPoints - 3D point with altitude parsed correctly",
          "[input-parsing]") {
  const auto result =
      CoordinateInputParser::ParseUserPoints("116.3912, 39.9073, 50.5");
  REQUIRE(result.size() == 1);
  CHECK(result[0].lon == Catch::Approx(116.3912));
  CHECK(result[0].lat == Catch::Approx(39.9073));
  REQUIRE(result[0].alt.has_value());
  CHECK(*result[0].alt == Catch::Approx(50.5));
}

TEST_CASE("ParseUserPoints - multiple points separated by semicolons",
          "[input-parsing]") {
  const auto result = CoordinateInputParser::ParseUserPoints(
      "1.0, 2.0; 3.0, 4.0; 5.0, 6.0, 7.0");
  REQUIRE(result.size() == 3);
  CHECK(result[0].lon == Catch::Approx(1.0));
  CHECK(result[1].lon == Catch::Approx(3.0));
  CHECK(result[2].lon == Catch::Approx(5.0));
  CHECK(!result[0].alt.has_value());
  CHECK(!result[1].alt.has_value());
  REQUIRE(result[2].alt.has_value());
  CHECK(*result[2].alt == Catch::Approx(7.0));
}

TEST_CASE("ParseUserPoints - invalid text returns empty vector",
          "[input-parsing]") {
  const auto result =
      CoordinateInputParser::ParseUserPoints("not a coordinate");
  REQUIRE(result.empty());
}

TEST_CASE("ParseUserPoints - partial invalid is skipped", "[input-parsing]") {
  const auto result =
      CoordinateInputParser::ParseUserPoints("1.0, 2.0; bad; 3.0, 4.0");
  REQUIRE(result.size() == 2);
}

// ============================================================
// CoordinateInputParser::ParseJumpLocation
// ============================================================

TEST_CASE("ParseJumpLocation - empty input returns nullopt",
          "[input-parsing]") {
  CHECK(!CoordinateInputParser::ParseJumpLocation("").has_value());
}

TEST_CASE("ParseJumpLocation - comma-separated 2D coordinate",
          "[input-parsing]") {
  const auto result =
      CoordinateInputParser::ParseJumpLocation("116.3912,39.9073");
  REQUIRE(result.has_value());
  CHECK(result->lon == Catch::Approx(116.3912));
  CHECK(result->lat == Catch::Approx(39.9073));
  CHECK(result->alt == Catch::Approx(0.0));
}

TEST_CASE("ParseJumpLocation - space-separated 2D coordinate",
          "[input-parsing]") {
  const auto result =
      CoordinateInputParser::ParseJumpLocation("116.3912 39.9073");
  REQUIRE(result.has_value());
  CHECK(result->lon == Catch::Approx(116.3912));
  CHECK(result->lat == Catch::Approx(39.9073));
}

TEST_CASE("ParseJumpLocation - 3D coordinate with altitude",
          "[input-parsing]") {
  const auto result =
      CoordinateInputParser::ParseJumpLocation("116.3912, 39.9073, 100.0");
  REQUIRE(result.has_value());
  CHECK(result->lon == Catch::Approx(116.3912));
  CHECK(result->lat == Catch::Approx(39.9073));
  CHECK(result->alt == Catch::Approx(100.0));
}

TEST_CASE("ParseJumpLocation - more than 3 components returns nullopt",
          "[input-parsing]") {
  CHECK(!CoordinateInputParser::ParseJumpLocation("1,2,3,4").has_value());
}

TEST_CASE("ParseJumpLocation - invalid text returns nullopt",
          "[input-parsing]") {
  CHECK(!CoordinateInputParser::ParseJumpLocation("abc def").has_value());
}

// ============================================================
// CoordinateInputParser::ParseLaneKey
// ============================================================

TEST_CASE("ParseLaneKey - valid format road/s0/lane_id", "[input-parsing]") {
  const auto result =
      CoordinateInputParser::ParseLaneKey("road_42 / 15.5 / -1");
  REQUIRE(result.has_value());
  CHECK(result->road_id == "road_42");
  CHECK(result->lanesection_s0 == Catch::Approx(15.5));
  CHECK(result->lane_id == -1);
}

TEST_CASE("ParseLaneKey - wrong number of components returns nullopt",
          "[input-parsing]") {
  CHECK(!CoordinateInputParser::ParseLaneKey("road/15.5").has_value());
  CHECK(!CoordinateInputParser::ParseLaneKey("road/15.5/-1/extra").has_value());
}

TEST_CASE("ParseLaneKey - non-numeric s0 returns nullopt", "[input-parsing]") {
  CHECK(!CoordinateInputParser::ParseLaneKey("road/abc/-1").has_value());
}

TEST_CASE("ParseLaneKey - non-integer lane_id returns nullopt",
          "[input-parsing]") {
  CHECK(!CoordinateInputParser::ParseLaneKey("road/15.5/abc").has_value());
}
