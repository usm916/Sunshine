/**
 * @file src/platform/windows/clipboard.cpp
 * @brief Windows Unicode text clipboard polling for the authenticated streaming session.
 */
#include "src/platform/clipboard_policy.h"
#include "src/platform/clipboard_session.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <windows.h>

namespace platf {
  namespace {
    /**
     * @brief Read bounded Unicode text while holding the OS clipboard lock.
     * @param window Clipboard owner window.
     * @param text Valid UTF-8, or empty for an unsupported/invalid value.
     * @param sequence Sequence observed under the lock.
     * @return False when another application holds the clipboard; caller retries later.
     */
    bool read_text(HWND window, std::string &text, DWORD &sequence) {
      if (!OpenClipboard(window)) {
        return false;
      }
      text.clear();
      auto handle = GetClipboardData(CF_UNICODETEXT);
      if (handle) {
        const auto bytes = GlobalSize(handle);
        if (bytes >= sizeof(wchar_t) && bytes <= (SS_CLIPBOARD_TEXT_MAX + 1) * sizeof(wchar_t)) {
          const auto *wide = static_cast<const wchar_t *>(GlobalLock(handle));
          if (wide) {
            std::size_t count = 0;
            const auto bound = bytes / sizeof(wchar_t);
            while (count < bound && wide[count]) {
              ++count;
            }
            if (count < bound && count > 0) {
              const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, static_cast<int>(count), nullptr, 0, nullptr, nullptr);
              if (length > 0 && length <= SS_CLIPBOARD_TEXT_MAX) {
                text.resize(length);
                if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, static_cast<int>(count), text.data(), length, nullptr, nullptr)) {
                  text.clear();
                }
              }
            }
            GlobalUnlock(handle);
          }
        }
      }
      sequence = GetClipboardSequenceNumber();
      CloseClipboard();
      return true;
    }

    /**
     * @brief Write validated text, transferring allocation ownership only on success.
     * @param window Window that owns the clipboard after EmptyClipboard.
     * @param text Complete UTF-8 scalar sequence.
     * @param sequence Resulting sequence under the OS lock.
     * @return Whether the OS accepted the text.
     */
    bool write_text(HWND window, const std::string &text, DWORD &sequence) {
      const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
      if (count <= 0) {
        return false;
      }
      auto allocation = GlobalAlloc(GMEM_MOVEABLE, (count + 1) * sizeof(wchar_t));
      if (!allocation) {
        return false;
      }
      auto *wide = static_cast<wchar_t *>(GlobalLock(allocation));
      if (!wide) {
        GlobalFree(allocation);
        return false;
      }
      const auto converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide, count);
      wide[count] = L'\0';
      GlobalUnlock(allocation);
      bool success = false;
      if (converted == count && OpenClipboard(window)) {
        success = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, allocation);
        sequence = GetClipboardSequenceNumber();
        CloseClipboard();
      }
      if (!success) {
        GlobalFree(allocation);
      }
      return success;
    }

    /**
     * @brief Serialized clipboard service with a single bounded pending write.
     */
    class clipboard_service_t {
    public:
      /**
       * @brief Start a worker that can own the Windows clipboard.
       */
      clipboard_service_t():
          worker([this](std::stop_token stop) {
            run(stop);
          }) {}

      std::mutex lock;  ///< Protects ownership and pending data, including unsubscribe.
      clipboard_session_t state;  ///< State belonging to the current streaming session.
      std::chrono::steady_clock::time_point deadline;  ///< Busy clipboard writes expire after two seconds.

    private:
      /**
       * @brief Poll at most four times per second while a session subscribes.
       * @param stop Shutdown request.
       */
      void run(std::stop_token stop) {
        const auto window = CreateWindowExW(0, L"STATIC", L"Sunshine clipboard", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) {
          return;
        }
        DWORD seen = 0;
        while (!stop.stop_requested()) {
          MSG message;
          while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            DispatchMessageW(&message);
          }
          {
            std::lock_guard guard(lock);
            if (state.owner) {
              if (state.pending) {
                if (std::chrono::steady_clock::now() > deadline) {
                  state.pending.reset();
                } else {
                  state.applied(write_text(window, *state.pending, seen));
                }
              }
              if ((clipboard_permissions() & SS_CLIPBOARD_HOST_TO_CLIENT) && !state.pending && (!state.has_snapshot || GetClipboardSequenceNumber() != seen)) {
                std::string text;
                if (read_text(window, text, seen)) {
                  state.publish(text);
                }
              }
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        DestroyWindow(window);
      }

      std::jthread worker;  ///< Joined before service state is destroyed.
    };

    /**
     * @brief Lazily create the process-wide worker.
     * @return Clipboard service.
     */
    clipboard_service_t &service() {
      static clipboard_service_t value;
      return value;
    }
  }  // namespace

  void clipboard_subscribe(clipboard_queue_t queue) {
    if (!clipboard_permissions()) {
      return;
    }
    auto &worker = service();
    std::lock_guard guard(worker.lock);
    worker.state.subscribe(queue);
  }

  void clipboard_set(std::string_view text, const clipboard_queue_t &origin) {
    if (!(clipboard_permissions() & SS_CLIPBOARD_CLIENT_TO_HOST)) {
      return;
    }
    auto &worker = service();
    std::lock_guard guard(worker.lock);
    if (worker.state.stage(origin, text)) {
      worker.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
  }

  void clipboard_unsubscribe(const clipboard_queue_t &queue) {
    if (!clipboard_permissions()) {
      return;
    }
    auto &worker = service();
    std::lock_guard guard(worker.lock);
    worker.state.unsubscribe(queue);
  }
}  // namespace platf
