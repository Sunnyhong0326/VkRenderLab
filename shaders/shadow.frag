#version 450

layout(location = 0) in vec2 uv;
layout(set = 0, binding = 0) uniform sampler2D baseTexture;

layout(push_constant) uniform Geometry {
    mat4 viewProjection;
    vec4 baseColor;
    vec4 material;
} push;

void main()
{
    if (push.material.z >= 0.0 && texture(baseTexture, uv).a * push.baseColor.a < push.material.z)
        discard;
}
