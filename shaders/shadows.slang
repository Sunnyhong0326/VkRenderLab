[[vk::binding(3, 0)]] Sampler2D<float4> shadowAtlas;

int shadowFace(float3 direction)
{
    float3 magnitude = abs(direction);
    if (magnitude.x >= max(magnitude.y, magnitude.z))
        return direction.x >= 0.0 ? 0 : 1;
    if (magnitude.y >= magnitude.z)
        return direction.y >= 0.0 ? 2 : 3;
    return direction.z >= 0.0 ? 4 : 5;
}

float pointVisibility(float3 position, bool filterShadow)
{
    if (frame.shadowLightIndex < 0 || !enabled(POINT_SHADOWS))
        return 1.0;
    int face = shadowFace(position - frame.lights[frame.shadowLightIndex].positionExtent.xyz);
    float4 clip = mul(frame.shadowViewProjection[face], float4(position, 1.0));
    if (clip.w <= 0.0)
        return 1.0;
    float3 ndc = clip.xyz / clip.w;
    if (ndc.z <= 0.0 || ndc.z >= 1.0)
        return 1.0;

    uint width, height;
    shadowAtlas.GetDimensions(width, height);
    int2 tileSize = int2(width, height) / int2(3, 2);
    int2 tile = int2(face % 3, face / 3) * tileSize;
    float2 texel = (ndc.xy * 0.5 + 0.5) * float2(tileSize);
    float receiver = ndc.z - 0.00015;
    if (!filterShadow)
    {
        int2 pixel = clamp(int2(texel), int2(0), tileSize - 1);
        return step(receiver, shadowAtlas.Load(int3(tile + pixel, 0)).r);
    }

    int2 base = int2(floor(texel - 0.5));
    float2 weight = frac(texel - 0.5);
    float2 rows = float2(0.0);
    for (int y = 0; y < 2; ++y)
    {
        int2 a = clamp(base + int2(0, y), int2(0), tileSize - 1);
        int2 b = clamp(base + int2(1, y), int2(0), tileSize - 1);
        rows[y] = lerp(step(receiver, shadowAtlas.Load(int3(tile + a, 0)).r),
                      step(receiver, shadowAtlas.Load(int3(tile + b, 0)).r), weight.x);
    }
    return lerp(rows.x, rows.y, weight.y);
}
