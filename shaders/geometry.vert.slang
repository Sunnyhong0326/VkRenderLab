#include "geometry_io.slang"

struct Vertex
{
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

[shader("vertex")]
GeometryVaryings main(Vertex vertex)
{
    GeometryVaryings output;
    output.clipPosition = mul(push.viewProjection, float4(vertex.position, 1.0));
    output.worldNormal = vertex.normal;
    output.uv = vertex.uv;
    output.worldPosition = vertex.position;
    return output;
}
