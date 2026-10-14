/**
 * @file src_assets/windows/assets/shaders/directx/include/cursor_focus.hlsl
 * @brief Preserve cursor-area detail and average coarse background cells before encoding.
 */
cbuffer cursor_focus_cbuffer : register(b2) {
    float2 focus_center; ///< Normalized cursor coordinates in the capture texture.
    float2 focus_output_size; ///< Visible output size along the capture texture axes.
    float focus_radius; ///< Radius of the unchanged area in output pixels.
    float focus_feather; ///< Width of the transition to the coarse background.
    float focus_background_scale; ///< Coarse background cell size in output pixels.
    float focus_enabled; ///< Zero selects the original sampling path.
};

/**
 * @brief Sample detail at the cursor and a four-sample average elsewhere.
 * @param uv Normalized capture texture coordinates supplied by the vertex shader.
 * @return RGB values in the original capture colorspace, before transfer conversion.
 */
float3 cursor_focus_sample(float2 uv)
{
    float3 detail = image.Sample(def_sampler, uv, 0).rgb;
    if (focus_enabled == 0) {
        return detail;
    }
    float distance = length((uv - focus_center) * focus_output_size);
    if (distance <= focus_radius) {
        return detail;
    }
    float2 cell_size = focus_background_scale / focus_output_size;
    float2 center = (floor(uv / cell_size) + 0.5) * cell_size;
    float2 offset = cell_size * 0.25;
    float3 coarse = image.Sample(def_sampler, center + float2(-offset.x, -offset.y), 0).rgb;
    coarse += image.Sample(def_sampler, center + float2(offset.x, -offset.y), 0).rgb;
    coarse += image.Sample(def_sampler, center + float2(-offset.x, offset.y), 0).rgb;
    coarse += image.Sample(def_sampler, center + offset, 0).rgb;
    coarse *= 0.25;
    float blend = smoothstep(focus_radius, focus_radius + focus_feather, distance);
    return lerp(detail, coarse, blend);
}
