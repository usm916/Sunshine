/**
 * @file src/audio.h
 * @brief Declarations for audio capture and encoding.
 */
#pragma once

// local includes
#include "platform/common.h"
#include "thread_safe.h"
#include "utility.h"

#include <atomic>
#include <bitset>
#include <cstdint>
#include <memory>
#include <vector>

namespace audio {
  /**
   * @brief Shared audio gate whose generation invalidates samples queued before a pause.
   */
  struct runtime_t {
    std::atomic<std::uint64_t> generation {0};  ///< Odd generations permit audio; even generations mute it.

    /**
     * @brief Change the gate without reviving audio from an earlier active generation.
     * @param enabled Whether new samples may be captured and transmitted.
     */
    void set_enabled(bool enabled) {
      const auto previous = generation.load(std::memory_order_acquire);
      if (static_cast<bool>(previous & 1) != enabled) {
        generation.store(previous + 1, std::memory_order_release);
      }
    }
  };

  /**
   * @brief Supported Opus channel layouts advertised to Moonlight clients.
   */
  enum stream_config_e : int {
    STEREO,  ///< Stereo
    HIGH_STEREO,  ///< High stereo
    SURROUND51,  ///< Surround 5.1
    HIGH_SURROUND51,  ///< High surround 5.1
    SURROUND71,  ///< Surround 7.1
    HIGH_SURROUND71,  ///< High surround 7.1
    MAX_STREAM_CONFIG  ///< Maximum audio stream configuration
  };

  /**
   * @brief Static Opus encoder layout for one advertised audio mode.
   */
  struct opus_stream_config_t {
    std::int32_t sampleRate;  ///< Opus sample rate in hertz.
    int channelCount;  ///< Number of audio channels in the Opus layout.
    int streams;  ///< Number of Opus streams in the layout.
    int coupledStreams;  ///< Number of stereo-coupled Opus streams.
    const std::uint8_t *mapping;  ///< Channel mapping table passed to the Opus encoder.
    int bitrate;  ///< Target bitrate in bits per second.
  };

  /**
   * @brief Custom Opus channel layout supplied by configuration.
   */
  struct stream_params_t {
    int channelCount;  ///< Number of audio channels in the Opus layout.
    int streams;  ///< Number of Opus streams in the custom layout.
    int coupledStreams;  ///< Number of stereo-coupled Opus streams.
    std::uint8_t mapping[8];  ///< Channel mapping table for up to eight speakers.
  };

  extern opus_stream_config_t stream_configs[MAX_STREAM_CONFIG];

  /**
   * @brief Audio capture and encoder settings for a stream.
   */
  struct config_t {
    /**
     * @brief Boolean audio feature flags.
     */
    enum flags_e : int {
      HIGH_QUALITY,  ///< High quality audio
      HOST_AUDIO,  ///< Host audio
      CUSTOM_SURROUND_PARAMS,  ///< Custom surround parameters
      CONTINUOUS_AUDIO,  ///< Continuous audio
      MAX_FLAGS  ///< Maximum number of flags
    };

    int packetDuration;  ///< Packet duration in milliseconds requested by the client.
    int channels;  ///< Number of audio channels requested by the client.
    int mask;  ///< Speaker mask describing the requested channel layout.

    stream_params_t customStreamParams;  ///< Custom Opus layout used when CUSTOM_SURROUND_PARAMS is enabled.

    std::bitset<MAX_FLAGS> flags;  ///< Enabled audio feature flags.
    bool enabled = true;  ///< Whether this session may capture, encode and send audio.
    std::shared_ptr<runtime_t> runtime;  ///< Optional encoder-controlled gate retained across pauses.
  };

  /**
   * @brief Read the current audio generation while respecting the session's permanent mute.
   * @param config Negotiated audio permissions and optional runtime gate.
   * @return Odd active generation, or an even generation when muted.
   */
  inline std::uint64_t active_generation(const config_t &config) {
    return !config.enabled ? 0 : config.runtime ? config.runtime->generation.load(std::memory_order_acquire) :
                                                  1;
  }

  /**
   * @brief Reject samples and packets captured before the most recent audio resume.
   * @param config Session audio permissions and runtime gate.
   * @param generation Generation attached to the captured samples.
   * @return True only while the same active generation is still permitted.
   */
  inline bool generation_is_active(const config_t &config, std::uint64_t generation) {
    return (generation & 1) && active_generation(config) == generation;
  }

  /**
   * @brief Reserve Opus data, audio parity, encryption padding and conservative IP headers.
   * @param config Negotiated layout and packet duration; the temporary gate is ignored.
   * @return Potential audio transport allowance in kilobits per second, or zero for permanent mute.
   */
  std::uint32_t transport_reservation_kbps(const config_t &config);

  /**
   * @brief Reserve the legacy audio bandwidth allowance only for audio-enabled sessions.
   * @param config Session audio settings, including the trimming policy.
   * @return Bandwidth allowance in kilobits per second.
   */
  inline int bandwidth_reservation_kbps(const config_t &config) {
    return (active_generation(config) & 1) ? (config.flags[config_t::HIGH_QUALITY] ? 256 : 96) * config.channels : 0;
  }

  /**
   * @brief Shared audio device state used while streams are active.
   */
  struct audio_ctx_t {
    // We want to change the sink for the first stream only
    std::unique_ptr<std::atomic_bool> sink_flag;  ///< Tracks whether the capture sink was already switched.

    std::unique_ptr<platf::audio_control_t> control;  ///< Platform audio-control implementation.

    bool restore_sink;  ///< Whether Sunshine should restore the original sink when capture ends.
    platf::sink_t sink;  ///< Original sink captured before Sunshine switched devices.
  };

  /**
   * @brief Byte buffer used for encoded audio packet payloads.
   */
  using buffer_t = util::buffer_t<std::uint8_t>;

  /**
   * @brief Encoded audio packet paired with platform channel metadata.
   */
  struct packet_t {
    void *first;  ///< Session pointer used by the global sender.
    buffer_t second;  ///< Encoded Opus payload.
    std::uint64_t generation = 1;  ///< Capture generation used to reject stale queued audio.
  };

  /**
   * @brief Captured PCM data tagged with the active audio generation.
   */
  struct sample_t {
    std::vector<float> data;  ///< Interleaved PCM samples.
    std::uint64_t generation;  ///< Generation observed before sampling.
  };

#ifdef SUNSHINE_TESTS
  namespace test_hooks {
    /**
     * @brief Run the production Opus worker with isolated synthetic samples and packet output.
     * @param samples PCM input queue, stopped by the test to finish the worker.
     * @param config Negotiated audio layout and shared gate.
     * @param packets Private encoded output queue.
     */
    void encode_samples(std::shared_ptr<safe::queue_t<sample_t>> samples, config_t config, std::shared_ptr<safe::queue_t<packet_t>> packets);
  }  // namespace test_hooks
#endif
  /**
   * @brief Shared mailbox reference to the global audio context.
   */
  using audio_ctx_ref_t = safe::shared_t<audio_ctx_t>::ptr_t;

  /**
   * @brief Capture, encode, and publish audio packets for a stream.
   *
   * @param mail Mailbox used to publish encoded audio packets.
   * @param config Audio capture and encoder settings.
   * @param channel_data Platform-specific capture channel pointer.
   */
  void capture(safe::mail_t mail, config_t config, void *channel_data);

  /**
   * @brief Get the reference to the audio context.
   * @returns A shared pointer reference to audio context.
   * @note Aside from the configuration purposes, it can be used to extend the
   *       audio sink lifetime to capture sink earlier and restore it later.
   *
   * @examples
   * audio_ctx_ref_t audio = get_audio_ctx_ref()
   * @examples_end
   */
  audio_ctx_ref_t get_audio_ctx_ref();

  /**
   * @brief Check if the audio sink held by audio context is available.
   * @returns True if available (and can probably be restored), false otherwise.
   * @note Useful for delaying the release of audio context shared pointer (which
   *       tries to restore original sink).
   *
   * @examples
   * audio_ctx_ref_t audio = get_audio_ctx_ref()
   * if (audio.get()) {
   *     return is_audio_ctx_sink_available(*audio.get());
   * }
   * return false;
   * @examples_end
   * @param ctx Native context object used by the operation or callback.
   */
  bool is_audio_ctx_sink_available(const audio_ctx_t &ctx);
}  // namespace audio
