#include "frame.slang"
#include "gbuffer.slang"
#include "shadows.slang"

float ambientVisibility(int2 pixel, int2 size, float2 uv)
{
    if (!enabled(SSAO))
        return 1.0;
    float depth;
    int sampleIndex = nearestSample(pixel, depth);
    if (depth >= 1.0)
        return 1.0;

    float3 position = worldPositionAt(uv, depth);
    float3 normal = normalize(loadGBuffer(normalRoughnessTexture, pixel, sampleIndex).xyz);
    float radius = frame.eyeSceneScale.w * 0.085;
    float perspectiveDepth = (mul(frame.viewProjection, float4(position, 1.0))).w;
    float projectionScale = length(frame.viewProjection[1].xyz);
    float screenRadius = clamp(radius * projectionScale * float(size.y) * 0.5 / perspectiveDepth, 1.0, 56.0);
    float occlusion = 0.0;
    float rotation = float((pixel.x + 2 * pixel.y) % 4) * 1.5707963;
    const int tapCount = 8;
    for (int i = 0; i < tapCount; ++i)
    {
        float angle = rotation + float(i) * 2.3999632;
        float ring = sqrt((float(i) + 0.5) / float(tapCount));
        int2 neighbor = pixel + int2(round(float2(cos(angle), sin(angle)) * screenRadius * ring));
        if (any((neighbor < int2(0))) || any((neighbor >= size)))
            continue;
        float neighborDepth = loadGBuffer(depthTexture, neighbor, 0).r;
        if (neighborDepth >= 1.0)
            continue;
        float3 offset = worldPositionAt((float2(neighbor) + 0.5) / float2(size), neighborDepth) - position;
        float distanceToSample = length(offset);
        float horizon = dot(normal, offset) / max(distanceToSample, 1e-5);
        occlusion += max(horizon - 0.12, 0.0) * (1.0 - smoothstep(0.0, radius, distanceToSample));
    }
    return 1.0 - clamp(2.4 * occlusion / float(tapCount), 0.0, 0.85);
}

float3 shade(float3 base, float metallic, float roughness, float3 normal, float3 view, float3 light)
{
    float diffuse = max(dot(normal, light), 0.0);
    if (enabled(TOON))
        diffuse = floor(diffuse * 4.0 + 0.5) * 0.25;
    float3 halfway = normalize(light + view + float3(1e-6));
    float exponent = lerp(128.0, 4.0, roughness);
    float specular = pow(max(dot(normal, halfway), 0.0), exponent);
    float3 reflectance = lerp(float3(0.04), base, metallic);
    return diffuse * (base * (1.0 - metallic) + reflectance * specular);
}

float3 pointContribution(SceneLight source, bool castsShadow, float3 position, float3 base, float metallic,
                       float roughness, float3 normal, float3 view)
{
    float3 toLight = source.positionExtent.xyz - position;
    float distanceSquared = dot(toLight, toLight);
    float radius = source.positionExtent.w;
    float radiusSquared = radius * radius;
    if (radius <= 0.0 || distanceSquared >= radiusSquared)
        return float3(0.0);

    float3 light = toLight * rsqrt(max(distanceSquared, 1e-12));
    float falloff = 1.0 - distanceSquared / radiusSquared;
    float visibility = castsShadow ? pointVisibility(position + light * radius * 0.001, true) : 1.0;
    return visibility * (falloff * falloff) * shade(base, metallic, roughness, normal, view, light);
}

float3 areaContribution(SceneLight source, float3 position, float3 base, float metallic, float roughness,
                      float3 normal, float3 view)
{
    const float2 taps[4] = {float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0), float2(1.0, 1.0)};
    float2 halfSize = source.positionExtent.w * float2(1.0, 0.62);
    float3 axisZ = cross(source.direction.xyz, source.axisX.xyz);
    float area = 4.0 * halfSize.x * halfSize.y;
    float3 color = float3(0.0);
    for (int i = 0; i < taps.getCount(); ++i)
    {
        float2 offset = taps[i] * halfSize * 0.5773503;
        float3 toLight = source.positionExtent.xyz + source.axisX.xyz * offset.x + axisZ * offset.y - position;
        float distanceSquared = dot(toLight, toLight);
        float3 light = toLight * rsqrt(max(distanceSquared, 1e-12));
        float emitter = max(dot(-light, source.direction.xyz), 0.0) * area / (area + distanceSquared);
        color += emitter * shade(base, metallic, roughness, normal, view, light);
    }
    return color * (2.4 / float(taps.getCount()));
}

float3 directLighting(float3 position, float3 base, float metallic, float roughness, float3 normal, float3 view)
{
    float3 total = float3(0.0);
    for (int i = 0; i < frame.lightCount; ++i)
    {
        SceneLight source = frame.lights[i];
        float3 contribution;
        switch (source.type.x)
        {
            case DIRECTIONAL_LIGHT:
                contribution = shade(base, metallic, roughness, normal, view, -source.direction.xyz);
                break;
            case POINT_LIGHT:
                contribution = pointContribution(source, i == frame.shadowLightIndex, position,
                                                 base, metallic, roughness, normal, view);
                break;
            case RECTANGLE_LIGHT:
                contribution = areaContribution(source, position, base, metallic, roughness, normal, view);
                break;
            default:
                continue;
        }
        total += contribution * source.colorIntensity.rgb * source.colorIntensity.a;
    }
    return total;
}

float3 shadeSample(int2 pixel, float2 uv, int sampleIndex, float ambient)
{
    float depth = loadGBuffer(depthTexture, pixel, sampleIndex).r;
    if (depth >= 1.0)
        return float3(0.025, 0.035, 0.055);

    float4 baseMetallic = loadGBuffer(baseMetallicTexture, pixel, sampleIndex);
    float4 normalRoughness = loadGBuffer(normalRoughnessTexture, pixel, sampleIndex);
    float3 normal = normalize(normalRoughness.xyz);
    float3 position = worldPositionAt(uv, depth);
    float3 view = normalize(frame.eyeSceneScale.xyz - position);
    int mode = frame.debugView;
    float3 color;
    switch (mode)
    {
        case 1: color = baseMetallic.rgb; break;
        case 2: color = normal * 0.5 + 0.5; break;
        case 3: color = float3(1.0 - clamp(distance(position, frame.eyeSceneScale.xyz) / frame.eyeSceneScale.w, 0.0, 1.0)); break;
        case 4: color = float3(baseMetallic.a, normalRoughness.a, 0.0); break;
        default:
            color = baseMetallic.rgb * (0.22 * ambient);
            color += directLighting(position, baseMetallic.rgb, baseMetallic.a, normalRoughness.a, normal, view);
            color = color / (1.0 + color);
            break;
    }
    return color;
}

[shader("fragment")]
float4 main(float4 fragmentPosition : SV_Position) : SV_Target0
{
    int2 pixel = int2(fragmentPosition.xy);
    int2 size = gbufferSize();
    float2 uv = fragmentPosition.xy / float2(size);
    float ambient = ambientVisibility(pixel, size, uv);
    int sampleCount = gbufferSamples();
    float3 color = float3(0.0);
    for (int i = 0; i < sampleCount; ++i)
        color += shadeSample(pixel, uv, i, ambient);
    return float4(color / float(sampleCount), 1.0);
}
