/**
 * @file src/platform/clipboard_policy.h
 * @brief Permissions for the custom Windows text clipboard extension.
 */
#pragma once
#include "src/config.h"

#include <moonlight-common-c/src/Clipboard.h>

namespace platf {
  /**
   * @brief Get the directions permitted by the host configuration.
   * @return Protocol direction bits, or zero on unsupported hosts.
   */
  inline int clipboard_permissions() {
#ifdef _WIN32
    const auto &mode = config::input.clipboard_mode;
    if (mode == "bidirectional") {
      return SS_CLIPBOARD_HOST_TO_CLIENT | SS_CLIPBOARD_CLIENT_TO_HOST;
    }
    if (mode == "host_to_client") {
      return SS_CLIPBOARD_HOST_TO_CLIENT;
    }
    if (mode == "client_to_host") {
      return SS_CLIPBOARD_CLIENT_TO_HOST;
    }
#endif
    return 0;
  }
}  // namespace platf
