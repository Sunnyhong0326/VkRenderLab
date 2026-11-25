#version 450
#extension GL_GOOGLE_include_directive : require
#include "effects.glsl"

layout(set = 0, binding = 5) uniform sampler2D compositeColor;
layout(location = 0) out vec4 outputColor;

float luma(vec3 color)
{
    return sqrt(max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.0));
}

vec3 filterFXAA(vec2 uv, vec2 texelSize, vec3 center)
{
    float nw = luma(texture(compositeColor, uv + vec2(-1, -1) * texelSize).rgb);
    float ne = luma(texture(compositeColor, uv + vec2( 1, -1) * texelSize).rgb);
    float sw = luma(texture(compositeColor, uv + vec2(-1,  1) * texelSize).rgb);
    float se = luma(texture(compositeColor, uv + vec2( 1,  1) * texelSize).rgb);
    float low = min(luma(center), min(min(nw, ne), min(sw, se)));
    float high = max(luma(center), max(max(nw, ne), max(sw, se)));
    if (high - low < max(0.0312, high * 0.125))
        return center;

    vec2 direction = vec2(sw + se - nw - ne, nw + sw - ne - se);
    float reduce = max((nw + ne + sw + se) * 0.03125, 1.0 / 128.0);
    direction /= min(abs(direction.x), abs(direction.y)) + reduce;
    direction = clamp(direction, vec2(-8.0), vec2(8.0)) * texelSize;
    vec3 inner = 0.5 * (texture(compositeColor, uv - direction / 6.0).rgb +
                        texture(compositeColor, uv + direction / 6.0).rgb);
    vec3 outer = 0.5 * inner + 0.25 * (texture(compositeColor, uv - direction / 2.0).rgb +
                                       texture(compositeColor, uv + direction / 2.0).rgb);
    float filteredLuma = luma(outer);
    return filteredLuma < low || filteredLuma > high ? inner : outer;
}

void main()
{
    vec3 color = texelFetch(compositeColor, ivec2(gl_FragCoord.xy), 0).rgb;
    if (enabled(FXAA))
    {
        vec2 texelSize = 1.0 / vec2(textureSize(compositeColor, 0));
        color = filterFXAA(gl_FragCoord.xy * texelSize, texelSize, color);
    }
    outputColor = vec4(color, 1.0);
}
