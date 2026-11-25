layout(std140, set = 0, binding = 6) uniform Effects {
    mat4 viewProjection;
    mat4 inverseViewProjection;
    mat4 shadowViewProjection[6];
    vec4 eye;
    vec4 pointLight;
    vec4 areaLight;
    ivec4 settings;
} frame;

const int SSAO = 1 << 0;
const int POINT_SHADOWS = 1 << 1;
const int REFLECTIONS = 1 << 2;
const int TOON = 1 << 3;
const int FXAA = 1 << 4;
const int AREA_LIGHT = 1 << 5;
const int VOLUMETRIC_LIGHT = 1 << 6;

bool enabled(int effect)
{
    return frame.settings.x == 0 && (frame.settings.z & effect) != 0;
}

vec3 worldPositionAt(vec2 uv, float depth)
{
    vec4 world = frame.inverseViewProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return world.xyz / world.w;
}

#ifdef GBUFFER_SAMPLING
#ifdef MULTISAMPLED
#define GBUFFER_SAMPLER sampler2DMS
#else
#define GBUFFER_SAMPLER sampler2D
#endif

layout(set = 0, binding = 0) uniform GBUFFER_SAMPLER baseMetallicTexture;
layout(set = 0, binding = 1) uniform GBUFFER_SAMPLER normalRoughnessTexture;
layout(set = 0, binding = 2) uniform GBUFFER_SAMPLER depthTexture;

ivec2 gbufferSize()
{
#ifdef MULTISAMPLED
    return textureSize(depthTexture);
#else
    return textureSize(depthTexture, 0);
#endif
}

int gbufferSamples()
{
#ifdef MULTISAMPLED
    return textureSamples(depthTexture);
#else
    return 1;
#endif
}

int nearestSample(ivec2 pixel, out float depth)
{
    depth = texelFetch(depthTexture, pixel, 0).r;
    int nearest = 0;
    for (int i = 1; i < gbufferSamples(); ++i)
    {
        float candidate = texelFetch(depthTexture, pixel, i).r;
        if (candidate < depth)
        {
            depth = candidate;
            nearest = i;
        }
    }
    return nearest;
}
#endif

#ifdef SHADOW_SAMPLING
layout(set = 0, binding = 3) uniform sampler2D shadowAtlas;

int shadowFace(vec3 direction)
{
    vec3 magnitude = abs(direction);
    if (magnitude.x >= max(magnitude.y, magnitude.z))
        return direction.x >= 0.0 ? 0 : 1;
    if (magnitude.y >= magnitude.z)
        return direction.y >= 0.0 ? 2 : 3;
    return direction.z >= 0.0 ? 4 : 5;
}

float pointVisibility(vec3 position, bool filterShadow)
{
    if (!enabled(POINT_SHADOWS))
        return 1.0;
    int face = shadowFace(position - frame.pointLight.xyz);
    vec4 clip = frame.shadowViewProjection[face] * vec4(position, 1.0);
    if (clip.w <= 0.0)
        return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (ndc.z <= 0.0 || ndc.z >= 1.0)
        return 1.0;

    ivec2 tileSize = textureSize(shadowAtlas, 0) / ivec2(3, 2);
    ivec2 tile = ivec2(face % 3, face / 3) * tileSize;
    vec2 texel = (ndc.xy * 0.5 + 0.5) * vec2(tileSize);
    float receiver = ndc.z - 0.00015;
    if (!filterShadow)
    {
        ivec2 pixel = clamp(ivec2(texel), ivec2(0), tileSize - 1);
        return step(receiver, texelFetch(shadowAtlas, tile + pixel, 0).r);
    }

    // Bilinear PCF with each tap clamped to its own face.
    ivec2 base = ivec2(floor(texel - 0.5));
    vec2 weight = fract(texel - 0.5);
    vec2 rows = vec2(0.0);
    for (int y = 0; y < 2; ++y)
    {
        ivec2 a = clamp(base + ivec2(0, y), ivec2(0), tileSize - 1);
        ivec2 b = clamp(base + ivec2(1, y), ivec2(0), tileSize - 1);
        rows[y] = mix(step(receiver, texelFetch(shadowAtlas, tile + a, 0).r),
                      step(receiver, texelFetch(shadowAtlas, tile + b, 0).r), weight.x);
    }
    return mix(rows.x, rows.y, weight.y);
}
#endif
