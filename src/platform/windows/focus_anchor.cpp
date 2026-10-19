/**
 * @file src/platform/windows/focus_anchor.cpp
 * @brief Nonblocking caret lookup and keyboard/mouse activity observation on Windows.
 */
#include "focus_anchor.h"

#include "src/logging.h"
#include "src/utility.h"

#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <oleacc.h>
#include <thread>
#include <uiautomation.h>
#include <windows.h>
#include <wrl/client.h>

namespace platf::dxgi {
  namespace {
    using point_t = ::video::focus_anchor::point_t;  ///< Physical desktop point.
    using clock_t = std::chrono::steady_clock;  ///< Cache freshness clock.
    using Microsoft::WRL::ComPtr;
    using namespace std::chrono_literals;

    /**
     * @brief Retrieve a standard Win32 caret without reading any text.
     * @param foreground Current foreground window.
     * @return Physical caret center.
     */
    std::optional<point_t> system_caret(HWND foreground) {
      GUITHREADINFO info {};
      info.cbSize = sizeof(info);
      const auto thread = GetWindowThreadProcessId(foreground, nullptr);
      if (!thread || !GetGUIThreadInfo(thread, &info) || !info.hwndCaret || !IsWindowVisible(info.hwndCaret) || GetAncestor(info.hwndCaret, GA_ROOT) != GetAncestor(foreground, GA_ROOT) || info.rcCaret.bottom <= info.rcCaret.top) {
        return std::nullopt;
      }
      POINT point {info.rcCaret.left + (info.rcCaret.right - info.rcCaret.left) / 2, info.rcCaret.top + (info.rcCaret.bottom - info.rcCaret.top) / 2};
      const auto previous_dpi = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(info.hwndCaret));
      auto restore_dpi = util::fail_guard([&] {
        if (previous_dpi) {
          SetThreadDpiAwarenessContext(previous_dpi);
        }
      });
      if (!ClientToScreen(info.hwndCaret, &point) || !LogicalToPhysicalPointForPerMonitorDPI(info.hwndCaret, &point)) {
        return std::nullopt;
      }
      return point_t {point.x, point.y};
    }

    /**
     * @brief Query legacy accessible carets used by Chromium when TextPattern is unavailable.
     * @param foreground Top-level window owning the current input focus.
     * @return Visible physical caret bounds, without reading any text.
     */
    std::optional<point_t> legacy_caret(HWND foreground) {
      if (!foreground || !IsWindowVisible(foreground)) {
        return std::nullopt;
      }
      GUITHREADINFO info {sizeof(info)};
      GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info);
      for (auto window : {info.hwndFocus, foreground}) {
        if (!window || GetAncestor(window, GA_ROOT) != GetAncestor(foreground, GA_ROOT)) {
          continue;
        }
        ComPtr<IAccessible> accessible;
        if (FAILED(AccessibleObjectFromWindow(window, OBJID_CARET, IID_PPV_ARGS(&accessible))) || !accessible) {
          continue;
        }
        VARIANT child {};
        child.vt = VT_I4;
        child.lVal = CHILDID_SELF;
        VARIANT role {}, state {};
        auto clear = util::fail_guard([&] {
          VariantClear(&role);
          VariantClear(&state);
        });
        if (FAILED(accessible->get_accRole(child, &role)) || role.vt != VT_I4 || role.lVal != ROLE_SYSTEM_CARET || FAILED(accessible->get_accState(child, &state)) || state.vt != VT_I4 || (state.lVal & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN))) {
          continue;
        }
        LONG x = 0, y = 0, width = 0, height = 0;
        if (accessible->accLocation(&x, &y, &width, &height, child) == S_OK) {
          if (auto point = ::video::focus_anchor::rectangle_center(x, y, width, height)) {
            return point;
          }
        }
      }
      return std::nullopt;
    }

    /**
     * @brief Read the first visible text-range rectangle without reading its contents.
     * @param range UI Automation range supplied by the provider.
     * @return Left, top, width, height or no visible rectangle.
     */
    std::optional<std::array<double, 4>> range_bounds(IUIAutomationTextRange *range) {
      SAFEARRAY *rectangles = nullptr;
      if (!range || FAILED(range->GetBoundingRectangles(&rectangles)) || !rectangles) {
        return std::nullopt;
      }
      auto cleanup = util::fail_guard([&] {
        SafeArrayDestroy(rectangles);
      });
      LONG lower = 0, upper = -1;
      double *values = nullptr;
      if (SafeArrayGetDim(rectangles) != 1 || FAILED(SafeArrayGetLBound(rectangles, 1, &lower)) || FAILED(SafeArrayGetUBound(rectangles, 1, &upper)) || upper - lower + 1 < 4 || FAILED(SafeArrayAccessData(rectangles, reinterpret_cast<void **>(&values)))) {
        return std::nullopt;
      }
      const std::array<double, 4> result {values[0], values[1], values[2], values[3]};
      SafeArrayUnaccessData(rectangles);
      return result;
    }

    /**
     * @brief Resolve empty insertion ranges by expanding only their geometry to an adjacent character.
     * @param range Caret range, which is often degenerate in Chromium and other providers.
     * @return Physical insertion point or no visible geometry.
     */
    std::optional<point_t> range_caret(IUIAutomationTextRange *range) {
      if (auto bounds = range_bounds(range)) {
        return ::video::focus_anchor::rectangle_center((*bounds)[0], (*bounds)[1], (*bounds)[2], (*bounds)[3]);
      }
      int comparison = 0;
      if (!range || FAILED(range->CompareEndpoints(TextPatternRangeEndpoint_Start, range, TextPatternRangeEndpoint_End, &comparison)) || comparison != 0) {
        return std::nullopt;
      }
      ComPtr<IUIAutomationTextRange> expanded;
      int moved = 0;
      if (FAILED(range->Clone(&expanded)) || !expanded || FAILED(expanded->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 1, &moved))) {
        return std::nullopt;
      }
      bool trailing = false;
      if (!moved) {
        if (FAILED(expanded->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -1, &moved)) || !moved) {
          return std::nullopt;
        }
        trailing = true;
      }
      if (auto bounds = range_bounds(expanded.Get())) {
        return ::video::focus_anchor::rectangle_center((*bounds)[0] + (trailing ? (*bounds)[2] : 0), (*bounds)[1], 0, (*bounds)[3]);
      }
      return std::nullopt;
    }

    /**
     * @brief Retrieve a UI Automation caret, including providers exposing only TextPattern selection.
     * @param automation Worker-owned UI Automation client.
     * @return Physical caret center without inspecting text values.
     */
    std::optional<point_t> automation_caret(IUIAutomation *automation) {
      if (!automation) {
        return std::nullopt;
      }
      ComPtr<IUIAutomationElement> element;
      if (FAILED(automation->GetFocusedElement(&element)) || !element) {
        return std::nullopt;
      }
      ComPtr<IUIAutomationTextPattern2> pattern;
      if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&pattern))) && pattern) {
        BOOL active = FALSE;
        ComPtr<IUIAutomationTextRange> range;
        if (SUCCEEDED(pattern->GetCaretRange(&active, &range)) && active && range) {
          if (auto point = range_caret(range.Get())) {
            return point;
          }
        }
      }
      ComPtr<IUIAutomationTextPattern> selection_pattern;
      ComPtr<IUIAutomationTextRangeArray> selection;
      int count = 0;
      if (FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&selection_pattern))) || !selection_pattern || FAILED(selection_pattern->GetSelection(&selection)) || !selection || FAILED(selection->get_Length(&count)) || count != 1) {
        return std::nullopt;
      }
      ComPtr<IUIAutomationTextRange> range;
      int comparison = 1;
      if (FAILED(selection->GetElement(0, &range)) || !range || FAILED(range->CompareEndpoints(TextPatternRangeEndpoint_Start, range.Get(), TextPatternRangeEndpoint_End, &comparison)) || comparison != 0) {
        return std::nullopt;
      }
      return range_caret(range.Get());
    }

    /**
     * @brief Ignore modifier-only key presses until an editing/text key arrives.
     * @param key Windows virtual key.
     * @return Whether this key can express text/editing activity.
     */
    bool editing_key(DWORD key) {
      return key != VK_SHIFT && key != VK_LSHIFT && key != VK_RSHIFT && key != VK_CONTROL && key != VK_LCONTROL && key != VK_RCONTROL && key != VK_MENU && key != VK_LMENU && key != VK_RMENU && key != VK_LWIN && key != VK_RWIN && key != VK_CAPITAL && key != VK_NUMLOCK && key != VK_SCROLL;
    }
  }  // namespace

  /**
   * @brief Shared hook/worker state remains alive until an outstanding provider call returns.
   */
  struct focus_anchor_service_t::impl_t {
    /**
     * @brief Set the input activity policy.
     * @param threshold Physical mouse displacement.
     */
    explicit impl_t(int threshold):
        activity(threshold) {}

    std::mutex mutex;  ///< Protect activity and cached caret.
    std::condition_variable changed;  ///< Wake discovery after keyboard input or shutdown.
    ::video::focus_anchor::activity_t activity;  ///< Input-source policy.
    ::video::focus_anchor::caret_cache_t cached;  ///< Bounded cache associated with the input epoch.
    DWORD hook_thread_id = 0;  ///< Message thread receiving low-level input callbacks.
    bool stop = false;  ///< Stop requested by the last encode session.
    std::thread hooks;  ///< Hook thread; joined after posting quit.
    static thread_local impl_t *current;  ///< State belonging to the current hook thread.

    /**
     * @brief Observe a keyboard action and immediately pass it onward.
     * @param code Hook code.
     * @param message Keyboard message.
     * @param data Keyboard event metadata.
     * @return Next hook result.
     */
    static LRESULT CALLBACK keyboard_hook(int code, WPARAM message, LPARAM data) {
      if (code == HC_ACTION && current && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && editing_key(reinterpret_cast<KBDLLHOOKSTRUCT *>(data)->vkCode)) {
        const auto owner = reinterpret_cast<std::uintptr_t>(GetForegroundWindow());
        std::lock_guard lock(current->mutex);
        if (!current->activity.prefers_caret(owner)) {
          current->cached.reset();
        }
        current->activity.keyboard(owner);
        current->changed.notify_one();
      }
      return CallNextHookEx(nullptr, code, message, data);
    }

    /**
     * @brief Observe pointer movement/clicks without consuming them.
     * @param code Hook code.
     * @param message Mouse message.
     * @param data Mouse event metadata.
     * @return Next hook result.
     */
    static LRESULT CALLBACK mouse_hook(int code, WPARAM message, LPARAM data) {
      if (code == HC_ACTION && current) {
        const auto &event = *reinterpret_cast<MSLLHOOKSTRUCT *>(data);
        std::lock_guard lock(current->mutex);
        if (message == WM_MOUSEMOVE) {
          current->activity.mouse_move({event.pt.x, event.pt.y});
        } else if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN || message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
          current->activity.mouse_action({event.pt.x, event.pt.y});
          current->cached.reset();
        }
      }
      return CallNextHookEx(nullptr, code, message, data);
    }

    /**
     * @brief Run only hooks/message dispatch; never call UI Automation on this thread.
     */
    void observe() {
      current = this;
      MSG message {};
      PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
      {
        std::lock_guard lock(mutex);
        hook_thread_id = GetCurrentThreadId();
      }
      const auto keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, GetModuleHandleW(nullptr), 0);
      const auto mouse = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, GetModuleHandleW(nullptr), 0);
      if (!keyboard || !mouse) {
        BOOST_LOG(warning) << "Caret focus: input activity hooks unavailable; mouse fallback remains available";
        if (keyboard) {
          UnhookWindowsHookEx(keyboard);
        }
        if (mouse) {
          UnhookWindowsHookEx(mouse);
        }
        current = nullptr;
        return;
      } else {
        BOOST_LOG(info) << "Caret focus: keyboard/mouse activity observer started";
      }
      while (true) {
        {
          std::lock_guard lock(mutex);
          if (stop) {
            break;
          }
        }
        if (GetMessageW(&message, nullptr, 0, 0) <= 0) {
          break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      if (keyboard) {
        UnhookWindowsHookEx(keyboard);
      }
      if (mouse) {
        UnhookWindowsHookEx(mouse);
      }
      current = nullptr;
    }

    /**
     * @brief Query carets on a dedicated COM worker and discard stale/wrong-window results.
     */
    void discover() {
      const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      auto uninitialize = util::fail_guard([&] {
        if (SUCCEEDED(initialized)) {
          CoUninitialize();
        }
      });
      ComPtr<IUIAutomation2> automation;
      if (SUCCEEDED(initialized) && SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)))) {
        automation->put_ConnectionTimeout(150);
        automation->put_TransactionTimeout(150);
      }
      std::unique_lock lock(mutex);
      int previous_source = -1;
      auto last_source_log = clock_t::now() - 2s;
      while (!stop) {
        changed.wait_for(lock, 75ms);
        if (stop) {
          break;
        }
        const auto owner = GetForegroundWindow();
        if (!owner || !activity.prefers_caret(reinterpret_cast<std::uintptr_t>(owner))) {
          cached.reset();
          continue;
        }
        const auto revision = activity.revision();
        lock.unlock();
        const auto started = clock_t::now();
        auto point = system_caret(owner);
        int source = 0;
        if (!point) {
          point = legacy_caret(owner);
          source = 1;
        }
        if (!point) {
          point = automation_caret(automation.Get());
          source = 2;
        }
        if (point && source != previous_source && clock_t::now() - last_source_log >= 2s) {
          BOOST_LOG(info) << "Caret focus: position source=" << (source == 0 ? "win32" : source == 1 ? "msaa" :
                                                                                                       "uia");
          previous_source = source;
          last_source_log = clock_t::now();
        }
        const bool fresh = GetForegroundWindow() == owner && clock_t::now() - started <= 300ms;
        lock.lock();
        if (activity.prefers_caret(reinterpret_cast<std::uintptr_t>(owner))) {
          cached.publish(fresh ? point : std::nullopt, reinterpret_cast<std::uintptr_t>(owner), revision, started, clock_t::now());
        }
      }
    }
  };

  thread_local focus_anchor_service_t::impl_t *focus_anchor_service_t::impl_t::current = nullptr;

  focus_anchor_service_t::focus_anchor_service_t(int threshold):
      impl_(std::make_shared<impl_t>(threshold)) {
    POINT mouse {};
    if (GetPhysicalCursorPos(&mouse)) {
      impl_->activity.mouse_move({mouse.x, mouse.y});
    }
    impl_->hooks = std::thread([state = impl_] {
      state->observe();
    });
    // Provider calls may outlive encoder teardown; the worker owns its state until it returns.
    std::thread([state = impl_] {
      state->discover();
    }).detach();
  }

  focus_anchor_service_t::~focus_anchor_service_t() {
    DWORD thread = 0;
    {
      std::lock_guard lock(impl_->mutex);
      impl_->stop = true;
      thread = impl_->hook_thread_id;
    }
    impl_->changed.notify_one();
    if (thread) {
      PostThreadMessageW(thread, WM_QUIT, 0, 0);
    }
    if (impl_->hooks.joinable()) {
      impl_->hooks.join();
    }
  }

  std::optional<point_t> focus_anchor_service_t::caret() const {
    const auto foreground = reinterpret_cast<std::uintptr_t>(GetForegroundWindow());
    std::lock_guard lock(impl_->mutex);
    return impl_->activity.prefers_caret(foreground) ? impl_->cached.read(foreground, impl_->activity.revision(), clock_t::now()) : std::nullopt;
  }

  std::shared_ptr<focus_anchor_service_t> acquire_focus_anchor(int threshold) {
    static std::mutex mutex;
    static std::weak_ptr<focus_anchor_service_t> shared;
    std::lock_guard lock(mutex);
    auto service = shared.lock();
    if (!service) {
      service = std::make_shared<focus_anchor_service_t>(threshold);
      shared = service;
    }
    return service;
  }
#ifdef SUNSHINE_TESTS
  namespace testing {
    std::optional<point_t> system_caret(std::uintptr_t window) {
      return dxgi::system_caret(reinterpret_cast<HWND>(window));
    }

    std::optional<point_t> legacy_caret(std::uintptr_t window) {
      return dxgi::legacy_caret(reinterpret_cast<HWND>(window));
    }

    bool editing_key(std::uint32_t key) {
      return dxgi::editing_key(key);
    }
  }  // namespace testing
#endif
}  // namespace platf::dxgi
