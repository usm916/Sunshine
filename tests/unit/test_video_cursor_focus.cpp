/**
 * @file tests/unit/test_video_cursor_focus.cpp
 * @brief Test cursor focus mapping across rotated and offset displays.
 */
#include "../tests_common.h"

// standard includes
#include <limits>
#include <tuple>
#include <vector>

// local includes
#include <src/video_cursor_focus.h>
#include <src/video_cursor_focus_background.h>

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

TEST(CursorFocusTest, RectangleUsesIndependentDimensionsAndRotatesItsExtents) {
  settings_t settings {true, 96, 4};
  settings.shape = "rectangle";
  settings.width = 640;
  settings.height = 240;
  const auto normal = make_constants(settings, {0, 0, 100, 200, 1000, 2000, 0}, 50, 100);
  EXPECT_EQ(normal.rectangle, 1);
  EXPECT_EQ(normal.half_size, (std::array<float, 2> {320, 120}));
  EXPECT_EQ(normal.feather, 30);
  const auto rotated = make_constants(settings, {0, 0, 100, 200, 1000, 2000, 1}, 50, 100);
  EXPECT_EQ(rotated.half_size, (std::array<float, 2> {120, 320}));
  EXPECT_EQ(rotated.feather, 30);
  settings.width = -1;
  settings.height = 99999;
  const auto clamped = make_constants(settings, {0, 0, 100, 100, 100, 100, 0}, 50, 50);
  EXPECT_EQ(clamped.half_size, (std::array<float, 2> {16, 4096}));
  settings.shape = "unknown";
  EXPECT_EQ(make_constants(settings, {0, 0, 100, 100, 100, 100, 0}, 50, 50).rectangle, 0);
}

TEST(CursorFocusBackgroundTest, BoundsCoverFocusAndTransitionWithoutLeavingTexture) {
  settings_t settings {true, 32, 4};
  const geometry_t geometry {0, 0, 120, 100, 120, 100, 0};
  EXPECT_EQ(focus_bounds(make_constants(settings, geometry, 60, 50), 120, 100), (pixel_rect_t {20, 10, 101, 91}));
  EXPECT_EQ(focus_bounds(make_constants(settings, geometry, 0, 0), 120, 100), (pixel_rect_t {0, 0, 41, 41}));
  EXPECT_EQ(focus_bounds({}, 120, 100), pixel_rect_t {});
  EXPECT_EQ(focus_bounds(make_constants(settings, geometry, 60, 50), 0, 100), pixel_rect_t {});
  EXPECT_EQ(focus_bounds(make_constants(settings, geometry, 60, 50), 120, 0), pixel_rect_t {});
  settings.shape = "rectangle";
  settings.width = 64;
  settings.height = 32;
  EXPECT_EQ(focus_bounds(make_constants(settings, geometry, 60, 50), 120, 100), (pixel_rect_t {24, 30, 97, 71}));
}

TEST(CursorFocusBackgroundTest, OneSecondLoopVisitsEveryPixelOnceAtThirtyFps) {
  background_scheduler_t scheduler;
  EXPECT_TRUE(scheduler.advance(127, 97, 1000, 0).full);
  std::vector<int> visits(127 * 97);
  std::size_t tile_count = 0;
  for (int frame = 1; frame <= 30; ++frame) {
    const auto work = scheduler.advance(127, 97, 1000, frame * 1000000000LL / 30);
    EXPECT_FALSE(work.full);
    tile_count += work.count;
    for (std::size_t tile = 0; tile < work.count; ++tile) {
      const auto &rect = work.regions[tile];
      for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left; x < rect.right; ++x) {
          ++visits[y * 127 + x];
        }
      }
    }
  }
  EXPECT_EQ(tile_count, 30);
  EXPECT_TRUE(std::all_of(visits.begin(), visits.end(), [](int count) {
    return count == 1;
  }));
}

TEST(CursorFocusBackgroundTest, SameLoopUsesThreeTilesPerFrameAtTenFps) {
  background_scheduler_t scheduler;
  scheduler.advance(120, 100, 1000, 0);
  for (int frame = 1; frame <= 10; ++frame) {
    EXPECT_EQ(scheduler.advance(120, 100, 1000, frame * 100000000LL).count, 3);
  }
  EXPECT_EQ(scheduler.advance(120, 100, 1000, 1100000000).regions[0], (pixel_rect_t {0, 0, 20, 20}));
}

TEST(CursorFocusBackgroundTest, LongerPeriodAccumulatesFractionalTileWork) {
  background_scheduler_t scheduler;
  scheduler.advance(120, 100, 60000, 0);
  EXPECT_EQ(scheduler.advance(120, 100, 60000, 1000000000).count, 0);
  EXPECT_EQ(scheduler.advance(120, 100, 60000, 2000000000).count, 1);
}

TEST(CursorFocusBackgroundTest, FrozenAndContinuousModesHaveDistinctBehavior) {
  background_scheduler_t scheduler;
  EXPECT_TRUE(scheduler.advance(120, 100, -1, 0).full);
  EXPECT_EQ(scheduler.advance(120, 100, -1, 1000000000).count, 0);
  EXPECT_FALSE(scheduler.advance(120, 100, -1, 2000000000).full);
  EXPECT_TRUE(scheduler.advance(120, 100, 0, 2000000000).full);
  EXPECT_TRUE(scheduler.advance(120, 100, 0, 2100000000).full);
  EXPECT_TRUE(scheduler.advance(120, 100, -999, 2200000000).full);
  EXPECT_EQ(scheduler.advance(120, 100, -999, 2300000000).count, 0);
  EXPECT_TRUE(scheduler.advance(120, 100, 999999, 2400000000).full);
  EXPECT_EQ(scheduler.advance(120, 100, 999999, 3400000000).count, 0);
}

TEST(CursorFocusBackgroundTest, StallDoesNotCatchUpWithOneFrameWideRefresh) {
  background_scheduler_t scheduler;
  scheduler.advance(120, 100, 1000, 0);
  const auto after_stall = scheduler.advance(120, 100, 1000, 10000000000LL);
  EXPECT_FALSE(after_stall.full);
  EXPECT_EQ(after_stall.count, 8);
  EXPECT_EQ(scheduler.advance(120, 100, 1000, 10000000000LL).count, 0);
  EXPECT_EQ(scheduler.advance(120, 100, 1000, 10100000000LL).regions[0], (pixel_rect_t {40, 20, 60, 40}));
  EXPECT_EQ(scheduler.advance(120, 100, 1000, -1).count, 0);
}

TEST(CursorFocusBackgroundTest, DimensionChangesAndInvalidDimensionsReinitialize) {
  background_scheduler_t scheduler;
  scheduler.advance(120, 100, 1000, 0);
  EXPECT_TRUE(scheduler.advance(121, 100, 1000, 100).full);
  EXPECT_TRUE(scheduler.advance(121, 101, 1000, 200).full);
  EXPECT_FALSE(scheduler.advance(0, 100, 1000, 300).full);
  EXPECT_TRUE(scheduler.advance(1, 1, 1000, 400).full);
  EXPECT_EQ(scheduler.advance(1, 1, 1000, 1000000400).regions[0], (pixel_rect_t {0, 0, 1, 1}));
  EXPECT_FALSE(scheduler.advance(1, 0, 1000, 1000000500).full);
  EXPECT_TRUE(scheduler.advance(120, 100, 1000, 1000000600).full);
}

TEST(CursorFocusBackgroundTest, BoundsRejectInvalidOutputMappingAndClipExtremeCenters) {
  const geometry_t geometry {0, 0, 120, 100, 120, 100, 0};
  const auto valid = make_constants({true, 32, 4}, geometry, 60, 50);
  for (int field = 0; field < 6; ++field) {
    auto constants = valid;
    switch (field) {
      case 0:
        constants.output_size[0] = 0;
        break;
      case 1:
        constants.output_size[1] = -1;
        break;
      case 2:
        constants.output_size[0] = std::numeric_limits<float>::quiet_NaN();
        break;
      case 3:
        constants.output_size[1] = std::numeric_limits<float>::infinity();
        break;
      case 4:
        constants.center[0] = std::numeric_limits<float>::infinity();
        break;
      case 5:
        constants.center[1] = std::numeric_limits<float>::quiet_NaN();
        break;
    }
    EXPECT_EQ(focus_bounds(constants, 120, 100), pixel_rect_t {});
  }
  auto extreme = valid;
  extreme.center = {std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
  EXPECT_EQ(focus_bounds(extreme, 120, 100), (pixel_rect_t {120, 0, 120, 0}));
}
