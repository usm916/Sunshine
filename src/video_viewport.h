/**
 * @file src/video_viewport.h
 * @brief Per-session native-pixel viewport tracking and shared absolute-input mapping.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <utility>

namespace video::viewport {
  /**
   * @brief Optional crop policy independent of background simplification.
   */
  struct settings_t {
    bool enabled = false;  ///< Crop oversized desktops instead of scaling the entire monitor.
    int deadzone_width = 20;  ///< Left/right edge switch band in pixels; zero selects 20, limited to prevent overlap oscillation.
    int deadzone_height = 20;  ///< Top/bottom edge switch band in pixels; zero selects 20, limited to prevent overlap oscillation.
    bool monitor_switch = false;  ///< Opt in to stationary edge-center monitor changes for one active stream.
    int monitor_switch_delay = 600;  ///< Required stationary dwell in milliseconds, validated to 100..5000.
  };

  /**
   * @brief One committed viewport in monitor-local physical pixels.
   */
  struct frame_t {
    int x = 0;  ///< Left edge within the captured monitor.
    int y = 0;  ///< Top edge within the captured monitor.
    int width = 0;  ///< Visible source width.
    int height = 0;  ///< Visible source height.
    int source_width = 0;  ///< Full monitor width.
    int source_height = 0;  ///< Full monitor height.
    int output_width = 0;  ///< Requested encoded width.
    int output_height = 0;  ///< Requested encoded height.
    bool active = false;  ///< True only when at least one source dimension exceeds the request.
    /**
     * @brief Compare submitted viewport geometry.
     * @return Whether every viewport field matches.
     */
    bool operator==(const frame_t &) const = default;
  };

  /**
   * @brief Count fixed crop positions needed to cover one source axis.
   * @param total Source dimension in physical pixels.
   * @param length Visible dimension in physical pixels.
   * @return Floor of source divided by visible size plus one when oversized, capped to unique pixel origins; zero for invalid dimensions.
   */
  inline int section_count(int total, int length) {
    if (total <= 0 || length <= 0) {
      return 0;
    }
    if (total <= length) {
      return 1;
    }
    return length == 1 ? total : std::min(1 + total / length, total - length + 1);
  }

  /**
   * @brief Place a crop section evenly between the two source edges.
   * @param total Source dimension in physical pixels.
   * @param length Visible dimension in physical pixels.
   * @param index Zero-based section, clamped to the available positions.
   * @return Integer crop origin; zero when cropping is unnecessary or invalid.
   */
  inline int section_origin(int total, int length, int index) {
    const int count = section_count(total, length);
    return count > 1 ? static_cast<int>(static_cast<std::int64_t>(total - length) * std::clamp(index, 0, count - 1) / (count - 1)) : 0;
  }

  /**
   * @brief Keep the current section until its anchor enters an edge band, limited to avoid immediate reversal.
   * @param total Valid source dimension in physical pixels.
   * @param length Valid visible dimension, no larger than the source.
   * @param coordinate Anchor position, clamped to the source.
   * @param previous Previously committed origin, or absent on initialization.
   * @param deadzone Edge band width in pixels; zero selects 20. Each side is capped to half its adjacent overlap, with a minimum of one pixel.
   * @return Fixed origin whose crop contains the anchor.
   */
  inline int follow_section(int total, int length, int coordinate, std::optional<int> previous, int deadzone) {
    const int count = section_count(total, length);
    if (count <= 1) {
      return 0;
    }
    coordinate = std::clamp(coordinate, 0, total - 1);
    const auto boundary = [&](int index) {
      return static_cast<int>((static_cast<std::int64_t>(section_origin(total, length, index)) + section_origin(total, length, index + 1) + length) / 2);
    };
    if (previous) {
      const int old = std::clamp(*previous, 0, total - length);
      const int span = total - length;
      const int index = static_cast<int>((static_cast<std::int64_t>(old) * (count - 1) + span - 1) / span);
      const int margin = std::clamp(deadzone == 0 ? 20 : deadzone, 1, length);
      const auto edge_band = [&](int overlap) {
        return std::min(margin, std::max(1, overlap / 2));
      };
      const int lower = index == 0 ? 0 : old + edge_band(section_origin(total, length, index - 1) + length - old);
      const int upper = index == count - 1 ? total : old + length - edge_band(old + length - section_origin(total, length, index + 1));
      if (coordinate >= lower && coordinate < upper) {
        return section_origin(total, length, index);
      }
    }
    int first = 0;
    int last = count - 1;
    while (first < last) {
      const int middle = first + (last - first) / 2;
      if (coordinate < boundary(middle)) {
        last = middle;
      } else {
        first = middle + 1;
      }
    }
    return section_origin(total, length, first);
  }

  /**
   * @brief Follow an anchor by switching between fixed crop sections on each axis.
   * @param previous Previously committed frame, or an empty frame on initialization.
   * @param settings Crop policy and edge switch bands, limited to half the adjacent overlap.
   * @param sw Full source width.
   * @param sh Full source height.
   * @param ow Requested encoded width.
   * @param oh Requested encoded height.
   * @param anchor Optional monitor-local physical position, including a caret when available.
   * @param frozen Keep the current origin while direct contacts are pressed.
   * @return Clamped native-pixel crop, or an inactive frame for invalid or smaller sources.
   */
  inline frame_t track(const frame_t &previous, const settings_t &settings, int sw, int sh, int ow, int oh, std::optional<std::pair<int, int>> anchor, bool frozen = false) {
    frame_t result {0, 0, 0, 0, sw, sh, ow, oh};
    if (!settings.enabled || sw <= 0 || sh <= 0 || ow <= 0 || oh <= 0 || (sw <= ow && sh <= oh)) {
      return result;
    }
    result.width = std::min(sw, ow);
    result.height = std::min(sh, oh);
    result.active = true;
    const bool initialized = previous.active && previous.width == result.width && previous.height == result.height && previous.source_width == sw && previous.source_height == sh && previous.output_width == ow && previous.output_height == oh;
    if (initialized && (frozen || !anchor)) {
      result.x = previous.x;
      result.y = previous.y;
      return result;
    }
    result.x = follow_section(sw, result.width, anchor ? anchor->first : sw / 2, initialized ? std::optional<int>(previous.x) : std::nullopt, settings.deadzone_width);
    result.y = follow_section(sh, result.height, anchor ? anchor->second : sh / 2, initialized ? std::optional<int>(previous.y) : std::nullopt, settings.deadzone_height);
    return result;
  }

  /**
   * @brief Two HLSL rows mapping the rotated full-screen UVs into the crop.
   */
  struct shader_constants_t {
    std::array<float, 2> origin {};  ///< Texture-space upper-left origin.
    std::array<float, 2> scale {1, 1};  ///< Fraction of the original capture to sample.
    float enabled = 0;  ///< Zero leaves the existing shader path unchanged.
    std::array<float, 3> padding {};  ///< Constant-buffer alignment.
  };

  static_assert(sizeof(shader_constants_t) == 32);

  /**
   * @brief Transform monitor-local crop bounds into rotated capture-texture UVs.
   * @param frame Native-pixel viewport.
   * @param rotation Clockwise DXGI quarter turns, from zero to three.
   * @return HLSL constants; invalid inputs disable remapping.
   */
  inline shader_constants_t shader_constants(const frame_t &frame, int rotation) {
    shader_constants_t result;
    if (!frame.active || frame.source_width <= 0 || frame.source_height <= 0 || rotation < 0 || rotation > 3) {
      return result;
    }
    const float x = static_cast<float>(frame.x) / frame.source_width;
    const float y = static_cast<float>(frame.y) / frame.source_height;
    const float w = static_cast<float>(frame.width) / frame.source_width;
    const float h = static_cast<float>(frame.height) / frame.source_height;
    result.origin = {x, y};
    result.scale = {w, h};
    if (rotation == 1) {
      result.origin = {y, 1 - x - w};
      result.scale = {h, w};
    } else if (rotation == 2) {
      result.origin = {1 - x - w, 1 - y - h};
    } else if (rotation == 3) {
      result.origin = {1 - y - h, x};
      result.scale = {h, w};
    }
    result.enabled = 1;
    return result;
  }

  /**
   * @brief Map a client point into the committed native-pixel crop, excluding black padding.
   * @param frame Committed viewport.
   * @param x Client X position expressed in encoded pixels.
   * @param y Client Y position expressed in encoded pixels.
   * @return Monitor-local physical pixel position, clamped to the visible source.
   */
  inline std::pair<float, float> map_point(const frame_t &frame, float x, float y) {
    return {
      frame.x + std::clamp(x - (frame.output_width - frame.width) / 2.0f, 0.0f, static_cast<float>(std::max(0, frame.width - 1))),
      frame.y + std::clamp(y - (frame.output_height - frame.height) / 2.0f, 0.0f, static_cast<float>(std::max(0, frame.height - 1)))
    };
  }

  /**
   * @brief Shared viewport and direct-contact state; each connection owns its own instance.
   */
  struct state_t {
    settings_t settings;  ///< Immutable session policy.
    std::mutex mutex;  ///< Serializes frame submission and direct-input coordinate snapshots.
    frame_t frame;  ///< Last submitted source rectangle; protected by mutex.
    std::unordered_set<std::uint64_t> contacts;  ///< Pressed touch/pen contacts; protected by mutex.

    /**
     * @brief Read the last submitted viewport without racing frame conversion.
     * @return Copied frame.
     */
    frame_t snapshot() {
      const std::lock_guard lock(mutex);
      return frame;
    }

    /**
     * @brief Freeze tracking for pressed contacts, including simultaneous fingers.
     * @param id Contact identifier, with a separate namespace for the pen.
     * @param pressed Whether the contact remains down.
     * @param cancel_all Clear every contact for this session.
     */
    void contact(std::uint64_t id, bool pressed, bool cancel_all = false) {
      const std::lock_guard lock(mutex);
      if (cancel_all) {
        contacts.clear();
      } else if (pressed) {
        contacts.insert(id);
      } else {
        contacts.erase(id);
      }
    }
  };
}  // namespace video::viewport
