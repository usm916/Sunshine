/**
 * @file src/video_monitor_switch.h
 * @brief CPU-only monitor edge selection, stationary dwell, and recoverable capture transitions.
 */
#pragma once

#include "video_viewport.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace video::monitor_switch {
  /**
   * @brief Physical desktop rectangle with exclusive right/bottom bounds.
   */
  struct rect_t {
    int left;  ///< Physical left edge, possibly negative.
    int top;  ///< Physical top edge, possibly negative.
    int right;  ///< Exclusive right edge.
    int bottom;  ///< Exclusive bottom edge.
    /**
     * @brief Compare physical rectangles.
     * @return Whether all bounds match.
     */
    bool operator==(const rect_t &) const = default;
  };

  /**
   * @brief One attached Windows monitor identified by its stable capture name.
   */
  struct monitor_t {
    std::string name;  ///< Capture output name.
    rect_t bounds;  ///< Physical desktop bounds, independent of DPI scaling.
  };

  /**
   * @brief Switch destination and cursor landing point in physical desktop coordinates.
   */
  struct request_t {
    std::string name;  ///< Target capture output.
    std::pair<int, int> point;  ///< Cursor landing point just inside the new monitor.
    /**
     * @brief Compare a switch request.
     * @return Whether destination and point match.
     */
    bool operator==(const request_t &) const = default;
  };

  /**
   * @brief Find the nearest monitor reached from the middle fifth of a visible outer edge.
   * @param monitors Attached monitor candidates in physical desktop coordinates.
   * @param current Current capture output name.
   * @param crop Last committed crop; inactive means the entire monitor.
   * @param mouse Physical desktop cursor position.
   * @return Target and landing point, or none away from an eligible edge or without a neighbor.
   */
  inline std::optional<request_t> target_at_edge(std::span<const monitor_t> monitors, const std::string &current, const viewport::frame_t &crop, std::pair<int, int> mouse) {
    const auto source = std::find_if(monitors.begin(), monitors.end(), [&](const auto &monitor) {
      return monitor.name == current;
    });
    if (source == monitors.end()) {
      return std::nullopt;
    }
    const auto bounds = source->bounds;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0 || (crop.active && (crop.source_width != width || crop.source_height != height))) {
      return std::nullopt;
    }
    const rect_t view = crop.active ? rect_t {bounds.left + crop.x, bounds.top + crop.y, bounds.left + crop.x + crop.width, bounds.top + crop.y + crop.height} : bounds;
    const auto [x, y] = mouse;
    if (view.right <= view.left || view.bottom <= view.top || x < view.left || x >= view.right || y < view.top || y >= view.bottom) {
      return std::nullopt;
    }
    const auto centered = [](int coordinate, int first, int last) {
      const auto offset = static_cast<std::int64_t>(coordinate) - first;
      const auto length = static_cast<std::int64_t>(last) - first;
      return offset >= length * 2 / 5 && offset < std::max(length * 2 / 5 + 1, length * 3 / 5);
    };
    // Left, right, top, bottom. An internal crop edge never changes monitors.
    const std::array eligible {
      view.left == bounds.left && x - view.left < 20 && centered(y, view.top, view.bottom),
      view.right == bounds.right && view.right - 1 - x < 20 && centered(y, view.top, view.bottom),
      view.top == bounds.top && y - view.top < 20 && centered(x, view.left, view.right),
      view.bottom == bounds.bottom && view.bottom - 1 - y < 20 && centered(x, view.left, view.right),
    };
    std::optional<request_t> result;
    std::int64_t nearest = std::numeric_limits<std::int64_t>::max();
    for (const auto &monitor : monitors) {
      const auto b = monitor.bounds;
      if (monitor.name == current || b.right <= b.left || b.bottom <= b.top) {
        continue;
      }
      for (int edge = 0; edge < 4; ++edge) {
        if (!eligible[edge]) {
          continue;
        }
        const bool horizontal = edge < 2;
        const bool aligned = horizontal ? (y >= b.top && y < b.bottom) : (x >= b.left && x < b.right);
        const auto gap = edge == 0 ? static_cast<std::int64_t>(bounds.left) - b.right : edge == 1 ? static_cast<std::int64_t>(b.left) - bounds.right :
                                                                                      edge == 2   ? static_cast<std::int64_t>(bounds.top) - b.bottom :
                                                                                                    static_cast<std::int64_t>(b.top) - bounds.bottom;
        if (!aligned || gap < 0 || gap >= nearest) {
          continue;
        }
        const int inset_x = std::min(21, (b.right - b.left - 1) / 2);
        const int inset_y = std::min(21, (b.bottom - b.top - 1) / 2);
        const auto point = edge == 0 ? std::pair {b.right - 1 - inset_x, y} : edge == 1 ? std::pair {b.left + inset_x, y} :
                                                                            edge == 2   ? std::pair {x, b.bottom - 1 - inset_y} :
                                                                                          std::pair {x, b.top + inset_y};
        result = request_t {monitor.name, point};
        nearest = gap;
      }
    }
    return result;
  }

  /**
   * @brief Capture properties that must remain compatible with the negotiated encoder.
   */
  struct capture_key_t {
    std::uint64_t adapter;  ///< DXGI adapter LUID packed into an unsigned integer.
    bool hdr;  ///< Source HDR state used to choose the stream colorspace.
    /**
     * @brief Compare device and colorspace requirements.
     * @return Whether both requirements match.
     */
    bool operator==(const capture_key_t &) const = default;
  };

  /**
   * @brief Reject unknown, cross-GPU or mixed HDR/SDR automatic targets before cursor movement.
   * @param source Original capture identity.
   * @param target Newly opened capture identity.
   * @return Whether automatic switching can reuse the negotiated encoder capabilities.
   */
  inline bool compatible(const std::optional<capture_key_t> &source, const std::optional<capture_key_t> &target) {
    return source && target && *source == *target;
  }

  /**
   * @brief One stationary edge dwell, with a release latch after each activation.
   */
  class dwell_t {
  public:
    /**
     * @brief Update a dwell using CPU state only; movement greater than four pixels restarts it.
     * @param target Current eligible target, or none to cancel and rearm.
     * @param mouse Physical cursor position before any automatic warp.
     * @param now Monotonic observation time.
     * @param delay Required stationary time; clamped to 100..5000 ms.
     * @return One switch request after the dwell, until the pointer leaves the activation area.
     */
    std::optional<request_t> update(const std::optional<request_t> &target, std::pair<int, int> mouse, std::chrono::steady_clock::time_point now, std::chrono::milliseconds delay) {
      if (!target) {
        pending.reset();
        latched = false;
        return std::nullopt;
      }
      if (latched) {
        return std::nullopt;
      }
      if (!pending || pending->name != target->name || std::abs(static_cast<std::int64_t>(mouse.first) - origin.first) > 4 || std::abs(static_cast<std::int64_t>(mouse.second) - origin.second) > 4 || now < since) {
        pending = target;
        origin = mouse;
        since = now;
        return std::nullopt;
      }
      if (now - since < std::clamp(delay, std::chrono::milliseconds(100), std::chrono::milliseconds(5000))) {
        return std::nullopt;
      }
      latched = true;
      return target;
    }

  private:
    std::optional<request_t> pending;  ///< Target currently being timed.
    std::pair<int, int> origin {};  ///< Initial stationary pointer position.
    std::chrono::steady_clock::time_point since {};  ///< Dwell start time.
    bool latched = false;  ///< Prevents repeated requests while still in an edge area.
  };

  /**
   * @brief Result of a bounded target attempt and optional restoration.
   */
  struct transition_t {
    std::string selected;  ///< Output that was opened or restored.
    bool ready;  ///< Whether capture initialization succeeded.
    bool switched;  ///< Whether target initialization and cursor landing both succeeded.
  };

  /**
   * @brief Try a target once and restore the original output if capture or cursor landing fails.
   * @param request Desired output and cursor position.
   * @param current Original capture output.
   * @param available Refreshed capturable output names.
   * @param open Capture factory returning success for an output name; each call may retry internally.
   * @param warp Cursor setter invoked only after successful target capture initialization.
   * @return Selected output, readiness and switch outcome; transport sessions remain owned by the caller.
   */
  template<class Open, class Warp>
  transition_t transition(const request_t &request, const std::string &current, std::span<const std::string> available, Open open, Warp warp) {
    if (request.name != current && std::find(available.begin(), available.end(), request.name) != available.end() && open(request.name) && warp(request.point)) {
      return {request.name, true, true};
    }
    return {current, open(current), false};
  }
}  // namespace video::monitor_switch
