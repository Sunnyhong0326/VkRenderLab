#include "frame.slang"
#include "gbuffer.slang"
#include "shadows.slang"

[[vk::binding(4, 0)]] Sampler2D<float4> litColor;

bool reflectionDepth(float3 position, int2 size, out float2 uv, out float separation)
{
    float4 clip = mul(frame.viewProjection, float4(position, 1.0));
    uv = float2(0.0);
    separation = -frame.eyeSceneScale.w;
    if (clip.w <= 0.0)
        return false;
    uv = clip.xy / clip.w * 0.5 + 0.5;
    if (any((uv < float2(0.0))) || any((uv >= float2(1.0))))
        return false;
    float depth = loadGBuffer(depthTexture, int2(uv * float2(size)), 0).r;
    if (depth < 1.0)
    {
        float3 surface = worldPositionAt(uv, depth);
        separation = length(position - frame.eyeSceneScale.xyz) - length(surface - frame.eyeSceneScale.xyz);
    }
    return true;
}

float3 screenReflection(float3 color, float3 position, float3 normal, float metallic, float roughness, int2 size)
{
    if (roughness >= 0.72)
        return color;
    float3 view = normalize(frame.eyeSceneScale.xyz - position);
    float3 direction = reflect(-view, normal);
    float scale = frame.eyeSceneScale.w;
    float3 origin = position + normal * (0.0025 * scale);
    float maxDistance = scale * 0.75;
    float previousDistance = 0.0;
    float previousSeparation = -scale;
    const int steps = 24;
    for (int i = 1; i <= steps; ++i)
    {
        float t = float(i) / float(steps);
        float distanceAlongRay = maxDistance * (0.012 + 0.988 * t * t);
        float2 uv;
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
                float2 midUV;
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
                float2 fromEdge = min(uv, 1.0 - uv);
                float edgeFade = smoothstep(0.0, 0.085, min(fromEdge.x, fromEdge.y));
                float fresnel = lerp(0.06, 0.8, metallic);
                fresnel += (1.0 - fresnel) * pow(1.0 - clamp(dot(view, normal), 0.0, 1.0), 5.0);
                float weight = fresnel * edgeFade * (1.0 - 0.55 * t) *
                               (1.0 - smoothstep(0.18, 0.72, roughness));
                return lerp(color, litColor.Sample(uv).rgb, weight);
            }
        }
        previousDistance = distanceAlongRay;
        previousSeparation = separation;
    }
    return color;
}

float toonOutline(int2 pixel, int2 size, float3 normal, float surfaceDistance)
{
    const int2 offsets[4] = {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)};
    float edge = 0.0;
    for (int i = 0; i < offsets.getCount(); ++i)
    {
        int2 neighbor = clamp(pixel + offsets[i], int2(0), size - 1);
        float depth;
        int sampleIndex = nearestSample(neighbor, depth);
        if (depth >= 1.0)
            return 1.0;
        float3 neighborNormal = normalize(loadGBuffer(normalRoughnessTexture, neighbor, sampleIndex).xyz);
        float3 world = worldPositionAt((float2(neighbor) + 0.5) / float2(size), depth);
        float depthChange = abs(length(world - frame.eyeSceneScale.xyz) - surfaceDistance) / max(surfaceDistance, 1e-6);
        edge = max(edge, max(smoothstep(0.014, 0.04, depthChange),
                            smoothstep(0.16, 0.42, 1.0 - dot(neighborNormal, normal))));
    }
    return edge;
}

float3 scatterPointLight(float3 color, float3 direction, float surfaceDistance)
{
    SceneLight light = frame.lights[frame.shadowLightIndex];
    float radius = light.positionExtent.w;
    float3 fromLight = frame.eyeSceneScale.xyz - light.positionExtent.xyz;
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
        float3 position = frame.eyeSceneScale.xyz + direction * distanceAlongRay;
        float3 toLight = light.positionExtent.xyz - position;
        float distanceSquared = dot(toLight, toLight);
        float falloff = max(0.0, 1.0 - distanceSquared / (radius * radius));
        float visibility = pointVisibility(position, false);
        scattering += transmittance * (1.0 - stepTransmittance) * visibility * falloff * falloff;
        transmittance *= stepTransmittance;
    }
    return color * transmittance + float3(0.92, 0.86, 0.74) * light.colorIntensity.rgb *
                                   (light.colorIntensity.a * scattering);
}

[shader("fragment")]
float4 main(float4 fragmentPosition : SV_Position) : SV_Target0
{
    int2 pixel = int2(fragmentPosition.xy);
    float3 color = litColor.Load(int3(pixel, 0)).rgb;
    bool doReflections = enabled(REFLECTIONS);
    bool doOutline = enabled(TOON);
    bool doVolume = enabled(VOLUMETRIC_LIGHT) && frame.shadowLightIndex >= 0;
    if (doReflections || doOutline || doVolume)
    {
        int2 size = gbufferSize();
        float2 uv = fragmentPosition.xy / float2(size);
        float depth;
        int sampleIndex = nearestSample(pixel, depth);
        float3 position = worldPositionAt(uv, depth);
        float surfaceDistance = length(position - frame.eyeSceneScale.xyz);
        if (depth < 1.0 && (doReflections || doOutline))
        {
            float4 normalRoughness = loadGBuffer(normalRoughnessTexture, pixel, sampleIndex);
            float3 normal = normalize(normalRoughness.xyz);
            if (doReflections)
            {
                float metallic = loadGBuffer(baseMetallicTexture, pixel, sampleIndex).a;
                color = screenReflection(color, position, normal, metallic, normalRoughness.w, size);
            }
            if (doOutline)
                color *= 1.0 - 0.88 * toonOutline(pixel, size, normal, surfaceDistance);
        }
        if (doVolume)
            color = scatterPointLight(color, normalize(position - frame.eyeSceneScale.xyz), surfaceDistance);
    }
    return float4(color, 1.0);
}
