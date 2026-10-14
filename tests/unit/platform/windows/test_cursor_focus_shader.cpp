/**
 * @file tests/unit/platform/windows/test_cursor_focus_shader.cpp
 * @brief Validate actual cursor focus HLSL sampling and every conversion shader on D3D11 WARP.
 */
#include "../../../tests_common.h"

#ifdef _WIN32
  // standard includes
  #include <filesystem>
  #include <string>
  #include <vector>

  // platform includes
  #include <d3dcompiler.h>

  // local includes
  #include <src/config.h>
  #include <src/file_handler.h>
  #include <src/platform/windows/cursor_focus_background.h>
  #include <src/platform/windows/display.h>
  #include <src/video_cursor_focus.h>

using namespace platf::dxgi;
using namespace ::video::cursor_focus;

/**
 * @brief Render the production sampling helper into a small RGBA texture without touching the desktop.
 */
class CursorFocusShaderTest: public testing::Test {
protected:
  /**
   * @brief Compile one shader entry point and report any compiler diagnostics.
   * @param source HLSL source to compile.
   * @param entry Entry point name.
   * @param profile D3D shader profile.
   * @return Compiled bytecode, or an empty pointer on failure.
   */
  blob_t compile(const std::string &source, const char *entry, const char *profile) {
    blob_t code;
    blob_t errors;
    const auto status = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    EXPECT_TRUE(SUCCEEDED(status)) << (errors ? static_cast<const char *>(errors->GetBufferPointer()) : "");
    return code;
  }

  /**
   * @brief Create a WARP device and an alternating-pixel texture with a coordinate ramp.
   */
  void SetUp() override {
    ASSERT_TRUE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    source.resize(width * height * 4);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto offset = (y * width + x) * 4;
        source[offset] = (x + y) % 2 ? 255 : 0;
        source[offset + 1] = x * 2;
        source[offset + 2] = y * 2;
        source[offset + 3] = 255;
      }
    }
    D3D11_TEXTURE2D_DESC desc {
      .Width = width,
      .Height = height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
      .SampleDesc = {1, 0},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    };
    const D3D11_SUBRESOURCE_DATA data {.pSysMem = source.data(), .SysMemPitch = width * 4};
    ASSERT_TRUE(SUCCEEDED(device->CreateTexture2D(&desc, &data, &input)));
    ASSERT_TRUE(SUCCEEDED(device->CreateShaderResourceView(input.get(), nullptr, &input_view)));
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ASSERT_TRUE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &output)));
    ASSERT_TRUE(SUCCEEDED(device->CreateRenderTargetView(output.get(), nullptr, &output_view)));
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ASSERT_TRUE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)));

    const D3D11_BUFFER_DESC buffer_desc {
      .ByteWidth = sizeof(shader_constants_t),
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    ASSERT_TRUE(SUCCEEDED(device->CreateBuffer(&buffer_desc, nullptr, &constants_buffer)));
    D3D11_SAMPLER_DESC sampler_desc {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    ASSERT_TRUE(SUCCEEDED(device->CreateSamplerState(&sampler_desc, &sampler)));

    const auto helper_path = shader_dir / "include/cursor_focus.hlsl";
    const std::string shader = std::string {
                                 "Texture2D image : register(t0);\nSamplerState def_sampler : register(s0);\n"
                               } +
                               file_handler::read_file(helper_path.string().c_str()) + R"(
struct vertex_t { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
vertex_t main_vs(uint id : SV_VertexID) {
    float2 points[3] = {float2(-1,-1),float2(-1,3),float2(3,-1)};
    vertex_t v;
    v.pos = float4(points[id],0,1);
    v.uv = points[id] * float2(0.5,-0.5) + 0.5;
    return v;
}
float4 main_ps(vertex_t v) : SV_Target { return float4(cursor_focus_sample(v.uv),1); }
)";
    auto vs_code = compile(shader, "main_vs", "vs_5_0");
    auto ps_code = compile(shader, "main_ps", "ps_5_0");
    ASSERT_TRUE(vs_code);
    ASSERT_TRUE(ps_code);
    ASSERT_TRUE(SUCCEEDED(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vertex_shader)));
    ASSERT_TRUE(SUCCEEDED(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &pixel_shader)));
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_shader.get(), nullptr, 0);
    context->PSSetShader(pixel_shader.get(), nullptr, 0);
    context->PSSetSamplers(0, 1, &sampler);
    context->PSSetShaderResources(0, 1, &input_view);
    context->OMSetRenderTargets(1, &output_view, nullptr);
    const D3D11_VIEWPORT viewport {0, 0, width, height, 0, 1};
    context->RSSetViewports(1, &viewport);
  }

  /**
   * @brief Render and read back the actual sampling result.
   * @param constants Cursor geometry to upload.
   * @param bind Whether to bind the constant buffer, testing the default disabled path.
   * @return Packed RGBA output pixels, or an empty vector after readback failure.
   */
  std::vector<std::uint8_t> render(const shader_constants_t &constants, bool bind = true) {
    context->UpdateSubresource(constants_buffer.get(), 0, nullptr, &constants, 0, 0);
    ID3D11Buffer *buffer = bind ? constants_buffer.get() : nullptr;
    context->PSSetConstantBuffers(2, 1, &buffer);
    context->Draw(3, 0);
    context->CopyResource(staging.get(), output.get());
    D3D11_MAPPED_SUBRESOURCE mapped {};
    const auto status = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
    EXPECT_TRUE(SUCCEEDED(status));
    if (FAILED(status)) {
      return {};
    }
    std::vector<std::uint8_t> pixels(source.size());
    for (int y = 0; y < height; ++y) {
      std::copy_n(static_cast<const std::uint8_t *>(mapped.pData) + y * mapped.RowPitch, width * 4, pixels.data() + y * width * 4);
    }
    context->Unmap(staging.get(), 0);
    return pixels;
  }

  /**
   * @brief Replace captured pixels with a uniform color to detect stale versus fresh regions.
   * @param red Red channel value for every input pixel.
   */
  void replace_input(std::uint8_t red) {
    auto pixels = source;
    for (std::size_t offset = 0; offset < pixels.size(); offset += 4) {
      pixels[offset] = red;
      pixels[offset + 1] = 40;
      pixels[offset + 2] = 80;
    }
    context->UpdateSubresource(input.get(), 0, nullptr, pixels.data(), width * 4, 0);
  }

  /**
   * @brief Update and render the production GPU cache together with the production shader.
   * @param background Cache shared across calls.
   * @param constants Shape and cursor geometry.
   * @param loop_ms Background mode and target duration.
   * @param now_ns Simulated monotonic time.
   * @return Actual rendered pixels.
   */
  std::vector<std::uint8_t> render_background(cursor_focus_background_t &background, shader_constants_t constants, int loop_ms, std::int64_t now_ns) {
    EXPECT_EQ(background.update(device.get(), context.get(), input.get(), constants, loop_ms, now_ns), S_OK);
    auto *view = background.view();
    context->PSSetShaderResources(1, 1, &view);
    constants.history = 1;
    return render(constants);
  }

  static constexpr int width = 128;  ///< Test texture width.
  static constexpr int height = 96;  ///< Test texture height.
  const std::filesystem::path shader_dir = std::filesystem::path {SUNSHINE_SOURCE_DIR} / "src_assets/windows/assets/shaders/directx";  ///< Production shader directory.
  const geometry_t geometry {0, 0, width, height, width, height, 0};  ///< Unrotated visible output.
  std::vector<std::uint8_t> source;  ///< Known alternating detail and coordinate ramps.
  device_t device;  ///< Software D3D11 device independent of installed GPUs.
  device_ctx_t context;  ///< Immediate context.
  texture2d_t input;  ///< Immutable source texture.
  shader_res_t input_view;  ///< Source resource view.
  texture2d_t output;  ///< Render target.
  render_target_t output_view;  ///< Render target view.
  texture2d_t staging;  ///< CPU-readable staging texture.
  buf_t constants_buffer;  ///< Cursor geometry constant buffer.
  sampler_state_t sampler;  ///< Linear clamp sampler.
  vs_t vertex_shader;  ///< Fullscreen triangle vertex shader.
  ps_t pixel_shader;  ///< Production cursor sampling helper wrapped for RGBA output.
};

TEST_F(CursorFocusShaderTest, DisabledAndUnboundConstantsKeepEveryPixelUnchanged) {
  EXPECT_EQ(render({}), source);
  EXPECT_EQ(render({}, false), source);
  EXPECT_EQ(render(make_constants({true, 32, 4}, geometry, -1, 48)), source);
  EXPECT_EQ(render(make_constants({true, 32, 4}, geometry, 64, 48, false)), source);
}

TEST_F(CursorFocusShaderTest, PreservesCursorDetailSimplifiesBackgroundAndFeathersTheBoundary) {
  const auto pixels = render(make_constants({true, 32, 4}, geometry, 64, 48));
  ASSERT_EQ(pixels.size(), source.size());
  for (int y = 40; y < 56; ++y) {
    for (int x = 56; x < 72; ++x) {
      for (int channel = 0; channel < 4; ++channel) {
        const auto offset = (y * width + x) * 4 + channel;
        EXPECT_EQ(pixels[offset], source[offset]);
      }
    }
  }
  for (int y = 8; y < 12; ++y) {
    for (int x = 8; x < 12; ++x) {
      EXPECT_NEAR(pixels[(y * width + x) * 4], 128, 1);
    }
  }
  const auto transition = (48 * width + 100) * 4;
  EXPECT_GT(pixels[transition], source[transition]);
  EXPECT_LT(pixels[transition], 128);
}

TEST_F(CursorFocusShaderTest, MovesSharpAreaWithTheCursorWithoutMovingImageCoordinates) {
  const auto first = render(make_constants({true, 32, 4}, geometry, 64, 48));
  const auto second = render(make_constants({true, 32, 4}, geometry, 16, 16));
  ASSERT_EQ(first.size(), source.size());
  ASSERT_EQ(second.size(), source.size());
  const auto old_center = (48 * width + 64) * 4;
  const auto new_center = (16 * width + 16) * 4;
  EXPECT_EQ(first[old_center], source[old_center]);
  EXPECT_NEAR(second[old_center], 128, 1);
  EXPECT_EQ(second[new_center], source[new_center]);
  EXPECT_EQ(second[new_center + 1], source[new_center + 1]);
  EXPECT_EQ(second[new_center + 2], source[new_center + 2]);
}

TEST_F(CursorFocusShaderTest, RectanglePreservesWideDetailWithoutMakingHeightEqualToWidth) {
  settings_t settings {true, 32, 4};
  settings.shape = "rectangle";
  settings.width = 80;
  settings.height = 32;
  const auto pixels = render(make_constants(settings, geometry, 64, 48));
  for (int y = 35; y < 61; ++y) {
    for (int x = 26; x < 102; ++x) {
      const auto offset = (y * width + x) * 4;
      EXPECT_EQ(pixels[offset], source[offset]);
      EXPECT_EQ(pixels[offset + 1], source[offset + 1]);
      EXPECT_EQ(pixels[offset + 2], source[offset + 2]);
    }
  }
  EXPECT_NEAR(pixels[(8 * width + 64) * 4], 128, 1);
}

TEST_F(CursorFocusShaderTest, FrozenBackgroundRetainsVisitedPixelsAndKeepsTheMovingFocusLive) {
  settings_t settings {true, 32, 4};
  settings.shape = "rectangle";
  settings.width = settings.height = 32;
  cursor_focus_background_t background;
  EXPECT_EQ(background.view(), nullptr);
  const auto left = make_constants(settings, geometry, 20, 48);
  const auto right = make_constants(settings, geometry, 100, 48);
  render_background(background, left, -1, 0);
  replace_input(255);
  const auto first = render_background(background, left, -1, 100000000);
  EXPECT_EQ(first[(48 * width + 20) * 4], 255);
  EXPECT_NEAR(first[(8 * width + 60) * 4], 128, 1);
  replace_input(64);
  const auto moved = render_background(background, right, -1, 200000000);
  EXPECT_EQ(moved[(48 * width + 100) * 4], 64);
  EXPECT_EQ(moved[(48 * width + 20) * 4], 64);
  replace_input(0);
  const auto held = render_background(background, right, -1, 300000000);
  EXPECT_EQ(held[(48 * width + 100) * 4], 0);
  EXPECT_EQ(held[(48 * width + 20) * 4], 64);
  EXPECT_NEAR(held[(8 * width + 60) * 4], 128, 1);
  background.reset();
  const auto reset = render_background(background, right, -1, 400000000);
  EXPECT_EQ(reset[(8 * width + 60) * 4], 0);
}

TEST_F(CursorFocusShaderTest, RollingBackgroundRefreshesOneTileAndLeavesOtherTilesHeld) {
  cursor_focus_background_t background;
  const auto constants = make_constants({true, 32, 4}, geometry, 100, 48);
  render_background(background, constants, 1000, 0);
  replace_input(255);
  const auto first = render_background(background, constants, 1000, 33333334);
  EXPECT_EQ(first[(8 * width + 8) * 4], 255);
  EXPECT_NEAR(first[(8 * width + 48) * 4], 128, 1);
  EXPECT_EQ(first[(48 * width + 100) * 4], 255);
  const auto second = render_background(background, constants, 1000, 66666667);
  EXPECT_EQ(second[(8 * width + 28) * 4], 255);
  EXPECT_NEAR(second[(8 * width + 48) * 4], 128, 1);
}

TEST_F(CursorFocusShaderTest, BackgroundCacheReinitializesAfterDimensionAndFormatChanges) {
  cursor_focus_background_t background;
  auto constants = make_constants({true, 32, 4}, geometry, 64, 48);
  ASSERT_EQ(background.update(device.get(), context.get(), input.get(), constants, -1, 0), S_OK);
  D3D11_TEXTURE2D_DESC desc {};
  input->GetDesc(&desc);
  desc.Width = 64;
  texture2d_t resized;
  ASSERT_TRUE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &resized)));
  ASSERT_EQ(background.update(device.get(), context.get(), resized.get(), constants, -1, 1), S_OK);
  desc.Height = 64;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  texture2d_t reformatted;
  ASSERT_TRUE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &reformatted)));
  ASSERT_EQ(background.update(device.get(), context.get(), reformatted.get(), constants, -1, 2), S_OK);
}

TEST_F(CursorFocusShaderTest, BackgroundCacheRejectsUnsupportedViewsWithoutReplacingValidResources) {
  cursor_focus_background_t background;
  const auto constants = make_constants({true, 32, 4}, geometry, 64, 48);
  ASSERT_EQ(background.update(device.get(), context.get(), input.get(), constants, -1, 0), S_OK);
  auto *original_view = background.view();
  D3D11_TEXTURE2D_DESC desc {};
  input->GetDesc(&desc);
  desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
  texture2d_t typeless;
  ASSERT_TRUE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &typeless)));
  EXPECT_TRUE(FAILED(background.update(device.get(), context.get(), typeless.get(), constants, -1, 1)));
  EXPECT_EQ(background.view(), original_view);
}

TEST_F(CursorFocusShaderTest, FrozenCacheSkipsEmptyFocusRegions) {
  cursor_focus_background_t background;
  ASSERT_EQ(background.update(device.get(), context.get(), input.get(), {}, -1, 0), S_OK);
  replace_input(255);
  ASSERT_EQ(background.update(device.get(), context.get(), input.get(), {}, -1, 100000000), S_OK);
  auto *view = background.view();
  context->PSSetShaderResources(1, 1, &view);
  auto constants = make_constants({true, 32, 4}, geometry, 100, 48);
  constants.history = 1;
  const auto pixels = render(constants);
  EXPECT_NEAR(pixels[(8 * width + 8) * 4], 128, 1);
}

TEST_F(CursorFocusShaderTest, AutomaticNormalPresetMatchesUnmodifiedPixels) {
  settings_t saved {true, 96, 8, "rectangle", 640, 240, -1, true};
  const auto constants = make_constants(select_settings(saved, 3000), geometry, 64, 48);
  EXPECT_EQ(constants.enabled, 0);
  EXPECT_EQ(render(constants), source);
}

TEST_F(CursorFocusShaderTest, FrameBasedCacheUpdatesRowsInFiveFrames) {
  cursor_focus_background_t background;
  ASSERT_EQ(background.update(device.get(), context.get(), input.get(), {}, 0, 0, 5), S_OK);
  replace_input(255);
  ASSERT_EQ(background.update(device.get(), context.get(), input.get(), {}, 0, 0, 5), S_OK);
  auto *view = background.view();
  context->PSSetShaderResources(1, 1, &view);
  auto constants = make_constants({true, 32, 4}, geometry, 100, 70);
  constants.history = 1;
  const auto pixels = render(constants);
  EXPECT_EQ(pixels[(8 * width + 8) * 4], 255);
  EXPECT_NEAR(pixels[(40 * width + 8) * 4], 128, 1);
  for (int frame = 2; frame <= 5; ++frame) {
    ASSERT_EQ(background.update(device.get(), context.get(), input.get(), {}, 0, 0, 5), S_OK);
  }
  view = background.view();
  context->PSSetShaderResources(1, 1, &view);
  EXPECT_EQ(render(constants)[(40 * width + 8) * 4], 255);
}

TEST(CursorFocusFallbackTest, SoftwareDeviceRemainsAvailableWithFocusOnOrOff) {
  const auto original = config::video.cursor_focus.enabled;
  auto restore = util::fail_guard([&] {
    config::video.cursor_focus.enabled = original;
  });
  display_ddup_ram_t display;
  for (const bool enabled : {false, true}) {
    config::video.cursor_focus.enabled = enabled;
    auto device = display.make_avcodec_encode_device(platf::pix_fmt_e::nv12);
    ASSERT_NE(device, nullptr);
    device->set_cursor_focus({enabled, 96, 8});
  }
}

TEST(CursorFocusShaderCompilationTest, CompilesEveryProductionConversionVariant) {
  const auto directory = std::filesystem::path {SUNSHINE_SOURCE_DIR} / "src_assets/windows/assets/shaders/directx";
  int count = 0;
  for (const auto &entry : std::filesystem::directory_iterator {directory}) {
    const auto name = entry.path().filename().string();
    if (!name.starts_with("convert_") || name.find("_ps") == std::string::npos) {
      continue;
    }
    blob_t code;
    blob_t errors;
    const auto status = D3DCompileFromFile(entry.path().wstring().c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main_ps", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    EXPECT_TRUE(SUCCEEDED(status)) << name << ": " << (errors ? static_cast<const char *>(errors->GetBufferPointer()) : "");
    ++count;
  }
  EXPECT_GE(count, 15);
}
#endif
