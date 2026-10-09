/**
 * @file tests/unit/platform/windows/test_clipboard.cpp
 * @brief Exercise Unicode clipboard sharing on a private window station.
 */
#ifdef _WIN32
  #include "../../../tests_common.h"
  #include "src/config.h"
  #include "src/utility.h"

  #include <chrono>
  #include <cstring>
  #include <cwchar>
  #include <string>
  #include <string_view>
  #include <thread>
  #include <windows.h>

namespace {
  /**
   * @brief Keep test clipboard contents isolated from the user's desktop.
   */
  struct private_clipboard_t {
    HWINSTA original_station = GetProcessWindowStation();  ///< Original station, never closed by this fixture.
    HDESK original_desktop = GetThreadDesktop(GetCurrentThreadId());  ///< Original desktop.
    HWINSTA station = nullptr;  ///< Private station handle.
    HDESK desktop = nullptr;  ///< Private desktop handle.
    HWND window = nullptr;  ///< Clipboard owner created on the private desktop.
    bool switched_station = false;  ///< Whether process restoration is necessary.
    bool switched_desktop = false;  ///< Whether thread restoration is necessary.

    /**
     * @brief Create an isolated station before any clipboard access.
     */
    private_clipboard_t() {
      station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
      if (!station || !SetProcessWindowStation(station)) {
        return;
      }
      switched_station = true;
      desktop = CreateDesktopW(L"SunshineClipboardTest", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
      if (!desktop || !SetThreadDesktop(desktop)) {
        return;
      }
      switched_desktop = true;
      window = CreateWindowExW(0, L"STATIC", L"Clipboard test", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    /**
     * @brief Restore the original desktop without touching its clipboard.
     */
    ~private_clipboard_t() {
      if (window) {
        DestroyWindow(window);
      }
      if (switched_desktop) {
        SetThreadDesktop(original_desktop);
      }
      if (switched_station) {
        SetProcessWindowStation(original_station);
      }
      if (desktop) {
        CloseDesktop(desktop);
      }
      if (station) {
        CloseWindowStation(station);
      }
    }

    /**
     * @brief Set synthetic Unicode text.
     * @param text Test text.
     * @return OS success.
     */
    bool write(const std::wstring &text) {
      auto allocation = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
      if (!allocation) {
        return false;
      }
      auto *data = static_cast<wchar_t *>(GlobalLock(allocation));
      if (!data) {
        GlobalFree(allocation);
        return false;
      }
      std::memcpy(data, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
      GlobalUnlock(allocation);
      bool success = false;
      if (OpenClipboard(window)) {
        success = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, allocation);
        CloseClipboard();
      }
      if (!success) {
        GlobalFree(allocation);
      }
      return success;
    }

    /**
     * @brief Wait for a synthetic value to arrive from the worker.
     * @param expected Test text.
     * @return Whether observed.
     */
    bool wait_for(const std::wstring &expected) {
      for (int attempt = 0; attempt < 40; ++attempt) {
        // EmptyClipboard sends WM_DESTROYCLIPBOARD to the previous owner.
        // Pump our owner window while the worker takes ownership, as a GUI does.
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
          DispatchMessageW(&message);
        }
        bool matches = false;
        if (OpenClipboard(window)) {
          auto handle = GetClipboardData(CF_UNICODETEXT);
          const auto *data = handle ? static_cast<const wchar_t *>(GlobalLock(handle)) : nullptr;
          if (data) {
            const auto bound = GlobalSize(handle) / sizeof(wchar_t);
            std::size_t count = 0;
            while (count < bound && data[count]) {
              ++count;
            }
            matches = count < bound && std::wstring_view(data, count) == expected;
            GlobalUnlock(handle);
          }
          CloseClipboard();
        }
        if (matches) {
          return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      return false;
    }
  };
}  // namespace

TEST(WindowsClipboard, UnicodeBothDirectionsBusyRetryAndSessionIsolation) {
  private_clipboard_t clipboard;
  ASSERT_NE(clipboard.window, nullptr) << "Private window station creation failed";
  const auto original_mode = config::input.clipboard_mode;
  config::input.clipboard_mode = "bidirectional";
  auto mail = std::make_shared<safe::mail_raw_t>();
  auto first = mail->queue<platf::clipboard_text_t>("first");
  auto second = mail->queue<platf::clipboard_text_t>("second");
  auto cleanup = util::fail_guard([&] {
    platf::clipboard_unsubscribe(first);
    platf::clipboard_unsubscribe(second);
    config::input.clipboard_mode = original_mode;
  });
  ASSERT_TRUE(clipboard.write(L"Host 日本語 \U0001F600\r\n"));
  platf::clipboard_subscribe(first);
  auto initial = first->pop(std::chrono::seconds(4));
  ASSERT_TRUE(initial);
  EXPECT_EQ(initial->text, "Host 日本語 😀\r\n");

  ASSERT_TRUE(OpenClipboard(clipboard.window));
  platf::clipboard_set("Client 日本語 😀", first);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  CloseClipboard();
  EXPECT_TRUE(clipboard.wait_for(L"Client 日本語 \U0001F600"));
  EXPECT_FALSE(first->peek()) << "Remote write must not reflect back";

  platf::clipboard_subscribe(second);
  platf::clipboard_set("other client", second);
  ASSERT_TRUE(clipboard.write(L"local change"));
  auto changed = first->pop(std::chrono::seconds(4));
  ASSERT_TRUE(changed);
  EXPECT_EQ(changed->text, "local change");
  EXPECT_FALSE(second->peek());
  platf::clipboard_unsubscribe(first);
  platf::clipboard_subscribe(second);
  auto new_snapshot = second->pop(std::chrono::seconds(4));
  ASSERT_TRUE(new_snapshot);
  EXPECT_EQ(new_snapshot->text, "local change");
  platf::clipboard_unsubscribe(second);
  platf::clipboard_set("late write", second);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  EXPECT_TRUE(clipboard.wait_for(L"local change"));
}
#endif
