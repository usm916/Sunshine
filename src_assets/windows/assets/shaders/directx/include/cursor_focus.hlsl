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
    float2 focus_half_size; ///< Rectangle half-extents along capture-texture axes.
    float focus_rectangle; ///< One selects a rectangle, zero selects a circle.
    float focus_history; ///< One samples the held background texture.
};

Texture2D focus_background_image : register(t1); ///< Held capture pixels for rolling background updates.

/**
 * @brief Sample the held background when temporal refresh is active.
 * @param uv Normalized capture texture coordinates.
 * @return RGB values from the selected background source.
 */
float3 cursor_focus_background_sample(float2 uv)
{
    if (focus_history != 0) {
        return focus_background_image.Sample(def_sampler, uv, 0).rgb;
    }
    return image.Sample(def_sampler, uv, 0).rgb;
}

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
    float2 delta = (uv - focus_center) * focus_output_size;
    float distance = focus_rectangle != 0
        ? length(max(abs(delta) - focus_half_size, 0))
        : length(delta) - focus_radius;
    if (distance <= 0) {
        return detail;
    }
    float2 cell_size = focus_background_scale / focus_output_size;
    float2 center = (floor(uv / cell_size) + 0.5) * cell_size;
    float2 offset = cell_size * 0.25;
    float3 coarse = cursor_focus_background_sample(center + float2(-offset.x, -offset.y));
    coarse += cursor_focus_background_sample(center + float2(offset.x, -offset.y));
    coarse += cursor_focus_background_sample(center + float2(-offset.x, offset.y));
    coarse += cursor_focus_background_sample(center + offset);
    coarse *= 0.25;
    float blend = smoothstep(0, focus_feather, distance);
    return lerp(detail, coarse, blend);
}
