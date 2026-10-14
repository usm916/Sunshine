/**
 * @file src/platform/windows/cursor_focus_background.h
 * @brief D3D11 background cache shared by all hardware color-conversion paths.
 */
#pragma once

#include "display.h"
#include "src/video_cursor_focus_background.h"

namespace platf::dxgi {
  /**
   * @brief Retain capture pixels and update background tiles before color conversion.
   */
  class cursor_focus_background_t {
  public:
    /**
     * @brief Force a complete initialization on the next valid captured frame.
     */
    void reset() {
      scheduler_.reset();
      previous_focus_ = {};
    }

    /**
     * @brief Copy scheduled tiles and maintain fresh pixels around current and previous cursors.
     *
     * @param device Encoder D3D11 device.
     * @param context Encoder immediate context, with the capture mutex held.
     * @param input Current single-sample capture texture.
     * @param constants Validated focus geometry.
     * @param loop_ms Full refresh target, or minus one to freeze the background.
     * @param now_ns Monotonic conversion time in nanoseconds.
     * @param loop_frames Session-only sweep length in converted frames; zero uses milliseconds.
     * @return S_OK, or the D3D11 resource creation error.
     */
    HRESULT update(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *input, const ::video::cursor_focus::shader_constants_t &constants, int loop_ms, std::int64_t now_ns, int loop_frames = 0) {
      D3D11_TEXTURE2D_DESC desc {};
      input->GetDesc(&desc);
      if (!texture_ || desc.Width != width_ || desc.Height != height_ || desc.Format != format_) {
        texture2d_t texture;
        shader_res_t view;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        auto status = device->CreateTexture2D(&desc, nullptr, &texture);
        if (FAILED(status)) {
          return status;
        }
        status = device->CreateShaderResourceView(texture.get(), nullptr, &view);
        if (FAILED(status)) {
          return status;
        }
        texture_ = std::move(texture);
        view_ = std::move(view);
        width_ = desc.Width;
        height_ = desc.Height;
        format_ = desc.Format;
        reset();
      }
      ID3D11ShaderResourceView *unbound = nullptr;
      context->PSSetShaderResources(1, 1, &unbound);
      const auto work = scheduler_.advance(width_, height_, loop_ms, now_ns, loop_frames);
      if (work.full) {
        context->CopyResource(texture_.get(), input);
      } else {
        for (std::size_t index = 0; index < work.count; ++index) {
          copy_region(context, input, work.regions[index]);
        }
        // Refresh the old focus too: baked-in cursor pixels must not remain behind.
        copy_region(context, input, previous_focus_);
        copy_region(context, input, ::video::cursor_focus::focus_bounds(constants, width_, height_));
      }
      previous_focus_ = ::video::cursor_focus::focus_bounds(constants, width_, height_);
      return S_OK;
    }

    /**
     * @brief Obtain the background view for shader resource slot one.
     * @return Borrowed view pointer, owned by this cache.
     */
    ID3D11ShaderResourceView *view() {
      return view_.get();
    }

  private:
    /**
     * @brief Copy nonempty half-open bounds without changing source or destination coordinates.
     * @param context Immediate D3D11 context.
     * @param input Current captured texture.
     * @param rect Region to update in the background cache.
     */
    void copy_region(ID3D11DeviceContext *context, ID3D11Texture2D *input, const ::video::cursor_focus::pixel_rect_t &rect) {
      if (rect.left < rect.right && rect.top < rect.bottom) {
        const D3D11_BOX box {static_cast<UINT>(rect.left), static_cast<UINT>(rect.top), 0, static_cast<UINT>(rect.right), static_cast<UINT>(rect.bottom), 1};
        context->CopySubresourceRegion(texture_.get(), 0, box.left, box.top, 0, input, 0, &box);
      }
    }

    texture2d_t texture_;  ///< Retained capture-format background, private to this encoder.
    shader_res_t view_;  ///< Background resource view.
    UINT width_ = 0;  ///< Retained texture width.
    UINT height_ = 0;  ///< Retained texture height.
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;  ///< Retained texture format.
    ::video::cursor_focus::background_scheduler_t scheduler_;  ///< Bounded tile refresh schedule.
    ::video::cursor_focus::pixel_rect_t previous_focus_;  ///< Last sharp area, refreshed to erase old cursor pixels.
  };
}  // namespace platf::dxgi
