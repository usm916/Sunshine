/**
 * @file src/video_cursor_focus_background.h
 * @brief Time-based rolling refresh of a cursor-focus background without frame-wide bursts.
 */
#pragma once

#include "video_cursor_focus.h"

namespace video::cursor_focus {
  /**
   * @brief Half-open bounds in unrotated capture-texture pixels.
   */
  struct pixel_rect_t {
    int left = 0;  ///< Inclusive left edge.
    int top = 0;  ///< Inclusive top edge.
    int right = 0;  ///< Exclusive right edge.
    int bottom = 0;  ///< Exclusive bottom edge.
    /**
     * @brief Compare the bounds of two capture regions.
     * @param other Region to compare.
     * @return Whether every edge is equal.
     */
    bool operator==(const pixel_rect_t &other) const = default;
  };

  /**
   * @brief Find capture pixels covering the sharp area and its transition.
   *
   * @param constants Validated cursor geometry along capture-texture axes.
   * @param width Capture texture width.
   * @param height Capture texture height.
   * @return Clipped half-open bounds, or an empty rectangle for invalid geometry.
   */
  inline pixel_rect_t focus_bounds(const shader_constants_t &constants, int width, int height) {
    if (!constants.enabled || width <= 0 || height <= 0 || !std::isfinite(constants.output_size[0]) || !std::isfinite(constants.output_size[1]) || constants.output_size[0] <= 0 || constants.output_size[1] <= 0 || !std::isfinite(constants.center[0]) || !std::isfinite(constants.center[1])) {
      return {};
    }
    const auto extent_x = (constants.rectangle ? constants.half_size[0] : constants.radius) + constants.feather;
    const auto extent_y = (constants.rectangle ? constants.half_size[1] : constants.radius) + constants.feather;
    return {
      static_cast<int>(std::floor(std::clamp((static_cast<double>(constants.center[0]) - extent_x / constants.output_size[0]) * width, 0.0, static_cast<double>(width)))),
      static_cast<int>(std::floor(std::clamp((static_cast<double>(constants.center[1]) - extent_y / constants.output_size[1]) * height, 0.0, static_cast<double>(height)))),
      static_cast<int>(std::ceil(std::clamp((static_cast<double>(constants.center[0]) + extent_x / constants.output_size[0]) * width, 0.0, static_cast<double>(width)))),
      static_cast<int>(std::ceil(std::clamp((static_cast<double>(constants.center[1]) + extent_y / constants.output_size[1]) * height, 0.0, static_cast<double>(height))))
    };
  }

  /**
   * @brief Refresh work for one frame; at most eight background tiles are copied.
   */
  struct background_update_t {
    bool full = false;  ///< Initialize the entire cache or select continuous refresh.
    std::array<pixel_rect_t, 8> regions {};  ///< Capture rectangles selected for this frame.
    std::size_t count = 0;  ///< Number of valid rectangles.
  };

  /**
   * @brief Spread a six-column, five-row background sweep across converted frames.
   */
  class background_scheduler_t {
  public:
    /**
     * @brief Forget timing and dimensions so the next image initializes the cache.
     */
    void reset() {
      initialized_ = false;
      credit_ = 0;
      next_tile_ = 0;
    }

    /**
     * @brief Select a bounded amount of background work for the current frame.
     *
     * @param width Capture texture width.
     * @param height Capture texture height.
     * @param loop_ms Target time for a complete sweep; zero is continuous, minus one freezes.
     * @param now_ns Monotonic time in nanoseconds.
     * @return Full initialization or ordered, nonoverlapping tile updates.
     */
    background_update_t advance(int width, int height, int loop_ms, std::int64_t now_ns) {
      background_update_t result;
      if (width <= 0 || height <= 0) {
        reset();
        return result;
      }
      loop_ms = std::clamp(loop_ms, -1, 60000);
      if (!initialized_ || width_ != width || height_ != height || loop_ms_ != loop_ms) {
        reset();
        initialized_ = true;
        width_ = width;
        height_ = height;
        loop_ms_ = loop_ms;
        last_ns_ = now_ns;
        result.full = true;
        return result;
      }
      if (loop_ms == 0) {
        result.full = true;
        return result;
      }
      if (loop_ms < 0) {
        return result;
      }
      const auto columns = std::min(6, width);
      const auto rows = std::min(5, height);
      const auto tile_count = columns * rows;
      const long double period_ns = static_cast<long double>(loop_ms) * 1000000;
      const auto elapsed = std::clamp(static_cast<long double>(now_ns) - last_ns_, 0.0L, period_ns);
      last_ns_ = now_ns;
      credit_ += elapsed * tile_count / period_ns;
      const auto due = static_cast<int>(std::floor(credit_ + 1e-9L));
      credit_ = std::max(0.0L, credit_ - due);
      result.count = std::min(static_cast<std::size_t>(due), result.regions.size());
      for (std::size_t index = 0; index < result.count; ++index) {
        const auto column = next_tile_ % columns;
        const auto row = next_tile_ / columns;
        result.regions[index] = {
          column * width / columns,
          row * height / rows,
          (column + 1) * width / columns,
          (row + 1) * height / rows
        };
        next_tile_ = (next_tile_ + 1) % tile_count;
      }
      return result;
    }

  private:
    bool initialized_ = false;  ///< Whether dimensions and timing are initialized.
    int width_ = 0;  ///< Current texture width.
    int height_ = 0;  ///< Current texture height.
    int loop_ms_ = 0;  ///< Current background mode and period.
    std::int64_t last_ns_ = 0;  ///< Last converted frame time.
    long double credit_ = 0;  ///< Fractional tile work accumulated between frames.
    int next_tile_ = 0;  ///< Next tile in raster order.
  };
}  // namespace video::cursor_focus
