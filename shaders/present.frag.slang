#include "frame.slang"

[[vk::binding(5, 0)]] Sampler2D<float4> compositeColor;

float luma(float3 color)
{
    return sqrt(max(dot(color, float3(0.2126, 0.7152, 0.0722)), 0.0));
}

float3 filterFXAA(float2 uv, float2 texelSize, float3 center)
{
    float nw = luma(compositeColor.Sample(uv + float2(-1, -1) * texelSize).rgb);
    float ne = luma(compositeColor.Sample(uv + float2( 1, -1) * texelSize).rgb);
    float sw = luma(compositeColor.Sample(uv + float2(-1,  1) * texelSize).rgb);
    float se = luma(compositeColor.Sample(uv + float2( 1,  1) * texelSize).rgb);
    float low = min(luma(center), min(min(nw, ne), min(sw, se)));
    float high = max(luma(center), max(max(nw, ne), max(sw, se)));
    if (high - low < max(0.0312, high * 0.125))
        return center;

    float2 direction = float2(sw + se - nw - ne, nw + sw - ne - se);
    float reduce = max((nw + ne + sw + se) * 0.03125, 1.0 / 128.0);
    direction /= min(abs(direction.x), abs(direction.y)) + reduce;
    direction = clamp(direction, float2(-8.0), float2(8.0)) * texelSize;
    float3 inner = 0.5 * (compositeColor.Sample(uv - direction / 6.0).rgb +
                        compositeColor.Sample(uv + direction / 6.0).rgb);
    float3 outer = 0.5 * inner + 0.25 * (compositeColor.Sample(uv - direction / 2.0).rgb +
                                       compositeColor.Sample(uv + direction / 2.0).rgb);
    float filteredLuma = luma(outer);
    return filteredLuma < low || filteredLuma > high ? inner : outer;
}

[shader("fragment")]
float4 main(float4 fragmentPosition : SV_Position) : SV_Target0
{
    float3 color = compositeColor.Load(int3(int2(fragmentPosition.xy), 0)).rgb;
    if (enabled(FXAA))
    {
        uint width, height;
        compositeColor.GetDimensions(width, height);
        float2 texelSize = 1.0 / float2(width, height);
        color = filterFXAA(fragmentPosition.xy * texelSize, texelSize, color);
    }
    return float4(color, 1.0);
}
