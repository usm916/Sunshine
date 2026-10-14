/**
 * @file tests/unit/test_video_cursor_focus.cpp
 * @brief Test cursor focus mapping across rotated and offset displays.
 */
#include "../tests_common.h"

// standard includes
#include <limits>
#include <tuple>

// local includes
#include <src/video_cursor_focus.h>

using namespace video::cursor_focus;

TEST(CursorFocusTest, DisabledOrUnknownCursorPassesThrough) {
  const geometry_t geometry {0, 0, 100, 100, 100, 100, 0};
  EXPECT_EQ(make_constants({}, geometry, 50, 50).enabled, 0);
  EXPECT_EQ(make_constants({true, 160, 4}, geometry, 50, 50, false).enabled, 0);
}

TEST(CursorFocusTest, MapsNegativeDesktopCoordinatesAndVisibleViewport) {
  const auto result = make_constants({true, 96, 4}, {-1920, -100, 1920, 1080, 1280, 720, 0}, -960, 440);
  EXPECT_EQ(result.enabled, 1);
  EXPECT_FLOAT_EQ(result.center[0], 960.5f / 1920);
  EXPECT_FLOAT_EQ(result.center[1], 540.5f / 1080);
  EXPECT_EQ(result.output_size, (std::array<float, 2> {1280, 720}));
  EXPECT_EQ(result.radius, 96);
  EXPECT_EQ(result.feather, 24);
  EXPECT_EQ(result.background_scale, 4);
}

TEST(CursorFocusTest, RejectsOffDisplayAndExtremeCursorPositions) {
  const geometry_t geometry {-100, -50, 100, 50, 200, 100, 0};
  for (const auto &[x, y] : std::array<std::pair<std::int64_t, std::int64_t>, 6> {
         {{-101, -25}, {0, -25}, {-50, -51}, {-50, 0}, {std::numeric_limits<std::int64_t>::min(), 0}, {std::numeric_limits<std::int64_t>::max(), 0}}
       }) {
    EXPECT_EQ(make_constants({true, 96, 4}, geometry, x, y).enabled, 0);
  }
  EXPECT_EQ(make_constants({true, 96, 4}, geometry, -100, -50).enabled, 1);
  EXPECT_EQ(make_constants({true, 96, 4}, geometry, -1, -1).enabled, 1);
}

TEST(CursorFocusTest, RejectsInvalidGeometry) {
  const geometry_t valid {0, 0, 100, 100, 100, 100, 0};
  for (int field = 0; field < 10; ++field) {
    auto geometry = valid;
    switch (field) {
      case 0:
        geometry.width = 0;
        break;
      case 1:
        geometry.height = -1;
        break;
      case 2:
        geometry.output_width = 0;
        break;
      case 3:
        geometry.output_height = -1;
        break;
      case 4:
        geometry.output_width = std::numeric_limits<float>::quiet_NaN();
        break;
      case 5:
        geometry.output_height = std::numeric_limits<float>::infinity();
        break;
      case 6:
        geometry.rotation = -1;
        break;
      case 7:
        geometry.rotation = 4;
        break;
      case 8:
        geometry.output_width = std::numeric_limits<float>::infinity();
        break;
      case 9:
        geometry.output_height = std::numeric_limits<float>::quiet_NaN();
        break;
    }
    EXPECT_EQ(make_constants({true, 96, 4}, geometry, 50, 50).enabled, 0) << field;
  }
}

TEST(CursorFocusTest, ClampsProgrammaticSettingsToSafeLimits) {
  const geometry_t geometry {0, 0, 100, 100, 100, 100, 0};
  const auto low = make_constants({true, -1, 0}, geometry, 50, 50);
  EXPECT_EQ(low.radius, 32);
  EXPECT_EQ(low.background_scale, 2);
  const auto high = make_constants({true, 9999, 9999}, geometry, 50, 50);
  EXPECT_EQ(high.radius, 1024);
  EXPECT_EQ(high.feather, 256);
  EXPECT_EQ(high.background_scale, 16);
}

/**
 * @brief Rotation and expected capture-texture center and output dimensions.
 */
using FocusRotationParam = std::tuple<int, float, float, float, float>;

/**
 * @brief Exercise the same inverse rotation used by the color-conversion vertex shaders.
 */
struct CursorFocusRotationTest: testing::TestWithParam<FocusRotationParam> {};

TEST_P(CursorFocusRotationTest, FollowsInverseTextureRotation) {
  const auto &[rotation, x, y, width, height] = GetParam();
  const auto result = make_constants({true, 96, 4}, {0, 0, 100, 200, 300, 600, rotation}, 24, 149);
  EXPECT_FLOAT_EQ(result.center[0], x);
  EXPECT_FLOAT_EQ(result.center[1], y);
  EXPECT_FLOAT_EQ(result.output_size[0], width);
  EXPECT_FLOAT_EQ(result.output_size[1], height);
}

INSTANTIATE_TEST_SUITE_P(AllRotations, CursorFocusRotationTest, testing::Values(FocusRotationParam {0, 0.245f, 0.7475f, 300, 600}, FocusRotationParam {1, 0.7475f, 0.755f, 600, 300}, FocusRotationParam {2, 0.755f, 0.2525f, 300, 600}, FocusRotationParam {3, 0.2525f, 0.245f, 600, 300}));
