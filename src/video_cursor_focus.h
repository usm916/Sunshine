/**
 * @file src/video_cursor_focus.h
 * @brief Cursor-following detail preservation for Windows GPU color conversion.
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace video::cursor_focus {
  /**
   * @brief Opt-in settings expressed in visible output pixels.
   */
  struct settings_t {
    bool enabled = false;  ///< Preserve detail near the cursor and simplify the background.
    int radius = 160;  ///< Radius of the unchanged region, in output pixels.
    int background_scale = 4;  ///< Coarse background cell size in output pixels.
    std::string shape = "circle";  ///< Circle or cursor-centered rectangle.
    int width = 512;  ///< Width of the unchanged rectangle, in output pixels.
    int height = 320;  ///< Height of the unchanged rectangle, in output pixels.
    int background_loop_ms = 0;  ///< Full background refresh target; zero is live, minus one freezes it.
  };

  /**
   * @brief Desktop coordinates and the visible, unletterboxed output dimensions.
   */
  struct geometry_t {
    int left;  ///< Physical desktop X coordinate of the streamed display.
    int top;  ///< Physical desktop Y coordinate of the streamed display.
    int width;  ///< Physical display width after rotation.
    int height;  ///< Physical display height after rotation.
    float output_width;  ///< Width of the visible output viewport, excluding bars.
    float output_height;  ///< Height of the visible output viewport, excluding bars.
    int rotation;  ///< DXGI clockwise rotation count, in quarter turns from zero to three.
  };

  /**
   * @brief Three 16-byte HLSL constant-buffer rows in capture-texture coordinates.
   */
  struct shader_constants_t {
    std::array<float, 2> center {};  ///< Cursor center in normalized capture-texture coordinates.
    std::array<float, 2> output_size {};  ///< Output dimensions along capture-texture axes.
    float radius = 0;  ///< Radius of the unchanged region, in output pixels.
    float feather = 0;  ///< Width of the smooth transition to the coarse background.
    float background_scale = 0;  ///< Coarse-cell size, in output pixels.
    float enabled = 0;  ///< One for valid cursor geometry, zero for passthrough.
    std::array<float, 2> half_size {};  ///< Rectangle half-extents along capture-texture axes.
    float rectangle = 0;  ///< One selects a rectangle, zero selects a circle.
    float history = 0;  ///< One samples the held background texture.
  };

  static_assert(sizeof(shader_constants_t) == 48);

  /**
   * @brief Map a physical desktop cursor to the rotated capture texture.
   *
   * Unknown or off-display cursors and invalid geometry keep the frame unchanged.
   * The output viewport preserves the existing scaling and letterbox mapping.
   *
   * @param settings Configured shape, dimensions, radius and background cell size.
   * @param geometry Streamed display and visible output viewport.
   * @param cursor_x Physical desktop cursor X coordinate.
   * @param cursor_y Physical desktop cursor Y coordinate.
   * @param cursor_known Whether the OS returned a cursor position successfully.
   * @return Shader constants with filtering disabled when inputs cannot be mapped safely.
   */
  inline shader_constants_t make_constants(const settings_t &settings, const geometry_t &geometry, std::int64_t cursor_x, std::int64_t cursor_y, bool cursor_known = true) {
    shader_constants_t result;
    if (!settings.enabled || !cursor_known || geometry.width <= 0 || geometry.height <= 0 || !std::isfinite(geometry.output_width) || !std::isfinite(geometry.output_height) || geometry.output_width <= 0 || geometry.output_height <= 0 || geometry.rotation < 0 || geometry.rotation > 3) {
      return result;
    }
    const auto left = static_cast<std::int64_t>(geometry.left);
    const auto top = static_cast<std::int64_t>(geometry.top);
    if (cursor_x < left || cursor_y < top || cursor_x >= left + geometry.width || cursor_y >= top + geometry.height) {
      return result;
    }

    const float x = (static_cast<float>(cursor_x - left) + 0.5f) / geometry.width;
    const float y = (static_cast<float>(cursor_y - top) + 0.5f) / geometry.height;
    switch (geometry.rotation) {
      case 1:
        result.center = {y, 1.0f - x};
        break;
      case 2:
        result.center = {1.0f - x, 1.0f - y};
        break;
      case 3:
        result.center = {1.0f - y, x};
        break;
      default:
        result.center = {x, y};
        break;
    }
    result.output_size = {geometry.output_width, geometry.output_height};
    if (geometry.rotation & 1) {
      std::swap(result.output_size[0], result.output_size[1]);
    }
    result.radius = static_cast<float>(std::clamp(settings.radius, 32, 1024));
    result.feather = result.radius * 0.25f;
    result.background_scale = static_cast<float>(std::clamp(settings.background_scale, 2, 16));
    result.rectangle = settings.shape == "rectangle" ? 1.0f : 0.0f;
    result.half_size = {static_cast<float>(std::clamp(settings.width, 32, 8192)) * 0.5f, static_cast<float>(std::clamp(settings.height, 32, 8192)) * 0.5f};
    if (geometry.rotation & 1) {
      std::swap(result.half_size[0], result.half_size[1]);
    }
    if (result.rectangle) {
      result.feather = std::min(result.half_size[0], result.half_size[1]) * 0.25f;
    }
    result.enabled = 1.0f;
    return result;
  }
}  // namespace video::cursor_focus
