/**
 * @file src/video_cursor_focus_diagnostics.h
 * @brief Bounded diagnostics for effective background shader bypass and application.
 */
#pragma once

#include <chrono>
#include <optional>

namespace video::cursor_focus {
  /** @brief Effective GPU constant-buffer selection, independent of the requested policy. */
  enum class render_state_e {
    disabled,  ///< The session requests ordinary detail.
    missing_buffer,  ///< No focus constant buffer exists.
    unmapped_anchor,  ///< Cursor/caret geometry could not be mapped.
    applied  ///< Coarse processing is enabled in the submitted constants.
  };

  /**
   * @brief Classify the constants submitted to the background shader.
   * @param requested Whether the session requests coarse background processing.
   * @param buffer Whether its constant buffer exists.
   * @param enabled Whether mapped shader constants enable the processing.
   * @return Effective application or a specific bypass reason.
   */
  inline render_state_e render_state(bool requested, bool buffer, bool enabled) {
    return !requested ? render_state_e::disabled : !buffer ? render_state_e::missing_buffer :
                                                 !enabled  ? render_state_e::unmapped_anchor :
                                                             render_state_e::applied;
  }

  /** @brief Coalesce rapid UAC/desktop transitions without per-frame log output. */
  class render_gate_t {
  public:
    /**
     * @brief Admit only changed effective states, at most once per two seconds.
     * @param state Actual submitted shader state.
     * @param now Monotonic conversion time.
     * @return Whether a state-change diagnostic should be written.
     */
    bool should_log(render_state_e state, std::chrono::steady_clock::time_point now) {
      if (last_at_ && (now < *last_at_ || now - *last_at_ < std::chrono::seconds(2))) {
        return false;
      }
      if (last_ && *last_ == state) {
        return false;
      }
      last_ = state;
      last_at_ = now;
      return true;
    }

  private:
    std::optional<render_state_e> last_;  ///< Last published effective state.
    std::optional<std::chrono::steady_clock::time_point> last_at_;  ///< Last publication time.
  };
}  // namespace video::cursor_focus
