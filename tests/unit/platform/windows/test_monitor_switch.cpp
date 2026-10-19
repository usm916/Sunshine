/**
 * @file tests/unit/platform/windows/test_monitor_switch.cpp
 * @brief Native monitor sampling and deterministic controller gating/rate tests.
 */
#include "../../../tests_common.h"

#include <src/platform/windows/display.h>
#include <src/platform/windows/monitor_switch.h>
#include <wrl/client.h>
using namespace std::chrono_literals;
using namespace platf::dxgi;

TEST(WindowsMonitorSwitch, DisabledMissingAndSharedSessionsDoNotQueryWindows) {
  int calls = 0;
  monitor_switch_t controller({[&] {
                                 ++calls;
                                 return std::vector<::video::monitor_switch::monitor_t> {};
                               },
                               [&] {
                                 ++calls;
                                 return std::optional<std::pair<int, int>> {};
                               }});
  auto state = std::make_shared<::video::viewport::state_t>();
  EXPECT_FALSE(controller.poll("a", {}, 1));
  EXPECT_FALSE(controller.poll("a", state, 1));
  state->settings.monitor_switch = true;
  EXPECT_FALSE(controller.poll("a", state, 1));
  state->settings.enabled = true;
  EXPECT_FALSE(controller.poll("a", state, 2));
  state->contact(1, true);
  EXPECT_FALSE(controller.poll("a", state, 1));
  EXPECT_EQ(calls, 0);
}

TEST(WindowsMonitorSwitch, LimitsPollingAndCancelsForContactsMissingCursorAndRemovedDisplay) {
  int layouts = 0, cursors = 0;
  bool available = true, attached = true;
  monitor_switch_t controller({[&] {
                                 ++layouts;
                                 return attached ? std::vector<::video::monitor_switch::monitor_t> {{"a", {0, 0, 1000, 1000}}, {"b", {1000, 0, 2000, 1000}}} : std::vector<::video::monitor_switch::monitor_t> {};
                               },
                               [&] {
                                 ++cursors;
                                 return available ? std::optional<std::pair<int, int>> {{999, 500}} : std::nullopt;
                               }});
  auto state = std::make_shared<::video::viewport::state_t>();
  state->settings.enabled = true;
  state->settings.monitor_switch = true;
  const auto t = std::chrono::steady_clock::time_point {};
  EXPECT_FALSE(controller.poll("a", state, 1, t));
  EXPECT_EQ(layouts, 1);
  EXPECT_EQ(cursors, 1);
  EXPECT_FALSE(controller.poll("a", state, 1, t + 24ms));
  EXPECT_EQ(cursors, 1);
  EXPECT_FALSE(controller.poll("a", state, 1, t + 25ms));
  EXPECT_EQ(layouts, 1);
  EXPECT_EQ(cursors, 2);
  EXPECT_FALSE(controller.poll("a", state, 1, t + 599ms));
  EXPECT_EQ(layouts, 2);
  const auto request = controller.poll("a", state, 1, t + 625ms);
  ASSERT_TRUE(request);
  EXPECT_EQ(request->name, "b");
  state->contact(1, true);
  EXPECT_FALSE(controller.poll("a", state, 1, t + 700ms));
  state->contact(1, false);
  EXPECT_FALSE(controller.poll("a", state, 1, t + 800ms));
  available = false;
  EXPECT_FALSE(controller.poll("a", state, 1, t + 1400ms));
  available = true;
  EXPECT_FALSE(controller.poll("a", state, 1, t + 1500ms));
  attached = false;
  EXPECT_FALSE(controller.poll("a", state, 1, t + 2100ms));
}

TEST(WindowsMonitorSwitch, NativeLayoutAndCursorUseThePhysicalDesktop) {
  const auto layout = read_monitor_layout();
  ASSERT_FALSE(layout.empty());
  for (const auto &monitor : layout) {
    EXPECT_FALSE(monitor.name.empty());
    EXPECT_GT(monitor.bounds.right, monitor.bounds.left);
    EXPECT_GT(monitor.bounds.bottom, monitor.bounds.top);
  }
  const auto mouse = read_monitor_cursor();
  ASSERT_TRUE(mouse);
  EXPECT_TRUE(place_monitor_cursor(*mouse));
}

namespace {
  /**
   * @brief A concrete capture object exposing test HDR state without opening desktop duplication.
   */
  class key_display_t: public display_ddup_vram_t {
  public:
    bool hdr = false;  ///< Test source HDR state.

    /**
     * @brief Read the synthetic HDR flag.
     * @return Test HDR flag.
     */
    bool is_hdr() override {
      return hdr;
    }
  };
}  // namespace

TEST(WindowsMonitorSwitch, CompatibilityKeyUsesRealWarpAdapterAndRejectsMissingDevices) {
  EXPECT_FALSE(monitor_capture_key(nullptr));
  key_display_t display;
  EXPECT_FALSE(monitor_capture_key(&display));
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  ASSERT_TRUE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr)));
  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
  ASSERT_TRUE(SUCCEEDED(device.As(&dxgi)));
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
  ASSERT_TRUE(SUCCEEDED(dxgi->GetAdapter(&adapter)));
  ASSERT_TRUE(SUCCEEDED(adapter->QueryInterface(IID_IDXGIAdapter1, reinterpret_cast<void **>(&display.adapter))));
  const auto sdr = monitor_capture_key(&display);
  ASSERT_TRUE(sdr);
  EXPECT_FALSE(sdr->hdr);
  display.hdr = true;
  const auto hdr = monitor_capture_key(&display);
  ASSERT_TRUE(hdr);
  EXPECT_EQ(sdr->adapter, hdr->adapter);
  EXPECT_TRUE(hdr->hdr);
  EXPECT_FALSE(::video::monitor_switch::compatible(sdr, hdr));
}
