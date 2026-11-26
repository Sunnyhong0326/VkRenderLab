#include "geometry_io.slang"

struct Vertex
{
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

[shader("vertex")]
ShadowVaryings main(Vertex vertex)
{
    ShadowVaryings output;
    output.clipPosition = mul(push.viewProjection, float4(vertex.position, 1.0));
    output.uv = vertex.uv;
    return output;
}
