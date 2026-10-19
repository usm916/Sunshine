/**
 * @file src/platform/windows/monitor_switch.cpp
 * @brief Physical Windows monitor layout and stationary edge dwell sampling.
 */
#include "monitor_switch.h"

#include "display.h"
#include "utf_utils.h"

#include <windows.h>

namespace platf::dxgi {
  std::optional<::video::monitor_switch::capture_key_t> monitor_capture_key(platf::display_t *display) {
    const auto dxgi = dynamic_cast<display_base_t *>(display);
    if (!dxgi || !dxgi->adapter) {
      return std::nullopt;
    }
    DXGI_ADAPTER_DESC desc {};
    if (FAILED(dxgi->adapter->GetDesc(&desc))) {
      return std::nullopt;
    }
    const auto luid = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(desc.AdapterLuid.HighPart)) << 32) | desc.AdapterLuid.LowPart;
    return ::video::monitor_switch::capture_key_t {luid, dxgi->is_hdr()};
  }

  std::vector<::video::monitor_switch::monitor_t> read_monitor_layout() {
    std::vector<::video::monitor_switch::monitor_t> result;
    const auto previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto success = EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
      MONITORINFOEXW info {};
      info.cbSize = sizeof(info);
      if (GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&info))) {
        auto &list = *reinterpret_cast<std::vector<::video::monitor_switch::monitor_t> *>(data);
        list.push_back({utf_utils::to_utf8(info.szDevice), {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom}});
      }
      return TRUE;
    },
                                             reinterpret_cast<LPARAM>(&result));
    if (previous) {
      SetThreadDpiAwarenessContext(previous);
    }
    return success ? result : std::vector<::video::monitor_switch::monitor_t> {};
  }

  std::optional<std::pair<int, int>> read_monitor_cursor() {
    POINT point {};
    if (!GetPhysicalCursorPos(&point)) {
      return std::nullopt;
    }
    return std::pair<int, int> {point.x, point.y};
  }

  bool place_monitor_cursor(std::pair<int, int> point) {
    return SetPhysicalCursorPos(point.first, point.second) != FALSE;
  }

  std::optional<::video::monitor_switch::request_t> monitor_switch_t::poll(const std::string &current, const std::shared_ptr<::video::viewport::state_t> &viewport, std::size_t sessions, std::chrono::steady_clock::time_point now) {
    ::video::viewport::frame_t frame;
    int delay = 600;
    bool enabled = false;
    if (viewport && sessions == 1) {
      const std::lock_guard lock(viewport->mutex);
      frame = viewport->frame;
      delay = viewport->settings.monitor_switch_delay;
      enabled = viewport->settings.monitor_switch && viewport->settings.enabled && viewport->contacts.empty();
    }
    if (!enabled) {
      return dwell.update({}, {}, now, std::chrono::milliseconds(delay));
    }
    if (now < next_poll) {
      return std::nullopt;
    }
    next_poll = now + std::chrono::milliseconds(25);
    if (now >= next_layout) {
      monitors = api.layout();
      next_layout = now + std::chrono::milliseconds(500);
    }
    const auto mouse = api.cursor();
    const auto target = mouse ? ::video::monitor_switch::target_at_edge(monitors, current, frame, *mouse) : std::nullopt;
    return dwell.update(target, mouse.value_or(std::pair {0, 0}), now, std::chrono::milliseconds(delay));
  }
}  // namespace platf::dxgi
