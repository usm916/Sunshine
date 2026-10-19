/**
 * @file tests/unit/test_audio_runtime.cpp
 * @brief Reconnect, live audio gating, transport reservation and real Opus recovery regressions.
 */
#include "../tests_common.h"

#include <limits>
#include <opus/opus_multistream.h>
#include <src/audio.h>
#include <src/config.h>
#include <src/video.h>

using namespace std::chrono_literals;

TEST(AudioRuntimeTest, PauseAndResumeInvalidateOldGenerationsWithoutOverridingPermanentMute) {
  audio::config_t config {};
  EXPECT_EQ(audio::active_generation(config), 1);
  config.runtime = std::make_shared<audio::runtime_t>();
  EXPECT_EQ(audio::active_generation(config), 0);
  EXPECT_FALSE(audio::generation_is_active(config, 0));
  config.runtime->set_enabled(true);
  const auto first = audio::active_generation(config);
  EXPECT_EQ(first, 1);
  config.runtime->set_enabled(true);
  EXPECT_EQ(audio::active_generation(config), first);
  config.runtime->set_enabled(false);
  EXPECT_FALSE(audio::generation_is_active(config, first));
  config.runtime->set_enabled(true);
  EXPECT_EQ(audio::active_generation(config), 3);
  EXPECT_FALSE(audio::generation_is_active(config, first));
  EXPECT_TRUE(audio::generation_is_active(config, 3));
  config.enabled = false;
  EXPECT_EQ(audio::active_generation(config), 0);
  EXPECT_FALSE(audio::generation_is_active(config, 3));
}

TEST(AudioRuntimeTest, TransportAllowanceIncludesParityAndPacketHeadersEvenWhileTemporarilyMuted) {
  audio::config_t config {};
  config.channels = 2;
  config.packetDuration = 5;
  EXPECT_EQ(audio::transport_reservation_kbps(config), 413);
  config.runtime = std::make_shared<audio::runtime_t>();
  EXPECT_EQ(audio::transport_reservation_kbps(config), 413);
  config.flags[audio::config_t::HIGH_QUALITY] = true;
  EXPECT_EQ(audio::transport_reservation_kbps(config), 1037);
  config.flags[audio::config_t::HIGH_QUALITY] = false;
  config.packetDuration = 20;
  EXPECT_EQ(audio::transport_reservation_kbps(config), 212);
  config.packetDuration = 0;
  EXPECT_EQ(audio::transport_reservation_kbps(config), 1488);
  config.enabled = false;
  EXPECT_EQ(audio::transport_reservation_kbps(config), 0);
}

TEST(AudioRuntimeTest, BudgetReservesAudioOnlyAboveTheThresholdAndWithVideoHeadroom) {
  namespace low = video::low_bandwidth;
  for (const auto budget : {32U, 250U, 1000U, 2999U, 3000U, 5000U}) {
    const auto muted = low::make_profile({}, budget, 30);
    const auto media = low::make_profile({}, budget, 30, 1432, 2, 20, 413);
    EXPECT_EQ(media.wire_kbps, muted.wire_kbps);
    EXPECT_EQ(media.audio_enabled, budget >= 3000);
    if (media.audio_enabled) {
      EXPECT_LT(media.video_kbps, muted.video_kbps);
    } else {
      EXPECT_EQ(media, muted);
    }
  }
  EXPECT_FALSE(low::make_profile({}, 3000, 30, 1432, 2, 20, 2001).audio_enabled);
  EXPECT_TRUE(low::make_profile({}, 3000, 30, 1432, 2, 20, 2000).audio_enabled);
  EXPECT_FALSE(low::make_profile({}, 5000, 30, 1432, 2, 20, std::numeric_limits<std::uint32_t>::max()).audio_enabled);
}

TEST(AudioRuntimeTest, NewConnectionsRestoreAudioAtHighBudgetsAndPreserveExplicitMute) {
  for (const auto mode : {"disabled", "fixed", "adaptive"}) {
    for (const auto budget : {250U, 2999U, 3000U, 5000U}) {
      for (const bool allowed : {false, true}) {
        audio::config_t audio_config {};
        audio_config.channels = 2;
        audio_config.packetDuration = 5;
        video::config_t video_config {};
        video_config.framerate = 30;
        video_config.low_bandwidth.mode = mode;
        video_config.low_profile = video::low_bandwidth::make_profile(video_config.low_bandwidth, budget, 30);
        video_config.cursor_focus = video::low_bandwidth::focus_settings({}, budget);
        video::configure_session_audio(audio_config, video_config, allowed, 1432, 2, 20);
        EXPECT_EQ(static_cast<bool>(audio::active_generation(audio_config) & 1), allowed && budget >= 3000);
        if (video_config.low_bandwidth.mode != "disabled") {
          ASSERT_EQ(audio_config.runtime, video_config.audio_runtime);
          EXPECT_EQ(video_config.audio_reserve_kbps, allowed ? 413 : 0);
        } else {
          EXPECT_FALSE(audio_config.runtime);
          EXPECT_FALSE(video_config.audio_runtime);
          EXPECT_EQ(video_config.audio_reserve_kbps, 0);
        }
      }
    }
  }
}

TEST(AudioRuntimeTest, ReconnectingDoesNotReuseThePreviousConnectionsPausedGate) {
  audio::config_t audio_config {};
  audio_config.channels = 2;
  audio_config.packetDuration = 5;
  video::config_t video_config {};
  video_config.framerate = 30;
  video_config.low_bandwidth.mode = "adaptive";
  video_config.low_profile = video::low_bandwidth::make_profile(video_config.low_bandwidth, 5000, 30);
  video::configure_session_audio(audio_config, video_config, true, 1432, 2, 20);
  const auto old_gate = audio_config.runtime;
  ASSERT_TRUE(audio::active_generation(audio_config) & 1);
  old_gate->set_enabled(false);
  video::configure_session_audio(audio_config, video_config, true, 1432, 2, 20);
  EXPECT_NE(audio_config.runtime, old_gate);
  EXPECT_EQ(old_gate->generation.load() & 1, 0);
  EXPECT_TRUE(audio::active_generation(audio_config) & 1);
  video_config.low_bandwidth.mode = "disabled";
  video_config.cursor_focus.enabled = false;
  video::configure_session_audio(audio_config, video_config, true, 1432, 2, 20);
  EXPECT_FALSE(audio_config.runtime);
  EXPECT_TRUE(audio::active_generation(audio_config) & 1);
}

TEST(AudioRuntimeTest, RealOpusWorkerResumesAndRejectsQueuedAudioFromBeforeThePause) {
  audio::config_t config {};
  config.channels = 2;
  config.packetDuration = 5;
  config.runtime = std::make_shared<audio::runtime_t>();
  auto samples = std::make_shared<safe::queue_t<audio::sample_t>>(16);
  auto packets = std::make_shared<safe::queue_t<audio::packet_t>>(16);
  std::jthread worker([&] {
    audio::test_hooks::encode_samples(samples, config, packets);
  });
  auto cleanup = util::fail_guard([&] {
    samples->stop();
    worker.join();
  });
  samples->raise(std::vector<float>(480, 0.1f), 0);
  EXPECT_FALSE(packets->pop(25ms));
  config.runtime->set_enabled(true);
  samples->raise(std::vector<float>(480, 0.1f), 1);
  auto first = packets->pop(1s);
  ASSERT_TRUE(first);
  ASSERT_GT(first->second.size(), 0);
  EXPECT_EQ(first->generation, 1);
  config.runtime->set_enabled(false);
  samples->raise(std::vector<float>(480, 0.1f), 1);
  samples->raise(std::vector<float>(480, 0.1f), 2);
  EXPECT_FALSE(packets->pop(25ms));
  config.runtime->set_enabled(true);
  samples->raise(std::vector<float>(480, 0.1f), 1);
  samples->raise(std::vector<float>(480, 0.2f), 3);
  auto resumed = packets->pop(1s);
  ASSERT_TRUE(resumed);
  EXPECT_EQ(resumed->generation, 3);
  EXPECT_FALSE(audio::generation_is_active(config, first->generation));
  ASSERT_GT(resumed->second.size(), 0);
  int error = OPUS_OK;
  auto decoder = opus_multistream_decoder_create(48000, 2, 1, 1, platf::speaker::map_stereo.data(), &error);
  ASSERT_EQ(error, OPUS_OK);
  ASSERT_NE(decoder, nullptr);
  auto free_decoder = util::fail_guard([&] {
    opus_multistream_decoder_destroy(decoder);
  });
  std::vector<float> decoded(480);
  EXPECT_EQ(opus_multistream_decode_float(decoder, std::begin(first->second), first->second.size(), decoded.data(), 240, 0), 240);
  EXPECT_EQ(opus_multistream_decode_float(decoder, std::begin(resumed->second), resumed->second.size(), decoded.data(), 240, 0), 240);
  EXPECT_FALSE(packets->pop(25ms));
}

TEST(AudioRuntimeTest, MutedRuntimeWaitsForShutdownWithoutStartingCapture) {
  const auto original = config::audio.stream;
  auto restore = util::fail_guard([&] {
    config::audio.stream = original;
  });
  config::audio.stream = true;
  auto session_mail = std::make_shared<safe::mail_raw_t>();
  auto shutdown = session_mail->event<bool>(mail::shutdown);
  audio::config_t config {};
  config.runtime = std::make_shared<audio::runtime_t>();
  std::atomic_bool done {false};
  std::jthread worker([&] {
    audio::capture(session_mail, config, nullptr);
    done.store(true);
  });
  std::this_thread::sleep_for(25ms);
  EXPECT_FALSE(done.load());
  shutdown->raise(true);
  worker.join();
  EXPECT_TRUE(done.load());
}
