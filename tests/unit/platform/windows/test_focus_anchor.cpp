/**
 * @file tests/unit/platform/windows/test_focus_anchor.cpp
 * @brief Windows caret-coordinate discovery without activating or sending input to user windows.
 */
#include "../../../tests_common.h"
#ifdef _WIN32
  #include "src/platform/windows/focus_anchor.h"
  #include "src/utility.h"

  #include <oleacc.h>
  #include <windows.h>
  #include <wrl/client.h>

TEST(WindowsFocusAnchor, ReadsAnOwnedOffscreenCaretAndRejectsHiddenOrMissingCarets) {
  using platf::dxgi::testing::system_caret;
  EXPECT_FALSE(system_caret(0));
  EXPECT_FALSE(platf::dxgi::testing::legacy_caret(0));
  const auto dpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  auto restore = util::fail_guard([&] {
    if (dpi) {
      SetThreadDpiAwarenessContext(dpi);
    }
  });
  const auto window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"Sunshine caret test", WS_POPUP | WS_VISIBLE, -32000, -32000, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  ASSERT_NE(window, nullptr);
  auto cleanup = util::fail_guard([&] {
    DestroyCaret();
    DestroyWindow(window);
  });
  EXPECT_FALSE(system_caret(reinterpret_cast<std::uintptr_t>(window)));
  ASSERT_TRUE(CreateCaret(window, nullptr, 2, 20));
  ASSERT_TRUE(SetCaretPos(12, 24));
  auto caret = system_caret(reinterpret_cast<std::uintptr_t>(window));
  ASSERT_TRUE(caret.has_value());
  EXPECT_EQ(*caret, (video::focus_anchor::point_t {-31987, -31966}));
  ASSERT_TRUE(SetCaretPos(20, 30));
  EXPECT_EQ(system_caret(reinterpret_cast<std::uintptr_t>(window)), (video::focus_anchor::point_t {-31979, -31960}));
  ShowWindow(window, SW_HIDE);
  EXPECT_FALSE(platf::dxgi::testing::legacy_caret(reinterpret_cast<std::uintptr_t>(window)));
  EXPECT_FALSE(system_caret(reinterpret_cast<std::uintptr_t>(window)));
  DestroyCaret();
  EXPECT_FALSE(system_caret(reinterpret_cast<std::uintptr_t>(window)));
}

TEST(WindowsFocusAnchor, FiltersModifierOnlyActionsAndSharesTheObserverLifecycle) {
  for (auto key : {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN, VK_CAPITAL, VK_NUMLOCK, VK_SCROLL}) {
    EXPECT_FALSE(platf::dxgi::testing::editing_key(key));
  }
  for (auto key : {0x41, VK_BACK, VK_RETURN, VK_LEFT, VK_PACKET, VK_PROCESSKEY}) {
    EXPECT_TRUE(platf::dxgi::testing::editing_key(key));
  }
  auto first = platf::dxgi::acquire_focus_anchor(6);
  auto second = platf::dxgi::acquire_focus_anchor(6);
  EXPECT_EQ(first, second);
  first->caret();
  first.reset();
  second.reset();
  EXPECT_NE(platf::dxgi::acquire_focus_anchor(6), nullptr);
}

namespace {
  /**
   * @brief A Chromium-style MSAA caret exposing screen bounds without TextPattern or Win32 caret state.
   */
  class synthetic_accessible_caret_t final: public IAccessible {
  public:
    LONG x = 100, y = 200, width = 0, height = 26;  ///< Mutable physical caret geometry.
    LONG state = 0;  ///< Accessible state flags.
    LONG role = ROLE_SYSTEM_CARET;  ///< Exposed object role.
    LONG references = 1;  ///< COM reference count.

    /**
     * @brief Query the supported COM identities.
     */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
      if (!out) {
        return E_POINTER;
      }
      *out = nullptr;
      if (iid == __uuidof(IUnknown) || iid == __uuidof(IDispatch) || iid == __uuidof(IAccessible)) {
        *out = static_cast<IAccessible *>(this);
        AddRef();
        return S_OK;
      }
      return E_NOINTERFACE;
    }

    /**
     * @brief Retain the synthetic provider. @return Reference count.
     */
    ULONG STDMETHODCALLTYPE AddRef() override {
      return InterlockedIncrement(&references);
    }

    /**
     * @brief Release the provider. @return Remaining references.
     */
    ULONG STDMETHODCALLTYPE Release() override {
      auto remaining = InterlockedDecrement(&references);
      if (!remaining) {
        delete this;
      }
      return remaining;
    }

    /**
     * @brief No dispatch type information is required.
     */
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *count) override {
      *count = 0;
      return S_OK;
    }

    /**
     * @brief No dispatch type information is exposed.
     */
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Named dispatch is unused.
     */
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Dynamic dispatch is unused.
     */
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Expose only a caret role.
     */
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT, VARIANT *result) override {
      result->vt = VT_I4;
      result->lVal = role;
      return S_OK;
    }

    /**
     * @brief Expose visible or hidden provider state.
     */
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT, VARIANT *result) override {
      result->vt = VT_I4;
      result->lVal = state;
      return S_OK;
    }

    /**
     * @brief Return changing physical screen coordinates.
     */
    HRESULT STDMETHODCALLTYPE accLocation(LONG *left, LONG *top, LONG *cx, LONG *cy, VARIANT) override {
      *left = x;
      *top = y;
      *cx = width;
      *cy = height;
      return S_OK;
    }

    /**
     * @brief Unused get_accParent operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch **ppdispParent) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accChildCount operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accChildCount(LONG *pcountChildren) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accChild operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT varChildID, IDispatch **ppdispChild) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accName operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT varID, BSTR *pszName) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accValue operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT varID, BSTR *pszValue) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accDescription operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT varID, BSTR *pszDescription) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accHelp operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT varID, BSTR *pszHelp) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accHelpTopic operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR *pszHelpFile, VARIANT varID, LONG *pidTopic) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accKeyboardShortcut operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT varID, BSTR *pszKeyboardShortcut) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accFocus operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT *pvarID) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accSelection operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT *pvarID) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused get_accDefaultAction operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT varID, BSTR *pszDefaultAction) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused accSelect operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE accSelect(LONG flagsSelect, VARIANT varID) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused accNavigate operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE accNavigate(LONG navDir, VARIANT varStart, VARIANT *pvarEnd) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused accHitTest operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE accHitTest(LONG xLeft, LONG yTop, VARIANT *pvarID) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused accDoDefaultAction operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT varID) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused put_accName operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT varID, BSTR pszName) override {
      return E_NOTIMPL;
    }

    /**
     * @brief Unused put_accValue operation on the synthetic caret.
     */
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT varID, BSTR pszValue) override {
      return E_NOTIMPL;
    }
  };

  /**
   * @brief Respond only to caret-object requests on an owned inactive window.
   */
  LRESULT CALLBACK caret_provider_window(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams));
    }
    if (message == WM_GETOBJECT && static_cast<LONG>(lparam) == OBJID_CARET) {
      auto provider = reinterpret_cast<IAccessible *>(GetWindowLongPtrW(window, GWLP_USERDATA));
      if (provider) {
        return LresultFromObject(__uuidof(IAccessible), wparam, provider);
      }
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }
}  // namespace

TEST(WindowsFocusAnchor, TracksLegacyBrowserCaretMovementAndRejectsHiddenProviders) {
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  auto release_com = util::fail_guard([&] {
    if (SUCCEEDED(initialized)) {
      CoUninitialize();
    }
  });
  Microsoft::WRL::ComPtr<synthetic_accessible_caret_t> provider;
  provider.Attach(new synthetic_accessible_caret_t);
  const WNDCLASSW cls {.lpfnWndProc = caret_provider_window, .hInstance = GetModuleHandleW(nullptr), .lpszClassName = L"SunshineLegacyCaretRegression"};
  ASSERT_NE(RegisterClassW(&cls), 0);
  auto release_class = util::fail_guard([&] {
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
  });
  const auto window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls.lpszClassName, L"Sunshine legacy caret test", WS_POPUP | WS_VISIBLE, -32000, -32000, 100, 100, nullptr, nullptr, cls.hInstance, provider.Get());
  ASSERT_NE(window, nullptr);
  auto release_window = util::fail_guard([&] {
    DestroyWindow(window);
  });
  const auto id = reinterpret_cast<std::uintptr_t>(window);
  EXPECT_EQ(platf::dxgi::testing::legacy_caret(id), (video::focus_anchor::point_t {100, 213}));
  provider->x = 420;
  provider->y = 300;
  EXPECT_EQ(platf::dxgi::testing::legacy_caret(id), (video::focus_anchor::point_t {420, 313}));
  provider->state = STATE_SYSTEM_INVISIBLE;
  EXPECT_FALSE(platf::dxgi::testing::legacy_caret(id));
  provider->state = STATE_SYSTEM_OFFSCREEN;
  EXPECT_FALSE(platf::dxgi::testing::legacy_caret(id));
  provider->state = 0;
  provider->role = ROLE_SYSTEM_TEXT;
  EXPECT_FALSE(platf::dxgi::testing::legacy_caret(id));
}
#endif
