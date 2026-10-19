/**
 * @file tests/unit/test_video_monitor_switch.cpp
 * @brief Monitor geometry, stationary dwell and bounded fallback without capture hardware.
 */
#include "../tests_common.h"

#include <src/video_monitor_switch.h>
using namespace video::monitor_switch;
using namespace std::chrono_literals;

namespace {
  /**
   * @brief A main monitor and four neighbors, including negative desktop origins.
   */
  const std::vector<monitor_t> cross {
    {"main", {0, 0, 3840, 2160}},
    {"left", {-1920, 0, 0, 2160}},
    {"right", {3840, 0, 5760, 2160}},
    {"top", {0, -1080, 3840, 0}},
    {"bottom", {0, 2160, 3840, 3240}},
  };
}  // namespace

TEST(MonitorSwitch, EachOuterEdgeUsesTwentyPixelsAndMiddleFifth) {
  const std::array<std::pair<int, int>, 4> points {{{19, 1080}, {3820, 1080}, {1920, 19}, {1920, 2140}}};
  const std::array<std::string, 4> names {"left", "right", "top", "bottom"};
  const std::array<std::pair<int, int>, 4> landings {{{-22, 1080}, {3861, 1080}, {1920, -22}, {1920, 2181}}};
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto request = target_at_edge(cross, "main", {}, points[i]);
    ASSERT_TRUE(request);
    EXPECT_EQ(request->name, names[i]);
    EXPECT_EQ(request->point, landings[i]);
    EXPECT_EQ(*request, (request_t {names[i], landings[i]}));
  }
  for (const auto p : {std::pair {20, 1080}, std::pair {3819, 1080}, std::pair {1920, 20}, std::pair {1920, 2139}, std::pair {0, 0}, std::pair {3839, 2159}, std::pair {-1, 1080}, std::pair {3840, 1080}}) {
    EXPECT_FALSE(target_at_edge(cross, "main", {}, p));
  }
  EXPECT_FALSE(target_at_edge(cross, "missing", {}, {0, 1080}));
}

TEST(MonitorSwitch, OnlyVisibleMonitorEdgesActivateAndStaleOrInvalidGeometryIsIgnored) {
  auto crop = video::viewport::track({}, {true}, 3840, 2160, 1280, 960, {{1500, 1000}});
  EXPECT_FALSE(target_at_edge(cross, "main", crop, {crop.x, crop.y + 480}));
  crop = video::viewport::track({}, {true}, 3840, 2160, 1280, 960, {{3839, 1080}});
  EXPECT_EQ(target_at_edge(cross, "main", crop, {3839, crop.y + 480})->name, "right");
  EXPECT_FALSE(target_at_edge(cross, "main", crop, {3839, crop.y + 10}));
  crop.source_width = 1920;
  EXPECT_FALSE(target_at_edge(cross, "main", crop, {3839, 1080}));
  crop.source_width = 3840;
  crop.width = 0;
  EXPECT_FALSE(target_at_edge(cross, "main", crop, {3839, 1080}));
  std::vector<monitor_t> invalid {{"zero", {0, 0, 0, 10}}};
  EXPECT_FALSE(target_at_edge(invalid, "zero", {}, {0, 5}));
  EXPECT_TRUE((rect_t {0, 0, 1, 1}) == (rect_t {0, 0, 1, 1}));
}

TEST(MonitorSwitch, ChoosesNearestAlignedNeighborAndHandlesGapsUnpluggingAndSmallMonitors) {
  std::vector<monitor_t> layout {{"main", {-100, -100, 900, 900}}, {"far", {2000, -100, 3000, 900}}, {"near", {950, -100, 1950, 900}}, {"misaligned", {900, 1000, 1900, 2000}}, {"invalid", {900, 0, 900, 0}}};
  EXPECT_EQ(target_at_edge(layout, "main", {}, {899, 400})->name, "near");
  layout.erase(layout.begin() + 2);
  EXPECT_EQ(target_at_edge(layout, "main", {}, {899, 400})->name, "far");
  EXPECT_FALSE(target_at_edge(layout, "main", {}, {-100, 400}));
  std::vector<monitor_t> tiny {{"a", {0, 0, 2, 2}}, {"b", {2, 0, 4, 2}}};
  EXPECT_EQ(target_at_edge(tiny, "a", {}, {1, 0})->point, (std::pair {2, 0}));
}

TEST(MonitorSwitch, DwellWaitsForStationaryTimeThenLatchesUntilRelease) {
  dwell_t dwell;
  const auto t = std::chrono::steady_clock::time_point {};
  const request_t r {"next", {1021, 500}};
  EXPECT_FALSE(dwell.update(r, {999, 500}, t, 600ms));
  EXPECT_FALSE(dwell.update(r, {999, 500}, t + 599ms, 600ms));
  EXPECT_EQ(dwell.update(r, {999, 500}, t + 600ms, 600ms), r);
  EXPECT_FALSE(dwell.update(r, {999, 500}, t + 10s, 600ms));
  EXPECT_FALSE(dwell.update({}, {}, t + 11s, 600ms));
  EXPECT_FALSE(dwell.update(r, {999, 500}, t + 12s, 600ms));
  EXPECT_EQ(dwell.update(r, {999, 500}, t + 13s, 600ms), r);
}

TEST(MonitorSwitch, MovementTargetChangeClockResetAndDelayLimitsRestartDwell) {
  const auto t = std::chrono::steady_clock::time_point {};
  const request_t r {"a", {1, 1}};
  dwell_t d;
  EXPECT_FALSE(d.update(r, {0, 0}, t, 600ms));
  EXPECT_FALSE(d.update(r, {4, 4}, t + 500ms, 600ms));
  EXPECT_FALSE(d.update(r, {5, 4}, t + 600ms, 600ms));
  EXPECT_FALSE(d.update(r, {5, 4}, t + 1199ms, 600ms));
  EXPECT_EQ(d.update(r, {5, 4}, t + 1200ms, 600ms), r);
  d.update({}, {}, t, 600ms);
  d.update(r, {}, t + 1s, 600ms);
  EXPECT_FALSE(d.update(request_t {"b", {2, 2}}, {}, t + 2s, 600ms));
  EXPECT_FALSE(d.update(request_t {"b", {2, 2}}, {}, t, 600ms));
  EXPECT_EQ(d.update(request_t {"b", {2, 2}}, {}, t + 600ms, 600ms), (request_t {"b", {2, 2}}));
  d.update({}, {}, t, 600ms);
  d.update(r, {}, t, -1ms);
  EXPECT_FALSE(d.update(r, {}, t + 99ms, -1ms));
  EXPECT_EQ(d.update(r, {}, t + 100ms, -1ms), r);
  d.update({}, {}, t, 600ms);
  d.update(r, {}, t, 20s);
  EXPECT_FALSE(d.update(r, {}, t + 4999ms, 20s));
  EXPECT_EQ(d.update(r, {}, t + 5s, 20s), r);
}

TEST(MonitorSwitch, TransitionKeepsTransportOwnershipAndBoundsTargetAndRestoreAttempts) {
  const request_t r {"new", {21, 500}};
  const std::vector<std::string> names {"old", "new"};
  std::vector<std::string> calls;
  int warps = 0;
  auto open = [&](const std::string &name) {
    calls.push_back(name);
    return true;
  };
  auto warp = [&](auto point) {
    ++warps;
    EXPECT_EQ(point, r.point);
    return true;
  };
  auto result = transition(r, "old", names, open, warp);
  EXPECT_TRUE(result.ready);
  EXPECT_TRUE(result.switched);
  EXPECT_EQ(result.selected, "new");
  EXPECT_EQ(calls, (std::vector<std::string> {"new"}));
  EXPECT_EQ(warps, 1);
  calls.clear();
  warps = 0;
  result = transition(r, "old", names, [&](const auto &n) {
    calls.push_back(n);
    return n == "old";
  },
                      warp);
  EXPECT_TRUE(result.ready);
  EXPECT_FALSE(result.switched);
  EXPECT_EQ(result.selected, "old");
  EXPECT_EQ(calls, (std::vector<std::string> {"new", "old"}));
  EXPECT_EQ(warps, 0);
  calls.clear();
  result = transition(r, "old", names, open, [](auto) {
    return false;
  });
  EXPECT_TRUE(result.ready);
  EXPECT_FALSE(result.switched);
  EXPECT_EQ(calls.size(), 2);
  calls.clear();
  result = transition(request_t {"removed", {}}, "old", names, open, warp);
  EXPECT_EQ(calls, (std::vector<std::string> {"old"}));
  EXPECT_FALSE(result.switched);
  calls.clear();
  result = transition(request_t {"old", {}}, "old", names, open, warp);
  EXPECT_EQ(calls, (std::vector<std::string> {"old"}));
  EXPECT_FALSE(result.switched);
  result = transition(r, "old", names, [](auto) {
    return false;
  },
                      warp);
  EXPECT_FALSE(result.ready);
  EXPECT_FALSE(result.switched);
}

TEST(MonitorSwitch, AutomaticTargetsRequireKnownMatchingGpuAndHdrState) {
  const capture_key_t source {42, false};
  EXPECT_TRUE(compatible(source, source));
  EXPECT_FALSE(compatible({}, source));
  EXPECT_FALSE(compatible(source, {}));
  EXPECT_FALSE(compatible(capture_key_t {43, false}, source));
  EXPECT_FALSE(compatible(capture_key_t {42, true}, source));
}
