#ifdef MULTISAMPLED
typedef Sampler2DMS<float4> GBufferTexture;
#else
typedef Sampler2D<float4> GBufferTexture;
#endif

[[vk::binding(0, 0)]] GBufferTexture baseMetallicTexture;
[[vk::binding(1, 0)]] GBufferTexture normalRoughnessTexture;
[[vk::binding(2, 0)]] GBufferTexture depthTexture;

float4 loadGBuffer(GBufferTexture image, int2 pixel, int sampleIndex)
{
#ifdef MULTISAMPLED
    return image.Load(pixel, sampleIndex);
#else
    return image.Load(int3(pixel, 0));
#endif
}

int2 gbufferSize()
{
    uint width, height;
#ifdef MULTISAMPLED
    uint samples;
    depthTexture.GetDimensions(width, height, samples);
#else
    depthTexture.GetDimensions(width, height);
#endif
    return int2(width, height);
}

int gbufferSamples()
{
#ifdef MULTISAMPLED
    uint width, height, samples;
    depthTexture.GetDimensions(width, height, samples);
    return int(samples);
#else
    return 1;
#endif
}

int nearestSample(int2 pixel, out float depth)
{
    depth = loadGBuffer(depthTexture, pixel, 0).r;
    int nearest = 0;
    for (int i = 1; i < gbufferSamples(); ++i)
    {
        float candidate = loadGBuffer(depthTexture, pixel, i).r;
        if (candidate < depth)
        {
            depth = candidate;
            nearest = i;
        }
    }
    return nearest;
}
