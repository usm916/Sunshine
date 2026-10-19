/**
 * @file tests/unit/test_video_viewport.cpp
 * @brief Viewport tracking, rotated texture bounds, and direct-input invariants.
 */
#include "../tests_common.h"

#include <limits>
#include <src/video_viewport.h>
using namespace video::viewport;

TEST(ViewportCropTest, DisabledInvalidEqualAndSmallerSourcesKeepWholeMonitor) {
  EXPECT_FALSE(track({}, {}, 3840, 2160, 1920, 1080, {}).active);
  for (const auto size : {std::array {0, 2160, 1920, 1080}, std::array {3840, -1, 1920, 1080}, std::array {3840, 2160, 0, 1080}, std::array {3840, 2160, 1920, -1}, std::array {1920, 1080, 1920, 1080}, std::array {1280, 720, 1920, 1080}}) {
    EXPECT_FALSE(track({}, {true}, size[0], size[1], size[2], size[3], {}).active);
  }
  EXPECT_EQ(shader_constants({}, 0).enabled, 0);
}

TEST(ViewportCropTest, SectionCountsOriginsAndInvalidDimensionsAreGeneralized) {
  EXPECT_EQ(section_count(3840, 2560), 2);
  EXPECT_EQ(section_origin(3840, 2560, 0), 0);
  EXPECT_EQ(section_origin(3840, 2560, 1), 1280);
  EXPECT_EQ(section_count(3840, 1280), 4);
  const std::array horizontal {0, 853, 1706, 2560};
  for (int index = 0; index < 4; ++index) {
    EXPECT_EQ(section_origin(3840, 1280, index), horizontal[index]);
  }
  EXPECT_EQ(section_count(2160, 960), 3);
  EXPECT_EQ(section_count(1280, 1280), 1);
  EXPECT_EQ(section_count(3840, 1000), 4);
  EXPECT_EQ(section_origin(3840, 1000, 1), 946);
  EXPECT_EQ(section_origin(3840, 1000, 2), 1893);
  EXPECT_EQ(section_origin(3840, 1000, 3), 2840);
  EXPECT_EQ(section_origin(3840, 1280, -1), 0);
  EXPECT_EQ(section_origin(3840, 1280, 10), 2560);
  EXPECT_EQ(section_count(720, 1080), 1);
  EXPECT_EQ(section_origin(720, 1080, 1), 0);
  EXPECT_EQ(follow_section(720, 1080, 360, {}, 0), 0);
  for (const auto size : {std::pair {0, 1}, std::pair {1, 0}, std::pair {-1, 1}, std::pair {1, -1}}) {
    EXPECT_EQ(section_count(size.first, size.second), 0);
    EXPECT_EQ(section_origin(size.first, size.second, 0), 0);
    EXPECT_EQ(follow_section(size.first, size.second, 0, {}, 0), 0);
  }
}

TEST(ViewportCropTest, FiftyPixelBandsSwitchAtEachEdgeAndKeepTheInteriorFixed) {
  const settings_t settings {true, 50, 50};
  const auto middle = track({}, settings, 3840, 2160, 1280, 960, {{1493, 1080}});
  ASSERT_EQ(middle.x, 853);
  ASSERT_EQ(middle.y, 600);
  for (const auto point : {std::pair {50, 50}, std::pair {1229, 909}, std::pair {50, 909}, std::pair {1229, 50}}) {
    EXPECT_EQ(track(middle, settings, 3840, 2160, 1280, 960, {{middle.x + point.first, middle.y + point.second}}), middle);
  }
  const std::array<std::pair<int, int>, 4> points {{{49, 480}, {1230, 480}, {640, 49}, {640, 910}}};
  const std::array<std::pair<int, int>, 4> expected {{{0, 600}, {1706, 600}, {853, 0}, {853, 1200}}};
  for (std::size_t i = 0; i < points.size(); ++i) {
    const std::pair anchor {middle.x + points[i].first, middle.y + points[i].second};
    const auto moved = track(middle, settings, 3840, 2160, 1280, 960, anchor);
    EXPECT_EQ(moved.x, expected[i].first);
    EXPECT_EQ(moved.y, expected[i].second);
    EXPECT_EQ(track(moved, settings, 3840, 2160, 1280, 960, anchor), moved);
  }
  EXPECT_EQ(settings_t {}.deadzone_width, 20);
  EXPECT_EQ(settings_t {}.deadzone_height, 20);
  EXPECT_EQ(track(middle, {true, 0, 0}, 3840, 2160, 1280, 960, {{2113, 1080}}).x, 1706);
}

TEST(ViewportCropTest, DefaultTwentyPixelBandsSwitchOnlyWithinEachEdge) {
  const settings_t settings {true};
  const auto middle = track({}, settings, 3840, 2160, 1280, 960, {{1493, 1080}});
  for (const auto point : {std::pair {20, 20}, std::pair {1259, 939}, std::pair {20, 939}, std::pair {1259, 20}}) {
    EXPECT_EQ(track(middle, settings, 3840, 2160, 1280, 960, {{middle.x + point.first, middle.y + point.second}}), middle);
  }
  const std::array<std::pair<int, int>, 4> points {{{19, 480}, {1260, 480}, {640, 19}, {640, 940}}};
  const std::array<std::pair<int, int>, 4> expected {{{0, 600}, {1706, 600}, {853, 0}, {853, 1200}}};
  for (std::size_t i = 0; i < points.size(); ++i) {
    const std::pair anchor {middle.x + points[i].first, middle.y + points[i].second};
    const auto moved = track(middle, settings, 3840, 2160, 1280, 960, anchor);
    EXPECT_EQ(moved.x, expected[i].first);
    EXPECT_EQ(moved.y, expected[i].second);
    EXPECT_EQ(track(moved, settings, 3840, 2160, 1280, 960, anchor), moved);
    EXPECT_EQ(track(middle, {true, 0, 0}, 3840, 2160, 1280, 960, anchor), moved);
  }
}

TEST(ViewportCropTest, ConfigurableBandsClampToOverlapAndKeepSwitchesStable) {
  const settings_t settings {true, 200, 200};
  auto left = track({}, settings, 3840, 2160, 2560, 1440, {{0, 0}});
  EXPECT_EQ(left.x, 0);
  EXPECT_EQ(left.y, 0);
  EXPECT_EQ(track(left, settings, 3840, 2160, 2560, 1440, {{2359, 1239}}), left);
  auto right = track(left, settings, 3840, 2160, 2560, 1440, {{2360, 1240}});
  EXPECT_EQ(right.x, 1280);
  EXPECT_EQ(right.y, 720);
  EXPECT_EQ(track(right, settings, 3840, 2160, 2560, 1440, {{1480, 920}}), right);
  EXPECT_EQ(track(right, settings, 3840, 2160, 2560, 1440, {{1479, 919}}), left);
  EXPECT_EQ(track(left, {true, 9000, 9000}, 3840, 2160, 2560, 1440, {{1919, 1079}}), left);
  EXPECT_EQ(track(left, {true, 9000, 9000}, 3840, 2160, 2560, 1440, {{1920, 1080}}), right);
  EXPECT_EQ(track(right, {true, 9000, 9000}, 3840, 2160, 2560, 1440, {{1920, 1080}}), right);
  EXPECT_EQ(track(right, {true, 9000, 9000}, 3840, 2160, 2560, 1440, {{1919, 1079}}), left);
  EXPECT_EQ(track(left, {true, -5, -5}, 3840, 2160, 2560, 1440, {{2558, 1438}}), left);
  EXPECT_EQ(track(left, {true, -5, -5}, 3840, 2160, 2560, 1440, {{2559, 1439}}), right);
  EXPECT_EQ(track({}, settings, 3840, 2160, 2560, 1440, {{-200, 9000}}).y, 720);
}

TEST(ViewportCropTest, ExactRatioAddsOverlapAndAbsoluteInputCanReachEverySection) {
  for (const int zone : {0, 200, 9000}) {
    const settings_t settings {true, zone, zone};
    auto frame = track({}, settings, 3840, 2160, 1280, 960, {{0, 0}});
    const std::array positions {0, 853, 1706, 2560};
    for (int index = 1; index < 4; ++index) {
      const auto point = map_point(frame, 1279, 0);
      frame = track(frame, settings, 3840, 2160, 1280, 960, {{static_cast<int>(point.first), static_cast<int>(point.second)}});
      EXPECT_EQ(frame.x, positions[index]);
      EXPECT_EQ(track(frame, settings, 3840, 2160, 1280, 960, {{static_cast<int>(point.first), static_cast<int>(point.second)}}), frame);
    }
    for (int index = 2; index >= 0; --index) {
      const auto point = map_point(frame, 0, 0);
      frame = track(frame, settings, 3840, 2160, 1280, 960, {{static_cast<int>(point.first), static_cast<int>(point.second)}});
      EXPECT_EQ(frame.x, positions[index]);
    }
    for (int index = 1; index < 3; ++index) {
      const auto point = map_point(frame, 0, 959);
      frame = track(frame, settings, 3840, 2160, 1280, 960, {{static_cast<int>(point.first), static_cast<int>(point.second)}});
      EXPECT_EQ(frame.y, index * 600);
    }
    for (int index = 1; index >= 0; --index) {
      const auto point = map_point(frame, 0, 0);
      frame = track(frame, settings, 3840, 2160, 1280, 960, {{static_cast<int>(point.first), static_cast<int>(point.second)}});
      EXPECT_EQ(frame.y, index * 600);
    }
    const auto last = track(frame, settings, 3840, 2160, 1280, 960, {{3839, 2159}});
    EXPECT_EQ(last.x, 2560);
    EXPECT_EQ(last.y, 1200);
  }
}

TEST(ViewportCropTest, DefaultEdgeBandsUnknownAnchorResizeAndFrozenContactsAreSafe) {
  auto frame = track({}, {true}, 3840, 2160, 2560, 1440, {});
  EXPECT_EQ(frame.x, 1280);
  EXPECT_EQ(frame.y, 720);
  EXPECT_EQ(track(frame, {true}, 3840, 2160, 2560, 1440, {}), frame);
  EXPECT_EQ(track(frame, {true}, 3840, 2160, 2560, 1440, {{0, 0}}, true), frame);
  EXPECT_EQ(track(frame, {true}, 3840, 2160, 2560, 1440, {{1300, 740}}), frame);
  EXPECT_EQ(track(frame, {true}, 3840, 2160, 2560, 1440, {{1299, 739}}).x, 0);
  const auto resized = track(frame, {true}, 2560, 1440, 1280, 720, {{0, 0}}, true);
  EXPECT_EQ(resized.x, 0);
  EXPECT_EQ(resized.y, 0);
  EXPECT_EQ(resized.width, 1280);
  EXPECT_EQ(track({}, {true}, 3840, 2160, 1280, 720, {{3839, 2159}}, true).x, 2560);
}

TEST(ViewportCropTest, AllSmallDimensionsKeepAnchorsVisibleAndOriginsOnFixedSections) {
  for (int total = 2; total <= 64; ++total) {
    for (int length = 1; length < total; ++length) {
      const int count = section_count(total, length);
      std::unordered_set<int> origins;
      for (int index = 0; index < count; ++index) {
        origins.insert(section_origin(total, length, index));
      }
      ASSERT_EQ(origins.size(), count);
      for (int coordinate = 0; coordinate < total; ++coordinate) {
        const int initial = follow_section(total, length, coordinate, {}, 0);
        ASSERT_TRUE(origins.contains(initial));
        ASSERT_GE(coordinate, initial);
        ASSERT_LT(coordinate, initial + length);
        for (const int previous : origins) {
          for (const int zone : {-1, 0, 1, 49, 50, 200}) {
            const int origin = follow_section(total, length, coordinate, previous, zone);
            ASSERT_TRUE(origins.contains(origin));
            ASSERT_GE(coordinate, origin);
            ASSERT_LT(coordinate, origin + length);
            ASSERT_EQ(follow_section(total, length, coordinate, origin, zone), origin);
          }
        }
      }
    }
  }
}

TEST(ViewportCropTest, LargeDimensionsUseWideArithmeticForOriginsAndHysteresis) {
  constexpr int total = std::numeric_limits<int>::max();
  EXPECT_EQ(section_count(total, 1), total);
  EXPECT_EQ(section_origin(total, 1, total - 1), total - 1);
  EXPECT_EQ(follow_section(total, 1, total - 1, {}, 0), total - 1);
  EXPECT_EQ(follow_section(total, total - 1, total - 1, 0, total), 1);
  EXPECT_EQ(follow_section(total, total - 1, 0, 1, total), 0);
}

TEST(ViewportCropTest, ShortAxisUsesCenteredBlackPaddingAndClampsInput) {
  auto frame = track({}, {true}, 3840, 720, 1920, 1080, {{1920, 360}});
  EXPECT_EQ(frame.width, 1920);
  EXPECT_EQ(frame.height, 720);
  EXPECT_EQ(map_point(frame, 0, 0), (std::pair<float, float> {960, 0}));
  EXPECT_EQ(map_point(frame, 1919, 1079), (std::pair<float, float> {2879, 719}));
  EXPECT_EQ(map_point(frame, 20, 200), (std::pair<float, float> {980, 20}));
  frame = track({}, {true}, 720, 3840, 1080, 1920, {{360, 1920}});
  EXPECT_EQ(map_point(frame, 200, 20), (std::pair<float, float> {20, 980}));
}

TEST(ViewportCropTest, RotatedSamplingMatchesFullDesktopPhysicalCoordinates) {
  const frame_t frame {300, 200, 1000, 600, 4000, 2000, 1000, 600, true};
  const std::array<std::array<float, 4>, 4> expected {{{0.075f, 0.1f, 0.25f, 0.3f}, {0.1f, 0.675f, 0.3f, 0.25f}, {0.675f, 0.6f, 0.25f, 0.3f}, {0.6f, 0.075f, 0.3f, 0.25f}}};
  for (int rotation = 0; rotation < 4; ++rotation) {
    auto constants = shader_constants(frame, rotation);
    EXPECT_EQ(constants.enabled, 1);
    EXPECT_NEAR(constants.origin[0], expected[rotation][0], 0.00001);
    EXPECT_NEAR(constants.origin[1], expected[rotation][1], 0.00001);
    EXPECT_NEAR(constants.scale[0], expected[rotation][2], 0.00001);
    EXPECT_NEAR(constants.scale[1], expected[rotation][3], 0.00001);
  }
  EXPECT_EQ(shader_constants(frame, -1).enabled, 0);
  EXPECT_EQ(shader_constants(frame, 4).enabled, 0);
  auto invalid = frame;
  invalid.source_width = 0;
  EXPECT_EQ(shader_constants(invalid, 0).enabled, 0);
  invalid = frame;
  invalid.source_height = 0;
  EXPECT_EQ(shader_constants(invalid, 0).enabled, 0);
}

TEST(ViewportCropTest, ContactTrackingKeepsMultipleFingersFrozenUntilAllAreReleased) {
  state_t state;
  state.frame = track({}, {true}, 3840, 2160, 1920, 1080, {});
  EXPECT_TRUE(state.snapshot().active);
  state.contact(1, true);
  state.contact(2, true);
  state.contact(1, false);
  EXPECT_EQ(state.contacts.size(), 1);
  state.contact(2, false);
  EXPECT_TRUE(state.contacts.empty());
  state.contact(1ULL << 32, true);
  state.contact(9, false, true);
  EXPECT_TRUE(state.contacts.empty());
}
