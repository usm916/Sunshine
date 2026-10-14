Texture2D image : register(t0);
SamplerState def_sampler : register(s0);

cbuffer color_matrix_cbuffer : register(b0) {
    float4 color_vec_y;
    float4 color_vec_u;
    float4 color_vec_v;
    float2 range_y;
    float2 range_uv;
};

#include "include/base_vs_types.hlsl"
#include "include/cursor_focus.hlsl"

float2 main_ps(vertex_t input) : SV_Target
{
#if defined(LEFT_SUBSAMPLING)
    float3 rgb_left = cursor_focus_sample(input.tex_right_left_center.xz);
    float3 rgb_right = cursor_focus_sample(input.tex_right_left_center.yz);
    float3 rgb = CONVERT_FUNCTION((rgb_left + rgb_right) * 0.5);
#elif defined(LEFT_SUBSAMPLING_SCALE)
    float3 rgb = cursor_focus_sample(input.tex_right_center_left_top.yw); // top-center
    rgb += cursor_focus_sample(input.tex_right_center_left_bottom.yw); // bottom-center
    rgb *= 2;
    rgb += cursor_focus_sample(input.tex_right_center_left_top.xw); // top-right
    rgb += cursor_focus_sample(input.tex_right_center_left_top.zw); // top-left
    rgb += cursor_focus_sample(input.tex_right_center_left_bottom.xw); // bottom-right
    rgb += cursor_focus_sample(input.tex_right_center_left_bottom.zw); // bottom-left
    rgb = CONVERT_FUNCTION(rgb * (1./8));
#elif defined(TOPLEFT_SUBSAMPLING)
    float3 rgb_top_left = cursor_focus_sample(input.tex_right_left_top.xz);
    float3 rgb_top_right = cursor_focus_sample(input.tex_right_left_top.yz);
    float3 rgb_bottom_left = cursor_focus_sample(input.tex_right_left_bottom.xz);
    float3 rgb_bottom_right = cursor_focus_sample(input.tex_right_left_bottom.yz);
    float3 rgb = CONVERT_FUNCTION((rgb_top_left + rgb_top_right + rgb_bottom_left + rgb_bottom_right) * 0.25);
#endif

    float u = dot(color_vec_u.xyz, rgb) + color_vec_u.w;
    float v = dot(color_vec_v.xyz, rgb) + color_vec_v.w;

    u = u * range_uv.x + range_uv.y;
    v = v * range_uv.x + range_uv.y;

    return float2(u, v);
}
