#version 450

layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 worldPosition;

layout(set = 0, binding = 0) uniform sampler2D baseTexture;
layout(set = 0, binding = 1) uniform sampler2D metallicRoughnessTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;

layout(location = 0) out vec4 baseMetallic;
layout(location = 1) out vec4 normalRoughness;

layout(push_constant) uniform Geometry {
    mat4 viewProjection;
    vec4 baseColor;
    vec4 material;
} push;

vec3 surfaceNormal(vec3 geometricNormal)
{
    vec3 positionDx = dFdx(worldPosition);
    vec3 positionDy = dFdy(worldPosition);
    vec2 uvDx = dFdx(uv);
    vec2 uvDy = dFdy(uv);
    float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
    if (determinant == 0.0)
        return geometricNormal;

    vec3 tangent = positionDx * uvDy.y - positionDy * uvDx.y;
    tangent -= geometricNormal * dot(geometricNormal, tangent);
    float tangentLengthSquared = dot(tangent, tangent);
    if (tangentLengthSquared == 0.0)
        return geometricNormal;

    float uvSign = sign(determinant);
    tangent *= uvSign * inversesqrt(tangentLengthSquared);
    vec3 bitangent = cross(geometricNormal, tangent);
    vec3 bitangentDerivative = positionDy * uvDx.x - positionDx * uvDy.x;
    if (uvSign * dot(bitangent, bitangentDerivative) < 0.0)
        bitangent = -bitangent;

    vec3 mappedNormal = textureGrad(normalTexture, uv, uvDx, uvDy).xyz * 2.0 - 1.0;
    mappedNormal.xy *= push.material.w;
    return normalize(tangent * mappedNormal.x + bitangent * mappedNormal.y
                     + geometricNormal * mappedNormal.z);
}

void main()
{
    vec4 base = texture(baseTexture, uv) * push.baseColor;
    if (push.material.z >= 0.0 && base.a < push.material.z)
        discard;

    vec3 geometricNormal = normalize(worldNormal);
    vec3 normal = surfaceNormal(geometricNormal);

    vec2 surface = texture(metallicRoughnessTexture, uv).bg * push.material.xy;
    baseMetallic = vec4(base.rgb, surface.x);
    normalRoughness = vec4(normal, clamp(surface.y, 0.045, 1.0));
}
