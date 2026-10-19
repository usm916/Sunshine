/**
 * @file src/platform/windows/viewport_crop.h
 * @brief Allocate mutable crop constants shared by production conversion and GPU regression tests.
 */
#pragma once
#include "display.h"
#include "src/video_viewport.h"

namespace platf::dxgi {
  /**
   * @brief Create a crop constant buffer that accepts per-frame UpdateSubresource calls.
   * @param device Conversion device.
   * @param buffer Receives the initialized buffer with cropping disabled.
   * @return D3D buffer creation status.
   */
  inline HRESULT create_viewport_crop_buffer(ID3D11Device *device, buf_t &buffer) {
    const ::video::viewport::shader_constants_t constants {};
    const D3D11_BUFFER_DESC desc {
      .ByteWidth = sizeof(constants),
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    const D3D11_SUBRESOURCE_DATA data {.pSysMem = &constants};
    return device->CreateBuffer(&desc, &data, &buffer);
  }
#ifdef SUNSHINE_TESTS
  namespace testing {
    /**
     * @brief Run the production D3D converter on a synthetic grayscale image and read two panned frames.
     * @param first Initial committed native-pixel crop.
     * @param second Crop used for the second conversion with unchanged output geometry.
     * @return Concatenated packed AYUV frames, or an empty vector on initialization failure.
     */
    std::vector<std::uint8_t> convert_viewport_frames(const ::video::viewport::frame_t &first, const ::video::viewport::frame_t &second);
  }  // namespace testing
#endif
}  // namespace platf::dxgi
