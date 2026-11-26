struct GeometryPush
{
    float4x4 viewProjection;
    float4 baseColor;
    float4 material;
};

[[vk::push_constant]] ConstantBuffer<GeometryPush> push;

struct GeometryVaryings
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] float3 worldNormal : NORMAL0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 worldPosition : TEXCOORD1;
};

struct ShadowVaryings
{
    float4 clipPosition : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
