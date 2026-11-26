[shader("vertex")]
float4 main(uint vertexIndex : SV_VulkanVertexID) : SV_Position
{
    float2 vertex = float2((vertexIndex << 1) & 2, vertexIndex & 2);
    return float4(vertex * 2.0 - 1.0, 0.0, 1.0);
}
