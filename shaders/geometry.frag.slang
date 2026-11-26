#include "geometry_io.slang"

[[vk::binding(0, 0)]] Sampler2D<float4> baseTexture;
[[vk::binding(1, 0)]] Sampler2D<float4> metallicRoughnessTexture;
[[vk::binding(2, 0)]] Sampler2D<float4> normalTexture;

struct GBufferOutputs
{
    float4 baseMetallic : SV_Target0;
    float4 normalRoughness : SV_Target1;
};

float3 surfaceNormal(float3 geometricNormal, float3 worldPosition, float2 uv)
{
    float3 positionDx = ddx(worldPosition);
    float3 positionDy = ddy(worldPosition);
    float2 uvDx = ddx(uv);
    float2 uvDy = ddy(uv);
    float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
    if (determinant == 0.0)
        return geometricNormal;

    float3 tangent = positionDx * uvDy.y - positionDy * uvDx.y;
    tangent -= geometricNormal * dot(geometricNormal, tangent);
    float tangentLengthSquared = dot(tangent, tangent);
    if (tangentLengthSquared == 0.0)
        return geometricNormal;

    float uvSign = sign(determinant);
    tangent *= uvSign * rsqrt(tangentLengthSquared);
    float3 bitangent = cross(geometricNormal, tangent);
    float3 bitangentDerivative = positionDy * uvDx.x - positionDx * uvDy.x;
    if (uvSign * dot(bitangent, bitangentDerivative) < 0.0)
        bitangent = -bitangent;

    float3 mappedNormal = normalTexture.SampleGrad(uv, uvDx, uvDy).xyz * 2.0 - 1.0;
    mappedNormal.xy *= push.material.w;
    return normalize(tangent * mappedNormal.x + bitangent * mappedNormal.y
                     + geometricNormal * mappedNormal.z);
}

[shader("fragment")]
GBufferOutputs main(GeometryVaryings input)
{
    float4 base = baseTexture.Sample(input.uv) * push.baseColor;
    if (push.material.z >= 0.0 && base.a < push.material.z)
        discard;

    float3 geometricNormal = normalize(input.worldNormal);
    float3 normal = surfaceNormal(geometricNormal, input.worldPosition, input.uv);

    float2 surface = metallicRoughnessTexture.Sample(input.uv).bg * push.material.xy;
    GBufferOutputs output;
    output.baseMetallic = float4(base.rgb, surface.x);
    output.normalRoughness = float4(normal, clamp(surface.y, 0.045, 1.0));
    return output;
}
