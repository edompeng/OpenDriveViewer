#include "src/logic/input_parsing.h"
#include <gtest/gtest.h>

// ============================================================
// CoordinateInputParser::ParseUserPoints
// ============================================================

TEST(CoordinateInputParserTest, ParseUserPoints_EmptyInputReturnsEmptyVector) {
  const auto result = CoordinateInputParser::ParseUserPoints("");
  EXPECT_TRUE(result.empty());
}

TEST(CoordinateInputParserTest, ParseUserPoints_Single2DPointParsedCorrectly) {
  const auto result =
      CoordinateInputParser::ParseUserPoints("116.3912, 39.9073");
  ASSERT_EQ(result.size(), 1);
  EXPECT_FLOAT_EQ(result[0].x, 116.3912f);
  EXPECT_FLOAT_EQ(result[0].y, 39.9073f);
  EXPECT_FALSE(result[0].z.has_value());
}

TEST(CoordinateInputParserTest, ParseUserPoints_2DPointWithParenthesesParsedCorrectly) {
  const auto result =
      CoordinateInputParser::ParseUserPoints("(116.3912, 39.9073)");
  ASSERT_EQ(result.size(), 1);
  EXPECT_FLOAT_EQ(result[0].x, 116.3912f);
  EXPECT_FLOAT_EQ(result[0].y, 39.9073f);
}

TEST(CoordinateInputParserTest, ParseUserPoints_3DPointWithAltitudeParsedCorrectly) {
  const auto result =
      CoordinateInputParser::ParseUserPoints("116.3912, 39.9073, 50.5");
  ASSERT_EQ(result.size(), 1);
  EXPECT_FLOAT_EQ(result[0].x, 116.3912f);
  EXPECT_FLOAT_EQ(result[0].y, 39.9073f);
  ASSERT_TRUE(result[0].z.has_value());
  EXPECT_FLOAT_EQ(*result[0].z, 50.5f);
}

TEST(CoordinateInputParserTest, ParseUserPoints_MultiplePointsSeparatedBySemicolons) {
  const auto result = CoordinateInputParser::ParseUserPoints(
      "1.0, 2.0; 3.0, 4.0; 5.0, 6.0, 7.0");
  ASSERT_EQ(result.size(), 3);
  EXPECT_FLOAT_EQ(result[0].x, 1.0f);
  EXPECT_FLOAT_EQ(result[1].x, 3.0f);
  EXPECT_FLOAT_EQ(result[2].x, 5.0f);
  EXPECT_FALSE(result[0].z.has_value());
  EXPECT_FALSE(result[1].z.has_value());
  ASSERT_TRUE(result[2].z.has_value());
  EXPECT_FLOAT_EQ(*result[2].z, 7.0f);
}

TEST(CoordinateInputParserTest, ParseUserPoints_InvalidTextReturnsEmptyVector) {
  const auto result =
      CoordinateInputParser::ParseUserPoints("not a coordinate");
  EXPECT_TRUE(result.empty());
}

TEST(CoordinateInputParserTest, ParseUserPoints_PartialInvalidIsSkipped) {
  const auto result =
      CoordinateInputParser::ParseUserPoints("1.0, 2.0; bad; 3.0, 4.0");
  EXPECT_EQ(result.size(), 2);
}

// ============================================================
// CoordinateInputParser::ParseJumpLocation
// ============================================================

TEST(CoordinateInputParserTest, ParseJumpLocation_EmptyInputReturnsNullopt) {
  EXPECT_FALSE(CoordinateInputParser::ParseJumpLocation("").has_value());
}

TEST(CoordinateInputParserTest, ParseJumpLocation_CommaSeparated2DCoordinate) {
  const auto result =
      CoordinateInputParser::ParseJumpLocation("116.3912,39.9073");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(result->x, 116.3912f);
  EXPECT_FLOAT_EQ(result->y, 39.9073f);
  EXPECT_FLOAT_EQ(result->z, 0.0f);
}

TEST(CoordinateInputParserTest, ParseJumpLocation_SpaceSeparated2DCoordinate) {
  const auto result =
      CoordinateInputParser::ParseJumpLocation("116.3912 39.9073");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(result->x, 116.3912f);
  EXPECT_FLOAT_EQ(result->y, 39.9073f);
}

TEST(CoordinateInputParserTest, ParseJumpLocation_3DCoordinateWithAltitude) {
  const auto result =
      CoordinateInputParser::ParseJumpLocation("116.3912, 39.9073, 100.0");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(result->x, 116.3912f);
  EXPECT_FLOAT_EQ(result->y, 39.9073f);
  EXPECT_FLOAT_EQ(result->z, 100.0f);
}

TEST(CoordinateInputParserTest, ParseJumpLocation_MoreThan3ComponentsReturnsNullopt) {
  EXPECT_FALSE(CoordinateInputParser::ParseJumpLocation("1,2,3,4").has_value());
}

TEST(CoordinateInputParserTest, ParseJumpLocation_InvalidTextReturnsNullopt) {
  EXPECT_FALSE(CoordinateInputParser::ParseJumpLocation("abc def").has_value());
}

// ============================================================
// CoordinateInputParser::ParseLaneKey
// ============================================================

TEST(CoordinateInputParserTest, ParseLaneKey_ValidFormat) {
  const auto result =
      CoordinateInputParser::ParseLaneKey("road_42 / 15.5 / -1");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->road_id, "road_42");
  EXPECT_FLOAT_EQ(result->lanesection_s0, 15.5f);
  EXPECT_EQ(result->lane_id, -1);
}

TEST(CoordinateInputParserTest, ParseLaneKey_WrongNumberOfComponentsReturnsNullopt) {
  EXPECT_FALSE(CoordinateInputParser::ParseLaneKey("road/15.5").has_value());
  EXPECT_FALSE(CoordinateInputParser::ParseLaneKey("road/15.5/-1/extra").has_value());
}

TEST(CoordinateInputParserTest, ParseLaneKey_NonNumericS0ReturnsNullopt) {
  EXPECT_FALSE(CoordinateInputParser::ParseLaneKey("road/abc/-1").has_value());
}

TEST(CoordinateInputParserTest, ParseLaneKey_NonIntegerLaneIdReturnsNullopt) {
  EXPECT_FALSE(CoordinateInputParser::ParseLaneKey("road/15.5/abc").has_value());
}
