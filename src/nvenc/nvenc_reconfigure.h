/**
 * @file src/nvenc/nvenc_reconfigure.h
 * @brief Declarations for atomic NVENC bitrate reconfiguration state.
 */
#pragma once

// standard includes
#include <cstdint>
#include <functional>

// local includes
#include "nvenc_sdk.h"
#include "src/video.h"

namespace NVENC_NAMESPACE {

  /**
   * @brief Own the last successful NVENC configuration used for bitrate-only updates.
   */
  class bitrate_reconfigure_state_t {
  public:
    /**
     * @brief Callback that applies one prepared NVENC reconfiguration request.
     */
    using apply_function_t = std::function<NVENCSTATUS(NV_ENC_RECONFIGURE_PARAMS *)>;

    /**
     * @brief Initialize state from the configuration accepted by NVENC.
     *
     * @param supported Whether the driver advertises dynamic bitrate support.
     * @param init_params Successful encoder initialization parameters.
     * @param encode_config Successful encoder configuration.
     */
    void initialize(
      bool supported,
      const NV_ENC_INITIALIZE_PARAMS &init_params,
      const NV_ENC_CONFIG &encode_config
    );

    /**
     * @brief Reset all reconfiguration capability and cached encoder state.
     */
    void reset();

    /**
     * @brief Atomically apply bitrate and optional frame rate while preserving reference state.
     *
     * @param target_kbps Requested bitrate in kilobits per second.
     * @param apply Function that invokes the driver reconfiguration API.
     * @param fps Encoder frequency, or zero to preserve the existing rate.
     * @return Detailed result including the effective target after the request.
     */
    video::bitrate_reconfigure_result_t reconfigure(
      std::uint32_t target_kbps,
      const apply_function_t &apply,
      std::uint32_t fps = 0
    );

  private:
    bool supported_ = false;  ///< Whether the active driver supports dynamic bitrate changes.
    NV_ENC_INITIALIZE_PARAMS init_params_ {};  ///< Last successful encoder initialization parameters.
    NV_ENC_CONFIG encode_config_ {};  ///< Last successful encoder configuration.
    std::uint32_t baseline_fps_num_ = 0;  ///< Initial encoder FPS numerator for per-frame VBV scaling.
    std::uint32_t baseline_fps_den_ = 0;  ///< Initial encoder FPS denominator.
    std::uint32_t baseline_average_bitrate_ = 0;  ///< Initial average bitrate used as the scaling denominator.
    std::uint32_t baseline_vbv_buffer_size_ = 0;  ///< Initial VBV size used to avoid cumulative rounding drift.
    std::uint32_t baseline_vbv_initial_delay_ = 0;  ///< Initial VBV delay used to avoid cumulative rounding drift.
  };

}  // namespace NVENC_NAMESPACE
