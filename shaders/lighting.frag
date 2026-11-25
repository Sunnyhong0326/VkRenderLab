#version 450

#ifdef MULTISAMPLED
#define GBUFFER_SAMPLER sampler2DMS
#else
#define GBUFFER_SAMPLER sampler2D
#endif

layout(set = 0, binding = 0) uniform GBUFFER_SAMPLER baseMetallicTexture;
layout(set = 0, binding = 1) uniform GBUFFER_SAMPLER normalRoughnessTexture;
layout(set = 0, binding = 2) uniform GBUFFER_SAMPLER depthTexture;
layout(location = 0) out vec4 outputColor;

layout(push_constant) uniform Lighting {
    mat4 inverseViewProjection;
    vec4 eye;
    vec4 pointLight;
    vec4 settings;
} push;

vec3 worldPositionAt(vec2 ndc, float depth)
{
    vec4 world = push.inverseViewProjection * vec4(ndc, depth, 1.0);
    return world.xyz / world.w;
}

vec3 shade(vec3 base, float metallic, float roughness, vec3 normal, vec3 view, vec3 light)
{
    float diffuse = max(dot(normal, light), 0.0);
    vec3 halfway = normalize(light + view + vec3(1e-6));
    float exponent = mix(128.0, 4.0, roughness);
    float specular = pow(max(dot(normal, halfway), 0.0), exponent);
    vec3 reflectance = mix(vec3(0.04), base, metallic);
    return diffuse * (base * (1.0 - metallic) + reflectance * specular);
}

vec3 pointContribution(vec3 position, vec3 base, float metallic, float roughness, vec3 normal, vec3 view)
{
    vec3 toLight = push.pointLight.xyz - position;
    float distanceSquared = dot(toLight, toLight);
    float radius = push.pointLight.w;
    float radiusSquared = radius * radius;
    if (radius <= 0.0 || distanceSquared >= radiusSquared)
        return vec3(0.0);

    vec3 light = toLight * inversesqrt(max(distanceSquared, 1e-12));
    float falloff = 1.0 - distanceSquared / radiusSquared;
    return (falloff * falloff) * shade(base, metallic, roughness, normal, view, light);
}

vec3 shadeSample(ivec2 pixel, vec2 ndc, int sampleIndex)
{
    float depth = texelFetch(depthTexture, pixel, sampleIndex).r;
    if (depth >= 1.0)
        return vec3(0.025, 0.035, 0.055);

    vec4 baseMetallic = texelFetch(baseMetallicTexture, pixel, sampleIndex);
    vec4 normalRoughness = texelFetch(normalRoughnessTexture, pixel, sampleIndex);
    vec3 normal = normalize(normalRoughness.xyz);
    vec3 position = worldPositionAt(ndc, depth);
    vec3 view = normalize(push.eye.xyz - position);
    int mode = int(push.settings.x);
    vec3 color;
    switch (mode)
    {
        case 1: color = baseMetallic.rgb; break;
        case 2: color = normal * 0.5 + 0.5; break;
        case 3: color = vec3(1.0 - clamp(distance(position, push.eye.xyz) / push.pointLight.w, 0.0, 1.0)); break;
        case 4: color = vec3(baseMetallic.a, normalRoughness.a, 0.0); break;
        default:
            vec3 sun = normalize(vec3(0.55, 1.0, 0.35));
            color = baseMetallic.rgb * 0.22;
            color += shade(baseMetallic.rgb, baseMetallic.a, normalRoughness.a, normal, view, sun) * 1.25;
            color += push.settings.y * pointContribution(position, baseMetallic.rgb, baseMetallic.a,
                                                         normalRoughness.a, normal, view);
            color = color / (1.0 + color);
            break;
    }
    return color;
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
#ifdef MULTISAMPLED
    vec2 ndc = gl_FragCoord.xy / vec2(textureSize(depthTexture)) * 2.0 - 1.0;
    int sampleCount = textureSamples(depthTexture);
    vec3 color = vec3(0.0);
    for (int i = 0; i < sampleCount; ++i)
        color += shadeSample(pixel, ndc, i);
    outputColor = vec4(color / float(sampleCount), 1.0);
#else
    vec2 ndc = gl_FragCoord.xy / vec2(textureSize(depthTexture, 0)) * 2.0 - 1.0;
    outputColor = vec4(shadeSample(pixel, ndc, 0), 1.0);
#endif
}
