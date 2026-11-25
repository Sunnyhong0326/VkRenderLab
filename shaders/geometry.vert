#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec2 uv;
layout(location = 2) out vec3 worldPosition;

layout(push_constant) uniform Geometry {
    mat4 viewProjection;
    vec4 baseColor;
    vec4 material;
} push;

void main()
{
    worldNormal = normal;
    uv = texCoord;
    worldPosition = position;
    gl_Position = push.viewProjection * vec4(position, 1.0);
}
