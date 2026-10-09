/**
 * @file src/platform/clipboard_session.h
 * @brief Single-session ownership and echo suppression for text sharing.
 */
#pragma once
#include "common.h"

#include <moonlight-common-c/src/Clipboard.h>
#include <optional>

namespace platf {
  /**
   * @brief Clipboard state owned by one active streaming session; caller serializes access.
   */
  class clipboard_session_t {
  public:
    /**
     * @brief Claim ownership, rejecting a second session.
     * @param queue Client queue.
     * @return Whether the session owns sharing.
     */
    bool subscribe(const clipboard_queue_t &queue) {
      if (!queue || (owner && owner != queue)) {
        return false;
      }
      owner = queue;
      return true;
    }

    /**
     * @brief Drop pending writes and snapshots at disconnect.
     * @param queue Disconnecting client.
     */
    void unsubscribe(const clipboard_queue_t &queue) {
      if (owner == queue) {
        owner.reset();
        pending.reset();
        last.clear();
        has_snapshot = false;
      }
    }

    /**
     * @brief Stage the latest valid remote text.
     * @param queue Origin.
     * @param text Text.
     * @return Whether accepted.
     */
    bool stage(const clipboard_queue_t &queue, std::string_view text) {
      if (!owner || queue != owner || text.empty() || !SsClipboardTextValid(text.data(), text.size())) {
        return false;
      }
      pending = std::string(text);
      return true;
    }

    /**
     * @brief Record an OS write only after success; failures remain retryable.
     * @param success OS result.
     */
    void applied(bool success) {
      if (success && pending) {
        last = std::move(*pending);
        pending.reset();
        has_snapshot = true;
      }
    }

    /**
     * @brief Publish valid host text once and suppress echoes.
     * @param text Host text.
     * @return Whether queued.
     */
    bool publish(const std::string &text) {
      if (!owner || text.empty() || !SsClipboardTextValid(text.data(), text.size()) || (has_snapshot && last == text)) {
        return false;
      }
      if (!owner->raise(clipboard_text_t {1, text})) {
        return false;
      }
      last = text;
      has_snapshot = true;
      return true;
    }

    clipboard_queue_t owner;  ///< The sole session allowed to share.
    std::optional<std::string> pending;  ///< Latest pending remote write, bounded by the protocol.
    std::string last;  ///< Last successfully queued or applied text.
    bool has_snapshot = false;  ///< Whether this owner has a current snapshot.
  };
}  // namespace platf
