/**
 * @file tests/unit/test_video_low_bandwidth.cpp
 * @brief Low-rate admission, bitrate/FPS coupling, congestion and recovery traces.
 */
#include "../tests_common.h"

#include <limits>
#include <src/video_low_bandwidth.h>

namespace {
  namespace low = video::low_bandwidth;
  using namespace std::chrono_literals;
  constexpr low::time_point_t epoch {};  ///< Deterministic monotonic test origin.
}  // namespace

TEST(LowBandwidthProfile, BoundsInvalidLimitsAndAccountsForParity) {
  low::settings_t settings;
  for (auto budget : {0U, 32U, 100U, 250U, 500U, 1000U, 2000U, 3000U, 1000001U}) {
    const auto p = low::make_profile(settings, budget, 30);
    EXPECT_GE(p.fps, 2);
    EXPECT_LE(p.fps, 15);
    EXPECT_GE(p.video_kbps, 8U);
    EXPECT_LE(p.video_kbps, p.wire_kbps);
    EXPECT_EQ(p, p);
  }
  settings.minimum_fps = 100;
  settings.maximum_fps = -1;
  EXPECT_EQ(low::make_profile(settings, 250, 0).fps, 1);
  EXPECT_GT(low::make_profile({}, 1000, 30, 1400, 0, 0).video_kbps, low::make_profile({}, 1000, 30).video_kbps);
  EXPECT_GE(low::make_profile({}, 64, 30, -1, -1, -1).video_kbps, 8U);
  EXPECT_GE(low::make_profile({}, 32, 30, 999999, 255, 255).video_kbps, 8U);
}

TEST(LowBandwidthProfile, PreservesGeometryAndLinksExistingBackgroundThresholds) {
  video::cursor_focus::settings_t base;
  base.shape = "rectangle";
  base.width = 400;
  base.height = 240;
  for (const auto budget : {100U, 250U, 500U, 1000U, 2000U, 3000U}) {
    const auto focus = low::focus_settings(base, budget);
    EXPECT_EQ(focus.width, 400);
    EXPECT_EQ(focus.height, 240);
    EXPECT_EQ(focus.shape, "rectangle");
    EXPECT_EQ(focus.enabled, budget < 3000);
  }
  EXPECT_EQ(low::focus_settings(base, 250).background_scale, 6);
  EXPECT_EQ(low::focus_settings(base, 500).background_scale, 4);
  EXPECT_EQ(low::focus_settings(base, 1000).background_scale, 3);
  EXPECT_EQ(low::focus_settings(base, 2000).background_scale, 2);
  EXPECT_EQ(low::focus_settings(base, 500).background_loop_ms, 3000);
  EXPECT_EQ(low::focus_settings(base, 1000).background_loop_ms, 1000);
  EXPECT_EQ(low::focus_settings(base, 2000).background_loop_frames, 5);
}

TEST(LowBandwidthController, FixedModeNeverAdaptsAndEarlySamplesAreIgnored) {
  low::controller_t controller;
  controller.initialize({}, 500, epoch);
  EXPECT_FALSE(controller.observe({1000, 1, 10}, epoch + 1s, 2000));
  EXPECT_EQ(controller.target(), 500U);
  low::settings_t settings;
  settings.mode = "adaptive";
  controller.initialize(settings, 500, epoch);
  EXPECT_FALSE(controller.observe({}, epoch - 1s, 0));
  EXPECT_FALSE(controller.observe({}, epoch + 100ms, 0));
  EXPECT_EQ(controller.target(), 500U);
}

TEST(LowBandwidthController, PersistentLossReachesSubMegabitFloorAndKeepsFpsBounded) {
  low::settings_t settings;
  settings.mode = "adaptive";
  settings.minimum_kbps = 100;
  low::controller_t controller;
  controller.initialize(settings, 1000, epoch);
  for (std::uint32_t i = 1; i <= 30; ++i) {
    controller.observe({40, i, 2}, epoch + i * 500ms, 0);
    const auto profile = low::make_profile(settings, controller.target(), 30);
    EXPECT_LE(profile.fps, settings.maximum_fps);
    EXPECT_GE(profile.fps, settings.minimum_fps);
  }
  EXPECT_EQ(controller.target(), 100U);
  EXPECT_FALSE(controller.observe({40, 31, 2}, epoch + 15500ms, 0));
  EXPECT_FALSE(controller.observe({40, 32, 2}, epoch + 16s, 0));
}

TEST(LowBandwidthController, IgnoresAnIsolatedLossAndRecoversOnlyWithFreshStableControl) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 500, epoch);
  EXPECT_FALSE(controller.observe({40, 1, 1}, epoch + 500ms, 0));
  EXPECT_FALSE(controller.observe({40, 2}, epoch + 1s, 0));
  EXPECT_FALSE(controller.observe({40, 3, 1}, epoch + 1500ms, 0));
  ASSERT_TRUE(controller.observe({40, 4, 1}, epoch + 2s, 0));
  const auto reduced = controller.target();
  for (std::uint32_t i = 5; i < 12; ++i) {
    EXPECT_FALSE(controller.observe({40, i}, epoch + i * 500ms, 0));
  }
  ASSERT_TRUE(controller.observe({40, 12}, epoch + 6s, 0));
  EXPECT_GT(controller.target(), reduced);
  for (std::uint32_t i = 13; i < 250; ++i) {
    controller.observe({40, i}, epoch + i * 500ms, 0);
  }
  EXPECT_EQ(controller.target(), 500U);
}

TEST(LowBandwidthController, DetectsRttGrowthAndWireOvershootWithoutFecReports) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 500, epoch);
  controller.observe({40, 1}, epoch + 500ms, 0);
  for (unsigned tick = 2; tick <= 8; ++tick) {
    controller.observe({200, tick}, epoch + tick * 500ms, 0);
  }
  const auto reduced = controller.target();
  EXPECT_LT(reduced, 500U);
  for (unsigned tick = 9; tick <= 18; ++tick) {
    controller.observe({40, tick}, epoch + tick * 500ms, 1000);
  }
  EXPECT_LT(controller.target(), reduced);
}

TEST(LowBandwidthController, StaleControlAndInvalidFeedbackNeverPermitRecovery) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 500, epoch);
  controller.observe({0, 0}, epoch + 500ms, 0);
  controller.observe({40, 1}, epoch + 1s, 0);
  for (unsigned i = 3; i <= 30; ++i) {
    controller.observe({40, 1}, epoch + i * 500ms, 0);
  }
  EXPECT_LT(controller.target(), 500U);
  const auto reduced = controller.target();
  for (unsigned i = 31; i <= 70; ++i) {
    EXPECT_FALSE(controller.observe({40, i, 0, 0, false}, epoch + i * 500ms, 0));
  }
  EXPECT_EQ(controller.target(), reduced);
  EXPECT_FALSE(controller.observe({40, 71}, epoch + 60s, 0));
}

TEST(LowBandwidthController, TinyCeilingAndRecoveredPacketsStayWithinPolicy) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 0, epoch);
  EXPECT_EQ(controller.target(), 32U);
  controller.observe({40, 1}, epoch + 500ms, 0);
  EXPECT_FALSE(controller.observe({40, 2, 0, 1}, epoch + 1s, 40));
  EXPECT_FALSE(controller.observe({40, 3, 0, 1}, epoch + 1500ms, 40));
  EXPECT_EQ(controller.target(), 32U);
}

TEST(LowBandwidthGate, FirstFrameIsImmediateAndStallsDoNotTriggerCatchUpBursts) {
  low::frame_gate_t gate;
  EXPECT_EQ(gate.wait(epoch), low::clock_t::duration::zero());
  gate.submitted(epoch, {250, 80, 5}, 0);
  EXPECT_EQ(gate.wait(epoch), 200ms);
  EXPECT_EQ(gate.wait(epoch + 1s), low::clock_t::duration::zero());
  gate.submitted(epoch + 1s, {250, 80, 5}, 10000);
  EXPECT_EQ(gate.wait(epoch + 1s), 320ms);
  gate.submitted(epoch + 2s, {0, 0, 0}, std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(gate.wait(epoch + 2s), 1s);
}

TEST(LowBandwidthTrace, VariableSubMegabitLinkSlowsAndRecoversWithoutExceedingLimits) {
  low::settings_t settings;
  settings.mode = "adaptive";
  settings.minimum_kbps = 64;
  low::controller_t controller;
  controller.initialize(settings, 1000, epoch);
  bool slowed = false;
  bool recovered = false;
  for (unsigned tick = 1; tick <= 240; ++tick) {
    const unsigned capacity = tick < 20 ? 1000 : tick < 100 ? 250 :
                                                              1000;
    const bool pressure = controller.target() > capacity;
    controller.observe({pressure ? 200U : 40U, tick, pressure ? 1U : 0U}, epoch + tick * 500ms, controller.target());
    EXPECT_GE(controller.target(), 64U);
    EXPECT_LE(controller.target(), 1000U);
    if (tick >= 20 && tick < 100 && controller.target() <= 250) {
      slowed = true;
    }
    if (tick > 100 && controller.target() > 500) {
      recovered = true;
    }
  }
  EXPECT_TRUE(slowed);
  EXPECT_TRUE(recovered);
}

TEST(LowBandwidthProfile, DefaultCeilingFollowsClientAndKeepsFourMegabitsUnfiltered) {
  low::settings_t settings;
  EXPECT_EQ(low::select_ceiling(settings, 4000), 4000U);
  EXPECT_FALSE(low::focus_settings({}, low::select_ceiling(settings, 4000)).enabled);
  EXPECT_EQ(low::select_ceiling(settings, 250), 250U);
  EXPECT_EQ(low::select_ceiling(settings, -1), 32U);
  EXPECT_EQ(low::select_ceiling(settings, 2000000), 1000000U);
  EXPECT_EQ(low::select_ceiling(settings, 4000, 2000), 2000U);
  settings.target_kbps = 500;
  EXPECT_EQ(low::select_ceiling(settings, 4000), 500U);
  EXPECT_EQ(low::select_ceiling(settings, 250), 250U);
  EXPECT_EQ(low::select_ceiling(settings, 4000, 250), 250U);
  EXPECT_EQ(low::select_ceiling(settings, 4000, 1), 32U);
}

TEST(LowBandwidthController, SparseHealthyControlRestoresTheOriginalBackgroundProfile) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 1000, epoch);
  EXPECT_FALSE(controller.observe({40, 1, 1}, epoch + 500ms, 0));
  ASSERT_TRUE(controller.observe({40, 2, 1}, epoch + 1s, 0));
  EXPECT_EQ(controller.target(), 750U);
  EXPECT_EQ(low::focus_settings({}, controller.target()).background_scale, 4);
  for (std::uint32_t tick = 3; tick <= 120; ++tick) {
    // A healthy acknowledgement every second leaves alternating 500-ms windows empty.
    const auto token = 2 + (tick - 2) / 2;
    controller.observe({40, token}, epoch + tick * 500ms, 0);
  }
  EXPECT_EQ(controller.target(), 1000U);
  EXPECT_EQ(low::focus_settings({}, controller.target()).background_scale, 3);
  EXPECT_EQ(low::focus_settings({}, controller.target()).background_loop_ms, 1000);
}

TEST(LowBandwidthController, FreshTokensWithoutHealthyRttNeverCertifyRecovery) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 500, epoch);
  controller.observe({40, 1, 1}, epoch + 500ms, 0);
  ASSERT_TRUE(controller.observe({40, 2, 1}, epoch + 1s, 0));
  const auto reduced = controller.target();
  for (std::uint32_t tick = 3; tick <= 60; ++tick) {
    controller.observe({0, tick}, epoch + tick * 500ms, 0);
    EXPECT_LE(controller.target(), reduced);
  }
}

TEST(LowBandwidthController, OneFpsSenderBurstsRecoverWithoutAReconnect) {
  low::settings_t settings;
  settings.mode = "adaptive";
  settings.minimum_fps = settings.maximum_fps = 1;
  low::controller_t controller;
  controller.initialize(settings, 4000, epoch);
  for (unsigned tick = 1; tick <= 32; ++tick) {
    controller.observe({40, tick, 1}, epoch + tick * 500ms, 0);
  }
  ASSERT_EQ(controller.target(), 100U);
  for (unsigned tick = 33; tick <= 432; ++tick) {
    const auto sent = tick % 2 ? controller.target() * 18 / 10 : 0;
    controller.observe({40, tick}, epoch + tick * 500ms, sent);
  }
  EXPECT_EQ(controller.target(), 4000U);
  EXPECT_FALSE(low::focus_settings({}, controller.target()).enabled);
}

TEST(LowBandwidthController, CleanStableRouteWithHigherRttEventuallyRecovers) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 4000, epoch);
  controller.observe({20, 1}, epoch + 500ms, 0);
  for (unsigned tick = 2; tick <= 480; ++tick) {
    controller.observe({80, tick}, epoch + tick * 500ms, 0);
  }
  EXPECT_EQ(controller.target(), 4000U);
  EXPECT_FALSE(low::focus_settings({}, controller.target()).enabled);
}

TEST(LowBandwidthSenderWindow, WeightsUnevenIntervalsAndExpiresPartialBuckets) {
  low::sender_window_t window;
  EXPECT_FALSE(window.ready());
  EXPECT_EQ(window.average_kbps(), 0U);
  window.observe(100, 750ms);
  window.observe(200, 750ms);
  window.observe(300, 1500ms);
  EXPECT_TRUE(window.ready());
  EXPECT_EQ(window.average_kbps(), 225U);
  window.observe(400, 500ms);
  EXPECT_EQ(window.average_kbps(), 275U);
  window.observe(0, 3s);
  EXPECT_TRUE(window.ready());
  EXPECT_EQ(window.average_kbps(), 0U);
  window.reset();
  EXPECT_FALSE(window.ready());
  EXPECT_EQ(window.average_kbps(), 0U);
  window.observe(std::numeric_limits<std::uint32_t>::max(), 30s);
  EXPECT_EQ(window.average_kbps(), 1000000U);
  window.reset();
  window.observe(10, 0ms);
  EXPECT_FALSE(window.ready());
  for (unsigned tick = 0; tick < 100; ++tick) {
    window.observe(10, 500ms);
  }
  EXPECT_TRUE(window.ready());
  EXPECT_EQ(window.average_kbps(), 10U);
}

TEST(LowBandwidthController, SustainedOvershootStillReducesAfterTheSenderWindowFills) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 500, epoch);
  for (unsigned tick = 1; tick <= 5; ++tick) {
    EXPECT_FALSE(controller.observe({40, tick}, epoch + tick * 500ms, 1000));
  }
  EXPECT_FALSE(controller.observe({40, 6}, epoch + 3s, 1000));
  EXPECT_EQ(controller.diagnostics().holds, low::sender_pressure);
  ASSERT_TRUE(controller.observe({40, 7}, epoch + 3500ms, 1000));
  EXPECT_EQ(controller.target(), 375U);
  controller.observe({40, 8}, epoch + 4s, 90);
  EXPECT_FALSE(controller.diagnostics().sender_ready);
  EXPECT_EQ(controller.diagnostics().sender_average_kbps, 90U);
}

TEST(LowBandwidthController, RouteBaselineRequiresExpiryAndCleanStableRttAfterReducingLoad) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 4000, epoch);
  controller.observe({20, 1}, epoch + 500ms, 0);
  for (unsigned tick = 2; tick <= 60; ++tick) {
    controller.observe({80, tick}, epoch + tick * 500ms, 0);
  }
  EXPECT_EQ(controller.diagnostics().baseline_rtt_ms, 20U);
  EXPECT_EQ(controller.diagnostics().baseline_revision, 0U);
  controller.observe({80, 61}, epoch + 30500ms, 0);
  EXPECT_EQ(controller.diagnostics().baseline_rtt_ms, 80U);
  EXPECT_EQ(controller.diagnostics().baseline_revision, 1U);
  EXPECT_EQ(controller.target(), 3240U);
  controller.observe({10, 62}, epoch + 31s, 0);
  EXPECT_EQ(controller.diagnostics().baseline_rtt_ms, 80U);
  for (unsigned tick = 63; tick <= 125; ++tick) {
    controller.observe({10, tick}, epoch + tick * 500ms, 0);
  }
  EXPECT_EQ(controller.diagnostics().baseline_rtt_ms, 10U);
}

TEST(LowBandwidthController, CongestionNoiseLossRecoveredPacketsAndUnreliableReportsNeverRebaseTheRoute) {
  low::settings_t settings;
  settings.mode = "adaptive";
  for (unsigned mode = 0; mode < 5; ++mode) {
    low::controller_t controller;
    controller.initialize(settings, 4000, epoch);
    controller.observe({20, 1}, epoch + 500ms, 0);
    for (unsigned tick = 2; tick <= 360; ++tick) {
      const unsigned rtt = mode == 0 && tick % 2 ? 140 : 80;
      controller.observe({rtt, tick, mode == 1 ? 1U : 0U, mode == 2 ? 1U : 0U, mode != 3}, epoch + tick * 500ms, mode == 4 ? 10000 : 0);
    }
    EXPECT_EQ(controller.diagnostics().baseline_rtt_ms, 20U) << mode;
    EXPECT_EQ(controller.diagnostics().baseline_revision, 0U) << mode;
    if (mode == 3) {
      EXPECT_EQ(controller.target(), 4000U);
    } else {
      EXPECT_LT(controller.target(), 4000U) << mode;
    }
  }
}

TEST(LowBandwidthController, StaleHighRttCannotAgeTheRouteOrAuthorizeRecovery) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 4000, epoch);
  controller.observe({20, 1}, epoch + 500ms, 0);
  controller.observe({80, 2}, epoch + 1s, 0);
  for (unsigned tick = 3; tick <= 360; ++tick) {
    controller.observe({80, 2}, epoch + tick * 500ms, 0);
  }
  EXPECT_EQ(controller.target(), 100U);
  EXPECT_EQ(controller.diagnostics().baseline_revision, 0U);
  EXPECT_EQ(controller.diagnostics().baseline_rtt_ms, 20U);
  EXPECT_TRUE(controller.diagnostics().holds & low::stalled);
  controller.observe({80, 3}, epoch + 200s, 0);
  EXPECT_TRUE(controller.diagnostics().holds & low::sampling_gap);
  EXPECT_EQ(controller.diagnostics().healthy_ms, 0U);
  EXPECT_EQ(controller.diagnostics().ack_age_ms, 0U);
}

TEST(LowBandwidthDiagnostics, LogsOnlyChangedReasonsAndCoalescesRapidTransitions) {
  low::diagnostic_gate_t gate;
  low::diagnostics_t value;
  EXPECT_TRUE(gate.should_log(value, epoch));
  value.holds = low::loss;
  EXPECT_FALSE(gate.should_log(value, epoch - 1s));
  EXPECT_FALSE(gate.should_log(value, epoch + 500ms));
  EXPECT_TRUE(gate.should_log(value, epoch + 2s));
  EXPECT_FALSE(gate.should_log(value, epoch + 100s));
  value.healthy_ms = 99999;
  value.ack_age_ms = 500;
  value.sender_average_kbps = 123;
  EXPECT_FALSE(gate.should_log(value, epoch + 101s));
  value.baseline_revision = 1;
  EXPECT_TRUE(gate.should_log(value, epoch + 102s));
  value.sender_ready = true;
  EXPECT_TRUE(gate.should_log(value, epoch + 104s));
  value.adaptation_stopped = true;
  EXPECT_TRUE(gate.should_log(value, epoch + 106s));
  value.holds = 0;
  EXPECT_TRUE(gate.should_log(value, epoch + 108s));
}

TEST(LowBandwidthController, SparseFreshHighRttPreservesRouteEvidenceWithoutTreatingSilenceAsRecovery) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 4000, epoch);
  for (unsigned tick = 1; tick <= 32; ++tick) {
    controller.observe({40, tick, 1}, epoch + tick * 500ms, 0);
  }
  ASSERT_EQ(controller.target(), 100U);
  for (unsigned tick = 33; tick <= 480; ++tick) {
    const auto token = 32 + (tick - 32) / 2;
    controller.observe({100, token}, epoch + tick * 500ms, 0);
    if (tick <= 60) {
      EXPECT_EQ(controller.target(), 100U);
      EXPECT_EQ(controller.diagnostics().baseline_revision, 0U);
    }
  }
  EXPECT_EQ(controller.diagnostics().baseline_rtt_ms, 100U);
  EXPECT_EQ(controller.diagnostics().baseline_revision, 1U);
  EXPECT_EQ(controller.target(), 4000U);
}

TEST(LowBandwidthController, ExtremeRttDoesNotOverflowTheLatencyThreshold) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 500, epoch);
  for (unsigned tick = 1; tick <= 24; ++tick) {
    controller.observe({std::numeric_limits<std::uint32_t>::max(), tick}, epoch + tick * 500ms, 0);
  }
  EXPECT_EQ(controller.target(), 500U);
  EXPECT_EQ(controller.diagnostics().holds, 0U);
}

TEST(LowBandwidthMobileRecovery, MobileJitterAndIsolatedDecoderRequestsKeepTheCeiling) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 5000, epoch);
  for (unsigned tick = 1; tick <= 240; ++tick) {
    low::sample_t sample {43 + (tick % 5) * 10, tick};
    sample.rtt_variance_ms = 10;
    sample.frame_loss_requests = tick % 40 == 0 ? 1 : 0;
    controller.observe(sample, epoch + tick * 500ms, 500);
    EXPECT_EQ(controller.target(), 5000U);
  }
  EXPECT_EQ(controller.diagnostics().verified_target_kbps, 0U);
}

TEST(LowBandwidthMobileRecovery, RepeatedDecoderRequestsAndIncompleteReportsStillReduce) {
  low::settings_t settings;
  settings.mode = "adaptive";
  for (bool incomplete : {false, true}) {
    low::controller_t controller;
    controller.initialize(settings, 1000, epoch);
    for (unsigned tick = 1; tick <= 50; ++tick) {
      low::sample_t sample {40, tick};
      sample.frame_loss_requests = incomplete ? 0 : 1;
      sample.incomplete_reports = incomplete ? 1 : 0;
      controller.observe(sample, epoch + tick * 500ms, 40);
    }
    EXPECT_EQ(controller.target(), 100U);
    EXPECT_TRUE(controller.diagnostics().holds & low::loss);
    EXPECT_TRUE(controller.diagnostics().holds & low::recovery_request);
    EXPECT_EQ(controller.diagnostics().unrecovered_packets, 0U);
  }
}

TEST(LowBandwidthMobileRecovery, HealthyMediaRestoresNormalQualityWithinThirtySecondsOfTheFloor) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 5000, epoch);
  for (unsigned tick = 1; tick <= 32; ++tick) {
    controller.observe({40, tick, 1}, epoch + tick * 500ms, 0);
  }
  ASSERT_EQ(controller.target(), 100U);
  for (unsigned tick = 33; tick <= 92; ++tick) {
    controller.observe({40, tick}, epoch + tick * 500ms, controller.target());
  }
  EXPECT_GE(controller.target(), 3000U);
  EXPECT_LE(controller.target(), 5000U);
  EXPECT_GT(controller.diagnostics().verified_target_kbps, 0U);
}

TEST(LowBandwidthMobileRecovery, IdleFramesCanRaiseTentativeTargetsWithoutClaimingMeasuredCapacity) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 5000, epoch);
  for (unsigned tick = 1; tick <= 32; ++tick) {
    controller.observe({40, tick, 1}, epoch + tick * 500ms, 0);
  }
  for (unsigned tick = 33; tick <= 140; ++tick) {
    controller.observe({40, tick}, epoch + tick * 500ms, 0);
  }
  EXPECT_EQ(controller.target(), 5000U);
  EXPECT_EQ(controller.diagnostics().verified_target_kbps, 0U);
}

TEST(LowBandwidthMobileRecovery, FailedProbesReturnToThePreviousTargetAndWaitBeforeRetrying) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 5000, epoch);
  for (unsigned tick = 1; tick <= 32; ++tick) {
    controller.observe({40, tick, 1}, epoch + tick * 500ms, 0);
  }
  unsigned attempts = 0;
  unsigned last_rejected = 0;
  for (unsigned tick = 33; tick <= 200; ++tick) {
    const auto before = controller.target();
    controller.observe({40, tick, before > 200 ? 1U : 0U}, epoch + tick * 500ms, std::min(200U, before));
    if (before <= 200 && controller.target() > 200) {
      if (last_rejected) {
        EXPECT_GE(tick - last_rejected, 8U);
      }
      ++attempts;
    }
    if (before > 200 && controller.target() <= 200) {
      EXPECT_EQ(controller.target(), 200U);
      last_rejected = tick;
    }
    EXPECT_LE(controller.target(), 400U);
  }
  EXPECT_GT(attempts, 2U);
  EXPECT_LE(attempts, 18U);
}

TEST(LowBandwidthMobileRecovery, DelayOnlyBackoffCannotCascadeToTheFloor) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 5000, epoch);
  controller.observe({40, 1}, epoch + 500ms, 0);
  for (unsigned tick = 2; tick <= 120; ++tick) {
    controller.observe({180, tick}, epoch + tick * 500ms, 0);
    EXPECT_GE(controller.target(), 4050U);
  }
  EXPECT_EQ(controller.target(), 5000U);
  EXPECT_EQ(controller.diagnostics().baseline_rtt_ms, 180U);
}

TEST(LowBandwidthMobileRecovery, ShortRttExcursionsPauseGrowthWithoutCuttingTheBudget) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 5000, epoch);
  controller.observe({40, 1}, epoch + 500ms, 0);
  for (unsigned tick = 2; tick <= 40; ++tick) {
    controller.observe({tick % 4 == 0 ? 130U : 40U, tick}, epoch + tick * 500ms, 500);
    EXPECT_EQ(controller.target(), 5000U);
  }
}

TEST(LowBandwidthMobileRecovery, IsolatedDecoderRequestsDoNotRepeatedlyRestartAHealthyProbeSequence) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 5000, epoch);
  for (unsigned tick = 1; tick <= 32; ++tick) {
    controller.observe({43, tick, 1}, epoch + tick * 500ms, 0);
  }
  for (unsigned tick = 33; tick <= 112; ++tick) {
    low::sample_t sample {43 + (tick % 5) * 8, tick};
    sample.rtt_variance_ms = 10;
    sample.frame_loss_requests = tick % 30 == 0 ? 1 : 0;
    controller.observe(sample, epoch + tick * 500ms, controller.target() / 2);
  }
  EXPECT_GE(controller.target(), 3000U);
}

TEST(LowBandwidthRouteWindow, ExpiresOldRoutesBoundsStorageAndRejectsNoisyEvidence) {
  low::route_window_t window;
  EXPECT_FALSE(window.baseline(epoch, 0));
  window.observe(epoch, 20);
  EXPECT_FALSE(window.baseline(epoch + 9s, 0));
  for (unsigned tick = 1; tick <= 140; ++tick) {
    window.observe(epoch + tick * 500ms, tick % 2 ? 43 : 83);
  }
  ASSERT_TRUE(window.baseline(epoch + 70s, 10));
  EXPECT_EQ(*window.baseline(epoch + 70s, 10), 43U);
  window.reset();
  for (unsigned tick = 1; tick <= 64; ++tick) {
    window.observe(epoch + tick * 500ms, tick % 2 ? 80 : 200);
  }
  EXPECT_FALSE(window.baseline(epoch + 32s, 0));
  window.reset();
  EXPECT_FALSE(window.baseline(epoch + 100s, 0));
}

TEST(LowBandwidthProfile, HysteresisKeepsNormalQualityAndAudioAcrossTheThreeMegabitBoundary) {
  const auto normal = low::make_profile({}, 5000, 30, 1432, 2, 20, 500);
  const auto near = low::make_profile({}, 2952, 30, 1432, 2, 20, 500, &normal);
  EXPECT_FALSE(near.coarse_background);
  EXPECT_TRUE(near.audio_enabled);
  EXPECT_FALSE(low::focus_settings({}, near).enabled);
  const auto low = low::make_profile({}, 2699, 30, 1432, 2, 20, 500, &near);
  EXPECT_TRUE(low.coarse_background);
  EXPECT_FALSE(low.audio_enabled);
  const auto rising = low::make_profile({}, 2952, 30, 1432, 2, 20, 500, &low);
  EXPECT_TRUE(rising.coarse_background);
  EXPECT_FALSE(rising.audio_enabled);
  EXPECT_TRUE(low::focus_settings({}, rising).enabled);
  const auto recovered = low::make_profile({}, 3000, 30, 1432, 2, 20, 500, &rising);
  EXPECT_FALSE(recovered.coarse_background);
  EXPECT_TRUE(recovered.audio_enabled);
  const auto no_audio = low::make_profile({}, 3000, 30, 1432, 2, 20, 2500, &normal);
  EXPECT_FALSE(no_audio.audio_enabled);
}

TEST(LowBandwidthMobileRecovery, SparseFreshHighRttStillTriggersBoundedDelayBackoff) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 5000, epoch);
  controller.observe({40, 1}, epoch + 500ms, 0);
  for (unsigned tick = 2; tick <= 22; ++tick) {
    controller.observe({180, 1 + tick / 2}, epoch + tick * 500ms, 0);
  }
  EXPECT_EQ(controller.target(), 4050U);
}

TEST(LowBandwidthMobileRecovery, LoadedProbeEvidenceExpiresDuringIdleStreaming) {
  low::settings_t settings;
  settings.mode = "adaptive";
  low::controller_t controller;
  controller.initialize(settings, 1000, epoch);
  for (unsigned tick = 1; tick <= 16; ++tick) {
    controller.observe({40, tick, 1}, epoch + tick * 500ms, 0);
  }
  for (unsigned tick = 17; tick <= 80; ++tick) {
    controller.observe({40, tick}, epoch + tick * 500ms, controller.target());
  }
  ASSERT_GT(controller.diagnostics().verified_target_kbps, 0U);
  for (unsigned tick = 81; tick <= 160; ++tick) {
    controller.observe({40, tick}, epoch + tick * 500ms, 0);
  }
  EXPECT_EQ(controller.diagnostics().verified_target_kbps, 0U);
  EXPECT_EQ(controller.target(), 1000U);
}

TEST(LowBandwidthRouteWindow, MoreFrequentFreshSamplesStillUseFixedStorage) {
  low::route_window_t window;
  for (unsigned tick = 1; tick <= 100; ++tick) {
    window.observe(epoch + tick * 100ms, 40);
  }
  EXPECT_FALSE(window.baseline(epoch + 10s, 0));
}
