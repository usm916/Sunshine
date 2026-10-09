/**
 * @file src/platform/clipboard_binding.h
 * @brief Serialize clipboard input against disconnect and mailbox rebinding.
 */
#pragma once
#include "common.h"

#include <mutex>

namespace platf {
  /**
   * @brief Gate clipboard operations belonging to a live stream.
   */
  class clipboard_binding_t {
  public:
    /**
     * @brief Bind the initial stream mailbox.
     * @param queue Stream mailbox.
     */
    explicit clipboard_binding_t(clipboard_queue_t queue):
        queue(std::move(queue)) {}

    /**
     * @brief Run an operation only while connected, serialized with disconnect.
     * @param action Operation receiving the current mailbox.
     * @return Whether the operation ran.
     */
    template<class Action>
    bool apply(Action action) {
      std::lock_guard guard(lock);
      if (!active || !queue) {
        return false;
      }
      action(queue);
      return true;
    }

    /**
     * @brief Disable late operations and remove the current subscription.
     * @param unsubscribe Platform subscription cleanup.
     */
    template<class Cleanup>
    void stop(Cleanup unsubscribe) {
      std::lock_guard guard(lock);
      active = false;
      unsubscribe(queue);
    }

    /**
     * @brief Replace the mailbox before resumed control input is accepted.
     * @param next Resumed stream mailbox.
     * @param unsubscribe Platform cleanup of the previous subscription.
     */
    template<class Cleanup>
    void rebind(clipboard_queue_t next, Cleanup unsubscribe) {
      std::lock_guard guard(lock);
      unsubscribe(queue);
      queue = std::move(next);
      active = true;
    }

  private:
    std::mutex lock;  ///< Serializes mailbox changes and platform operations.
    clipboard_queue_t queue;  ///< Mailbox for this stream generation.
    bool active = true;  ///< False after disconnect until an explicit rebind.
  };
}  // namespace platf
