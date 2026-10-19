/**
 * @file tests/unit/test_video_focus_anchor.cpp
 * @brief Caret/mouse arbitration, movement jitter, stale lookups and display fallback.
 */
#include "../tests_common.h"
#include "src/video_cursor_focus.h"
#include "src/video_focus_anchor.h"

#include <limits>
using namespace std::chrono_literals;
namespace anchor = video::focus_anchor;

TEST(FocusAnchorActivity, TypingWinsUntilCumulativeMovementReachesTheThreshold) {
  anchor::activity_t input(6);
  EXPECT_FALSE(input.prefers_caret(1));
  input.mouse_move({100, 100});
  input.keyboard(1);
  EXPECT_TRUE(input.prefers_caret(1));
  EXPECT_FALSE(input.prefers_caret(0));
  EXPECT_FALSE(input.prefers_caret(2));
  const auto revision = input.revision();
  input.keyboard(1);
  EXPECT_EQ(input.revision(), revision);
  for (int x = 101; x <= 105; ++x) {
    input.mouse_move({x, 100});
    EXPECT_TRUE(input.prefers_caret(1));
  }
  input.mouse_move({106, 100});
  EXPECT_FALSE(input.prefers_caret(1));
  EXPECT_GT(input.revision(), revision);
}

TEST(FocusAnchorActivity, DiagonalMovementClicksAndNewForegroundHaveExplicitPrecedence) {
  anchor::activity_t input;
  input.mouse_move({0, 0});
  input.keyboard(1);
  input.mouse_move({4, 4});
  EXPECT_TRUE(input.prefers_caret(1));
  input.mouse_move({5, 4});
  EXPECT_FALSE(input.prefers_caret(1));
  input.keyboard(1);
  input.mouse_action({5, 4});
  EXPECT_FALSE(input.prefers_caret(1));
  input.keyboard(2);
  EXPECT_TRUE(input.prefers_caret(2));
  EXPECT_FALSE(input.prefers_caret(1));
  input.keyboard(0);
  EXPECT_TRUE(input.prefers_caret(2));
  input.mouse_move({-1, 4});
  EXPECT_FALSE(input.prefers_caret(2));
}

TEST(FocusAnchorActivity, BoundsThresholdAndHandlesUnknownOrExtremeInitialPositions) {
  anchor::activity_t input(-1);
  input.keyboard(1);
  input.mouse_move({0, 0});
  EXPECT_TRUE(input.prefers_caret(1));
  input.mouse_move({0, -1});
  EXPECT_FALSE(input.prefers_caret(1));
  anchor::activity_t large(1000);
  large.mouse_move({0, 0});
  large.keyboard(1);
  large.mouse_move({0, 99});
  EXPECT_TRUE(large.prefers_caret(1));
  large.mouse_move({0, 100});
  EXPECT_FALSE(large.prefers_caret(1));
  large.keyboard(1);
  large.mouse_move({std::numeric_limits<int>::min(), std::numeric_limits<int>::max()});
  EXPECT_FALSE(large.prefers_caret(1));
}

TEST(FocusAnchorCache, RejectsOldWindowsEpochsMissingCaretsAndDelayedQueries) {
  anchor::caret_cache_t cache;
  const anchor::caret_cache_t::clock_t::time_point now {};
  EXPECT_FALSE(cache.read(1, 1, now));
  cache.publish(anchor::point_t {10, 20}, 1, 2, now, now + 10ms);
  EXPECT_EQ(cache.read(1, 2, now + 100ms), (anchor::point_t {10, 20}));
  EXPECT_FALSE(cache.read(0, 2, now + 100ms));
  EXPECT_FALSE(cache.read(2, 2, now + 100ms));
  EXPECT_FALSE(cache.read(1, 3, now + 100ms));
  EXPECT_FALSE(cache.read(1, 2, now));
  EXPECT_FALSE(cache.read(1, 2, now + 311ms));
  cache.reset();
  EXPECT_FALSE(cache.read(1, 2, now + 100ms));
  cache.publish(anchor::point_t {10, 20}, 1, 2, now, now + 301ms);
  EXPECT_FALSE(cache.read(1, 2, now + 301ms));
  cache.publish(anchor::point_t {10, 20}, 1, 2, now, now - 1ms);
  EXPECT_FALSE(cache.read(1, 2, now));
  cache.publish(std::nullopt, 1, 2, now, now);
  EXPECT_FALSE(cache.read(1, 2, now));
}

TEST(FocusAnchorSelection, UsesMouseForMissingOffDisplayAndInvalidCarets) {
  const anchor::point_t mouse {0, 20};
  EXPECT_EQ(anchor::select(mouse, anchor::point_t {-100, 30}, -1920, 0, 1920, 1080), (anchor::point_t {-100, 30}));
  EXPECT_EQ(anchor::select(mouse, anchor::point_t {0, 30}, -1920, 0, 1920, 1080), mouse);
  EXPECT_EQ(anchor::select(mouse, anchor::point_t {-1921, 30}, -1920, 0, 1920, 1080), mouse);
  EXPECT_EQ(anchor::select(mouse, anchor::point_t {-100, -1}, -1920, 0, 1920, 1080), mouse);
  EXPECT_EQ(anchor::select(mouse, anchor::point_t {-100, 1080}, -1920, 0, 1920, 1080), mouse);
  EXPECT_EQ(anchor::select(mouse, anchor::point_t {0, 0}, 0, 0, 0, 1080), mouse);
  EXPECT_EQ(anchor::select(mouse, anchor::point_t {0, 0}, 0, 0, 1920, 0), mouse);
  EXPECT_EQ(anchor::select(mouse, std::nullopt, 0, 0, 1920, 1080), mouse);
  EXPECT_FALSE(anchor::select(std::nullopt, std::nullopt, 0, 0, 1920, 1080));
  EXPECT_TRUE(anchor::select(std::nullopt, anchor::point_t {0, 0}, 0, 0, 1920, 1080));
}

TEST(FocusAnchorSelection, FeedsExistingRotationAndFocusGeometryWithoutChangingThePointer) {
  const auto point = anchor::select(anchor::point_t {100, 20}, anchor::point_t {-500, 100}, -1920, 0, 1920, 1080);
  video::cursor_focus::settings_t settings;
  settings.enabled = true;
  auto constants = video::cursor_focus::make_constants(settings, {-1920, 0, 1920, 1080, 1080, 1920, 1}, point->x, point->y);
  EXPECT_EQ(constants.enabled, 1);
  EXPECT_NEAR(constants.center[0], 100.5f / 1080, 0.0001f);
  EXPECT_NEAR(constants.center[1], 1 - 1420.5f / 1920, 0.0001f);
}

TEST(FocusAnchorSelection, ValidatesZeroWidthLegacyCaretsAndInvalidProviderGeometry) {
  EXPECT_EQ(anchor::rectangle_center(100, 200, 0, 20), (anchor::point_t {100, 210}));
  EXPECT_EQ(anchor::rectangle_center(-1920, -50, 2, 10), (anchor::point_t {-1919, -45}));
  EXPECT_FALSE(anchor::rectangle_center(0, 0, -1, 20));
  EXPECT_FALSE(anchor::rectangle_center(0, 0, 1, 0));
  EXPECT_FALSE(anchor::rectangle_center(std::numeric_limits<double>::infinity(), 0, 1, 10));
  EXPECT_FALSE(anchor::rectangle_center(0, std::numeric_limits<double>::quiet_NaN(), 1, 10));
  EXPECT_FALSE(anchor::rectangle_center(0, 0, std::numeric_limits<double>::infinity(), 10));
  EXPECT_FALSE(anchor::rectangle_center(0, 0, 1, std::numeric_limits<double>::quiet_NaN()));
  EXPECT_FALSE(anchor::rectangle_center(std::numeric_limits<int>::max(), 0, 2, 10));
  EXPECT_FALSE(anchor::rectangle_center(std::numeric_limits<int>::min() - 1.0, 0, 0, 10));
  EXPECT_FALSE(anchor::rectangle_center(0, std::numeric_limits<int>::max(), 0, 10));
  EXPECT_FALSE(anchor::rectangle_center(0, std::numeric_limits<int>::min() - 20.0, 0, 10));
}
