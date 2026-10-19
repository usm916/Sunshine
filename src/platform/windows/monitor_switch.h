/**
 * @file src/platform/windows/monitor_switch.h
 * @brief Low-rate Windows monitor and pointer sampling for optional edge dwell switching.
 */
#pragma once
#include "src/video_monitor_switch.h"

#include <functional>
#include <memory>

namespace platf {
  class display_t;
}

namespace platf::dxgi {
  /**
   * @brief Read a DXGI display's adapter and HDR identity without creating another device.
   * @param display Existing capture object.
   * @return Capture compatibility key, or none for an unsupported or failed query.
   */
  std::optional<::video::monitor_switch::capture_key_t> monitor_capture_key(platf::display_t *display);

  /**
   * @brief Enumerate attached monitors in physical desktop coordinates.
   * @return Current layout, or empty on failure.
   */
  std::vector<::video::monitor_switch::monitor_t> read_monitor_layout();
  /**
   * @brief Read the physical mouse position without reading UI contents.
   * @return Cursor coordinates, or none on desktop access failure.
   */
  std::optional<std::pair<int, int>> read_monitor_cursor();
  /**
   * @brief Place the cursor on a newly initialized monitor.
   * @param point Physical landing position.
   * @return Whether Windows accepted the position.
   */
  bool place_monitor_cursor(std::pair<int, int> point);

  /**
   * @brief Injectable native sampling operations for deterministic controller tests.
   */
  struct monitor_switch_api_t {
    std::function<std::vector<::video::monitor_switch::monitor_t>()> layout = read_monitor_layout;  ///< Attached monitor query.
    std::function<std::optional<std::pair<int, int>>()> cursor = read_monitor_cursor;  ///< Physical mouse query.
  };

  /**
   * @brief One stream's low-rate edge dwell controller; contains no GPU work.
   */
  class monitor_switch_t {
  public:
    /**
     * @brief Create a controller with native or test queries.
     * @param api Native queries, replaceable in tests.
     */
    explicit monitor_switch_t(monitor_switch_api_t api = {}):
        api(std::move(api)) {}

    /**
     * @brief Poll only a single enabled crop session; pressed direct contacts cancel dwell.
     * @param current Captured monitor's Windows device name.
     * @param viewport Shared crop policy, submitted frame and direct-contact state.
     * @param sessions Active sessions sharing the capture loop.
     * @param now Monotonic time, injectable for tests.
     * @return One requested monitor change after a stationary edge dwell.
     */
    std::optional<::video::monitor_switch::request_t> poll(const std::string &current, const std::shared_ptr<::video::viewport::state_t> &viewport, std::size_t sessions, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

  private:
    monitor_switch_api_t api;  ///< Native sampling operations.
    ::video::monitor_switch::dwell_t dwell;  ///< Stationary timer and repeat latch.
    std::vector<::video::monitor_switch::monitor_t> monitors;  ///< Cached native topology.
    std::chrono::steady_clock::time_point next_poll {};  ///< Pointer sampled at most 40 Hz.
    std::chrono::steady_clock::time_point next_layout {};  ///< Layout sampled at most twice per second.
  };
}  // namespace platf::dxgi
