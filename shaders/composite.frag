#version 450
#extension GL_GOOGLE_include_directive : require
#define GBUFFER_SAMPLING
#define SHADOW_SAMPLING
#include "effects.glsl"

layout(set = 0, binding = 4) uniform sampler2D litColor;
layout(location = 0) out vec4 outputColor;

bool reflectionDepth(vec3 position, ivec2 size, out vec2 uv, out float separation)
{
    vec4 clip = frame.viewProjection * vec4(position, 1.0);
    if (clip.w <= 0.0)
        return false;
    uv = clip.xy / clip.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0))))
        return false;
    float depth = texelFetch(depthTexture, ivec2(uv * vec2(size)), 0).r;
    separation = -frame.pointLight.w;
    if (depth < 1.0)
    {
        vec3 surface = worldPositionAt(uv, depth);
        separation = length(position - frame.eye.xyz) - length(surface - frame.eye.xyz);
    }
    return true;
}

vec3 screenReflection(vec3 color, vec3 position, vec3 normal, float metallic, float roughness, ivec2 size)
{
    if (roughness >= 0.72)
        return color;
    vec3 view = normalize(frame.eye.xyz - position);
    vec3 direction = reflect(-view, normal);
    float scale = frame.pointLight.w;
    vec3 origin = position + normal * (0.0025 * scale);
    float maxDistance = scale * 0.75;
    float previousDistance = 0.0;
    float previousSeparation = -scale;
    const int steps = 24;
    for (int i = 1; i <= steps; ++i)
    {
        float t = float(i) / float(steps);
        float distanceAlongRay = maxDistance * (0.012 + 0.988 * t * t);
        vec2 uv;
        float separation;
        if (!reflectionDepth(origin + direction * distanceAlongRay, size, uv, separation))
            break;

        if (previousSeparation < 0.0 && separation >= 0.0)
        {
            float low = previousDistance;
            float high = distanceAlongRay;
            for (int j = 0; j < 4; ++j)
            {
                float mid = (low + high) * 0.5;
                vec2 midUV;
                float midSeparation;
                if (!reflectionDepth(origin + direction * mid, size, midUV, midSeparation) || midSeparation < 0.0)
                {
                    low = mid;
                }
                else
                {
                    high = mid;
                    uv = midUV;
                    separation = midSeparation;
                }
            }
            if (separation < scale * 0.016)
            {
                vec2 fromEdge = min(uv, 1.0 - uv);
                float edgeFade = smoothstep(0.0, 0.085, min(fromEdge.x, fromEdge.y));
                float fresnel = mix(0.06, 0.8, metallic);
                fresnel += (1.0 - fresnel) * pow(1.0 - clamp(dot(view, normal), 0.0, 1.0), 5.0);
                float weight = fresnel * edgeFade * (1.0 - 0.55 * t) *
                               (1.0 - smoothstep(0.18, 0.72, roughness));
                return mix(color, texture(litColor, uv).rgb, weight);
            }
        }
        previousDistance = distanceAlongRay;
        previousSeparation = separation;
    }
    return color;
}

float toonOutline(ivec2 pixel, ivec2 size, vec3 normal, float surfaceDistance)
{
    const ivec2 offsets[] = ivec2[](ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1));
    float edge = 0.0;
    for (int i = 0; i < offsets.length(); ++i)
    {
        ivec2 neighbor = clamp(pixel + offsets[i], ivec2(0), size - 1);
        float depth;
        int sampleIndex = nearestSample(neighbor, depth);
        if (depth >= 1.0)
            return 1.0;
        vec3 neighborNormal = normalize(texelFetch(normalRoughnessTexture, neighbor, sampleIndex).xyz);
        vec3 world = worldPositionAt((vec2(neighbor) + 0.5) / vec2(size), depth);
        float depthChange = abs(length(world - frame.eye.xyz) - surfaceDistance) / max(surfaceDistance, 1e-6);
        edge = max(edge, max(smoothstep(0.014, 0.04, depthChange),
                            smoothstep(0.16, 0.42, 1.0 - dot(neighborNormal, normal))));
    }
    return edge;
}

vec3 scatterPointLight(vec3 color, vec3 direction, float surfaceDistance)
{
    float radius = frame.pointLight.w;
    vec3 fromLight = frame.eye.xyz - frame.pointLight.xyz;
    float alongRay = dot(fromLight, direction);
    float discriminant = alongRay * alongRay + radius * radius - dot(fromLight, fromLight);
    if (discriminant <= 0.0)
        return color;

    float entry = max(0.0, -alongRay - sqrt(discriminant));
    float exitDistance = min(surfaceDistance, -alongRay + sqrt(discriminant));
    if (exitDistance <= entry)
        return color;

    const int steps = 12;
    float stepLength = (exitDistance - entry) / float(steps);
    float extinction = 0.34 / radius;
    float stepTransmittance = exp(-extinction * stepLength);
    float transmittance = 1.0;
    float scattering = 0.0;
    for (int i = 0; i < steps; ++i)
    {
        float distanceAlongRay = entry + (float(i) + 0.5) * stepLength;
        vec3 position = frame.eye.xyz + direction * distanceAlongRay;
        vec3 toLight = frame.pointLight.xyz - position;
        float distanceSquared = dot(toLight, toLight);
        float falloff = max(0.0, 1.0 - distanceSquared / (radius * radius));
        float visibility = pointVisibility(position, false);
        scattering += transmittance * (1.0 - stepTransmittance) * visibility * falloff * falloff;
        transmittance *= stepTransmittance;
    }
    return color * transmittance + vec3(0.92, 0.86, 0.74) * scattering;
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 color = texelFetch(litColor, pixel, 0).rgb;
    bool doReflections = enabled(REFLECTIONS);
    bool doOutline = enabled(TOON);
    bool doVolume = enabled(VOLUMETRIC_LIGHT) && frame.settings.y != 0;
    if (doReflections || doOutline || doVolume)
    {
        ivec2 size = gbufferSize();
        vec2 uv = gl_FragCoord.xy / vec2(size);
        float depth;
        int sampleIndex = nearestSample(pixel, depth);
        vec3 position = worldPositionAt(uv, depth);
        float surfaceDistance = length(position - frame.eye.xyz);
        if (depth < 1.0 && (doReflections || doOutline))
        {
            vec4 normalRoughness = texelFetch(normalRoughnessTexture, pixel, sampleIndex);
            vec3 normal = normalize(normalRoughness.xyz);
            if (doReflections)
            {
                float metallic = texelFetch(baseMetallicTexture, pixel, sampleIndex).a;
                color = screenReflection(color, position, normal, metallic, normalRoughness.w, size);
            }
            if (doOutline)
                color *= 1.0 - 0.88 * toonOutline(pixel, size, normal, surfaceDistance);
        }
        if (doVolume)
            color = scatterPointLight(color, normalize(position - frame.eye.xyz), surfaceDistance);
    }
    outputColor = vec4(color, 1.0);
}
