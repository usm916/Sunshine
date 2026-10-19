/**
 * @file src/video_focus_anchor.h
 * @brief Input-driven selection of mouse and text-caret detail anchors.
 */
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace video::focus_anchor {
  /**
   * @brief Physical Windows desktop coordinates.
   */
  struct point_t {
    int x = 0;  ///< Physical desktop X.
    int y = 0;  ///< Physical desktop Y.
    /**
     * @brief Compare coordinates.
     */
    bool operator==(const point_t &) const = default;
  };

  /**
   * @brief Validate accessible caret bounds without reading text or trusting invalid provider numbers.
   * @param x Left edge in physical desktop pixels.
   * @param y Top edge in physical desktop pixels.
   * @param width Caret width; a zero-width insertion point is valid.
   * @param height Caret height.
   * @return Physical center or no valid caret.
   */
  inline std::optional<point_t> rectangle_center(double x, double y, double width, double height) {
    const double cx = x + width / 2, cy = y + height / 2;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) || width < 0 || height <= 0 || !std::isfinite(cx) || !std::isfinite(cy) || cx < std::numeric_limits<int>::min() || cx > std::numeric_limits<int>::max() || cy < std::numeric_limits<int>::min() || cy > std::numeric_limits<int>::max()) {
      return std::nullopt;
    }
    return point_t {static_cast<int>(cx), static_cast<int>(cy)};
  }

  /**
   * @brief Single-threaded activity policy; callers serialize shared access.
   */
  class activity_t {
  public:
    /**
     * @brief Set the movement distance needed to leave caret mode.
     * @param threshold Physical pixels, clamped to one through 100.
     */
    explicit activity_t(int threshold = 6):
        threshold_(std::clamp(threshold, 1, 100)) {}

    /**
     * @brief Observe movement without letting subthreshold jitter leave caret mode.
     * @param point Latest physical pointer position.
     */
    void mouse_move(point_t point) {
      mouse_ = point;
      if (!reference_) {
        reference_ = point;
      }
      const auto dx = static_cast<std::int64_t>(point.x) - reference_->x;
      const auto dy = static_cast<std::int64_t>(point.y) - reference_->y;
      // Bound each component before squaring so extreme desktop coordinates cannot overflow.
      if (dx >= threshold_ || dx <= -threshold_ || dy >= threshold_ || dy <= -threshold_ || dx * dx + dy * dy >= threshold_ * threshold_) {
        reference_ = point;
        caret_ = false;
        ++revision_;
      }
    }

    /**
     * @brief Switch to the mouse immediately for a click or wheel action.
     * @param point Physical pointer position at the action.
     */
    void mouse_action(point_t point) {
      mouse_ = reference_ = point;
      caret_ = false;
      ++revision_;
    }

    /**
     * @brief Prefer the caret following an editing key or Unicode text event.
     * @param foreground Identity of the window receiving the input.
     */
    void keyboard(std::uintptr_t foreground) {
      if (foreground == 0) {
        return;
      }
      if (!caret_ || foreground_ != foreground) {
        ++revision_;
      }
      reference_ = mouse_;
      foreground_ = foreground;
      caret_ = true;
    }

    /**
     * @brief Check whether caret lookup is appropriate for the current foreground.
     * @param foreground Current foreground-window identity.
     * @return True only after keyboard activity in that same foreground window.
     */
    bool prefers_caret(std::uintptr_t foreground) const {
      return caret_ && foreground != 0 && foreground == foreground_;
    }

    /**
     * @brief Return the activity epoch used to reject results from earlier input modes.
     */
    std::uint64_t revision() const {
      return revision_;
    }

  private:
    int threshold_;  ///< Meaningful cumulative pointer displacement.
    std::optional<point_t> mouse_;  ///< Latest observed pointer position.
    std::optional<point_t> reference_;  ///< Pointer baseline captured when typing begins.
    std::uintptr_t foreground_ = 0;  ///< Foreground that received the last keyboard action.
    std::uint64_t revision_ = 0;  ///< Changes whenever an old caret lookup must be rejected.
    bool caret_ = false;  ///< Typing remains preferred until meaningful mouse activity.
  };

  /**
   * @brief Reject stale, delayed and wrong-window caret lookups.
   */
  class caret_cache_t {
  public:
    using clock_t = std::chrono::steady_clock;  ///< Monotonic query/cache clock.

    /**
     * @brief Forget an unavailable caret or a changed foreground.
     */
    void reset() {
      point_.reset();
    }

    /**
     * @brief Publish a lookup with the input epoch at which it started.
     * @param point Caret returned by the provider.
     * @param owner Foreground-window identity.
     * @param revision Input epoch captured before the provider call.
     * @param started Lookup start.
     * @param finished Lookup completion.
     */
    void publish(std::optional<point_t> point, std::uintptr_t owner, std::uint64_t revision, clock_t::time_point started, clock_t::time_point finished) {
      point_ = finished >= started && finished - started <= std::chrono::milliseconds(300) ? point : std::nullopt;
      owner_ = owner;
      revision_ = revision;
      completed_ = finished;
    }

    /**
     * @brief Read only a recent lookup belonging to the current input mode/window.
     * @param owner Current foreground.
     * @param revision Current input epoch.
     * @param now Current monotonic time.
     * @return Fresh caret or no value, requiring mouse fallback.
     */
    std::optional<point_t> read(std::uintptr_t owner, std::uint64_t revision, clock_t::time_point now) const {
      return owner != 0 && owner == owner_ && revision == revision_ && now >= completed_ && now - completed_ <= std::chrono::milliseconds(300) ? point_ : std::nullopt;
    }

  private:
    std::optional<point_t> point_;  ///< Last successful bounded lookup.
    std::uintptr_t owner_ = 0;  ///< Window owning the cached caret.
    std::uint64_t revision_ = 0;  ///< Input epoch owning the cached caret.
    clock_t::time_point completed_ {};  ///< Completion time, never refreshed by reads.
  };

  /**
   * @brief Reject unavailable or off-display caret positions before falling back to the mouse.
   * @param mouse Current pointer position, or no known pointer.
   * @param caret Fresh physical caret position selected by the activity policy.
   * @param left Physical display left edge.
   * @param top Physical display top edge.
   * @param width Physical display width.
   * @param height Physical display height.
   * @return Valid on-display caret, otherwise the unmodified mouse fallback.
   */
  inline std::optional<point_t> select(std::optional<point_t> mouse, std::optional<point_t> caret, int left, int top, int width, int height) {
    if (caret && width > 0 && height > 0 && caret->x >= left && caret->y >= top && static_cast<std::int64_t>(caret->x) < static_cast<std::int64_t>(left) + width && static_cast<std::int64_t>(caret->y) < static_cast<std::int64_t>(top) + height) {
      return caret;
    }
    return mouse;
  }
}  // namespace video::focus_anchor
