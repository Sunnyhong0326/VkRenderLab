#include "geometry_io.slang"

[[vk::binding(0, 0)]] Sampler2D<float4> baseTexture;

[shader("fragment")]
void main(ShadowVaryings input)
{
    if (push.material.z >= 0.0 && baseTexture.Sample(input.uv).a * push.baseColor.a < push.material.z)
        discard;
}
