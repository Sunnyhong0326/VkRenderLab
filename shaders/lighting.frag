#version 450

#extension GL_GOOGLE_include_directive : require
#define GBUFFER_SAMPLING
#define SHADOW_SAMPLING
#include "effects.glsl"

layout(location = 0) out vec4 outputColor;

float ambientVisibility(ivec2 pixel, ivec2 size, vec2 uv)
{
    if (!enabled(SSAO))
        return 1.0;
    float depth;
    int sampleIndex = nearestSample(pixel, depth);
    if (depth >= 1.0)
        return 1.0;

    vec3 position = worldPositionAt(uv, depth);
    vec3 normal = normalize(texelFetch(normalRoughnessTexture, pixel, sampleIndex).xyz);
    float radius = frame.eyeSceneScale.w * 0.085;
    float perspectiveDepth = (frame.viewProjection * vec4(position, 1.0)).w;
    float projectionScale = length(vec3(frame.viewProjection[0].y, frame.viewProjection[1].y,
                                        frame.viewProjection[2].y));
    float screenRadius = clamp(radius * projectionScale * float(size.y) * 0.5 / perspectiveDepth, 1.0, 56.0);
    float occlusion = 0.0;
    float rotation = float((pixel.x + 2 * pixel.y) % 4) * 1.5707963;
    const int tapCount = 8;
    for (int i = 0; i < tapCount; ++i)
    {
        float angle = rotation + float(i) * 2.3999632;
        float ring = sqrt((float(i) + 0.5) / float(tapCount));
        ivec2 neighbor = pixel + ivec2(round(vec2(cos(angle), sin(angle)) * screenRadius * ring));
        if (any(lessThan(neighbor, ivec2(0))) || any(greaterThanEqual(neighbor, size)))
            continue;
        float neighborDepth = texelFetch(depthTexture, neighbor, 0).r;
        if (neighborDepth >= 1.0)
            continue;
        vec3 offset = worldPositionAt((vec2(neighbor) + 0.5) / vec2(size), neighborDepth) - position;
        float distanceToSample = length(offset);
        float horizon = dot(normal, offset) / max(distanceToSample, 1e-5);
        occlusion += max(horizon - 0.12, 0.0) * (1.0 - smoothstep(0.0, radius, distanceToSample));
    }
    return 1.0 - clamp(2.4 * occlusion / float(tapCount), 0.0, 0.85);
}

vec3 shade(vec3 base, float metallic, float roughness, vec3 normal, vec3 view, vec3 light)
{
    float diffuse = max(dot(normal, light), 0.0);
    if (enabled(TOON))
        diffuse = floor(diffuse * 4.0 + 0.5) * 0.25;
    vec3 halfway = normalize(light + view + vec3(1e-6));
    float exponent = mix(128.0, 4.0, roughness);
    float specular = pow(max(dot(normal, halfway), 0.0), exponent);
    vec3 reflectance = mix(vec3(0.04), base, metallic);
    return diffuse * (base * (1.0 - metallic) + reflectance * specular);
}

vec3 pointContribution(SceneLight source, bool castsShadow, vec3 position, vec3 base, float metallic,
                       float roughness, vec3 normal, vec3 view)
{
    vec3 toLight = source.positionExtent.xyz - position;
    float distanceSquared = dot(toLight, toLight);
    float radius = source.positionExtent.w;
    float radiusSquared = radius * radius;
    if (radius <= 0.0 || distanceSquared >= radiusSquared)
        return vec3(0.0);

    vec3 light = toLight * inversesqrt(max(distanceSquared, 1e-12));
    float falloff = 1.0 - distanceSquared / radiusSquared;
    float visibility = castsShadow ? pointVisibility(position + light * radius * 0.001, true) : 1.0;
    return visibility * (falloff * falloff) * shade(base, metallic, roughness, normal, view, light);
}

vec3 areaContribution(SceneLight source, vec3 position, vec3 base, float metallic, float roughness,
                      vec3 normal, vec3 view)
{
    const vec2 taps[] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    vec2 halfSize = source.positionExtent.w * vec2(1.0, 0.62);
    vec3 axisZ = cross(source.direction.xyz, source.axisX.xyz);
    float area = 4.0 * halfSize.x * halfSize.y;
    vec3 color = vec3(0.0);
    for (int i = 0; i < taps.length(); ++i)
    {
        vec2 offset = taps[i] * halfSize * 0.5773503;
        vec3 toLight = source.positionExtent.xyz + source.axisX.xyz * offset.x + axisZ * offset.y - position;
        float distanceSquared = dot(toLight, toLight);
        vec3 light = toLight * inversesqrt(max(distanceSquared, 1e-12));
        float emitter = max(dot(-light, source.direction.xyz), 0.0) * area / (area + distanceSquared);
        color += emitter * shade(base, metallic, roughness, normal, view, light);
    }
    return color * (2.4 / float(taps.length()));
}

vec3 directLighting(vec3 position, vec3 base, float metallic, float roughness, vec3 normal, vec3 view)
{
    vec3 total = vec3(0.0);
    for (int i = 0; i < frame.lightCount; ++i)
    {
        SceneLight source = frame.lights[i];
        vec3 contribution;
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

vec3 shadeSample(ivec2 pixel, vec2 uv, int sampleIndex, float ambient)
{
    float depth = texelFetch(depthTexture, pixel, sampleIndex).r;
    if (depth >= 1.0)
        return vec3(0.025, 0.035, 0.055);

    vec4 baseMetallic = texelFetch(baseMetallicTexture, pixel, sampleIndex);
    vec4 normalRoughness = texelFetch(normalRoughnessTexture, pixel, sampleIndex);
    vec3 normal = normalize(normalRoughness.xyz);
    vec3 position = worldPositionAt(uv, depth);
    vec3 view = normalize(frame.eyeSceneScale.xyz - position);
    int mode = frame.debugView;
    vec3 color;
    switch (mode)
    {
        case 1: color = baseMetallic.rgb; break;
        case 2: color = normal * 0.5 + 0.5; break;
        case 3: color = vec3(1.0 - clamp(distance(position, frame.eyeSceneScale.xyz) / frame.eyeSceneScale.w, 0.0, 1.0)); break;
        case 4: color = vec3(baseMetallic.a, normalRoughness.a, 0.0); break;
        default:
            color = baseMetallic.rgb * (0.22 * ambient);
            color += directLighting(position, baseMetallic.rgb, baseMetallic.a, normalRoughness.a, normal, view);
            color = color / (1.0 + color);
            break;
    }
    return color;
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 size = gbufferSize();
    vec2 uv = gl_FragCoord.xy / vec2(size);
    float ambient = ambientVisibility(pixel, size, uv);
    int sampleCount = gbufferSamples();
    vec3 color = vec3(0.0);
    for (int i = 0; i < sampleCount; ++i)
        color += shadeSample(pixel, uv, i, ambient);
    outputColor = vec4(color / float(sampleCount), 1.0);
}
