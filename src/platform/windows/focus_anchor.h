/**
 * @file src/platform/windows/focus_anchor.h
 * @brief Shared Windows input activity and asynchronous text-caret discovery.
 */
#pragma once
#include "src/video_focus_anchor.h"

#include <memory>

namespace platf::dxgi {
  /**
   * @brief Own input hooks while users need automatic detail anchoring.
   */
  class focus_anchor_service_t {
  public:
    /**
     * @brief Create workers.
     * @param threshold Meaningful mouse displacement in physical pixels.
     */
    explicit focus_anchor_service_t(int threshold);
    /**
     * @brief Remove hooks and stop caret discovery without waiting on a UI Automation provider.
     */
    ~focus_anchor_service_t();
    /** @brief Observers have a unique shutdown owner and cannot be copied. */
    focus_anchor_service_t(const focus_anchor_service_t &) = delete;
    /** @brief Observers have a unique shutdown owner and cannot be assigned. */
    focus_anchor_service_t &operator=(const focus_anchor_service_t &) = delete;
    /**
     * @brief Return a fresh caret for the current typing window, otherwise no caret.
     */
    std::optional<::video::focus_anchor::point_t> caret() const;

  private:
    struct impl_t;  ///< Shared worker lifetime, separate from encoder teardown.
    std::shared_ptr<impl_t> impl_;  ///< Worker-owned state.
  };

  /**
   * @brief Share one set of Windows hooks and one caret worker across encode sessions.
   * @param threshold Meaningful mouse displacement in physical pixels.
   * @return Shared activity service.
   */
  std::shared_ptr<focus_anchor_service_t> acquire_focus_anchor(int threshold);
#ifdef SUNSHINE_TESTS
  namespace testing {
    /**
     * @brief Query a standard caret in an owned test window.
     * @param window Window identity.
     * @return Physical caret center.
     */
    std::optional<::video::focus_anchor::point_t> system_caret(std::uintptr_t window);
    /**
     * @brief Check the keyboard-activity filter.
     * @param key Virtual key.
     * @return True for editing activity.
     */
    bool editing_key(std::uint32_t key);
    /**
     * @brief Query legacy caret geometry without activating the target window.
     * @param window Window identity.
     * @return Visible physical caret center.
     */
    std::optional<::video::focus_anchor::point_t> legacy_caret(std::uintptr_t window);

  }  // namespace testing
#endif
}  // namespace platf::dxgi
