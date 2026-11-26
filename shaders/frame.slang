struct SceneLight
{
    float4 positionExtent;
    float4 colorIntensity;
    float4 direction;
    float4 axisX;
    int4 type;
};

struct Effects
{
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    float4x4 shadowViewProjection[6];
    float4 eyeSceneScale;
    int debugView;
    int shadowLightIndex;
    int enabledEffects;
    int lightCount;
    SceneLight lights[MAX_SCENE_LIGHTS];
};

[[vk::binding(6, 0)]] ConstantBuffer<Effects, Std140DataLayout> frame;

static const int DIRECTIONAL_LIGHT = 0;
static const int POINT_LIGHT = 1;
static const int RECTANGLE_LIGHT = 2;
static const int SSAO = 1 << 0;
static const int POINT_SHADOWS = 1 << 1;
static const int REFLECTIONS = 1 << 2;
static const int TOON = 1 << 3;
static const int FXAA = 1 << 4;
static const int VOLUMETRIC_LIGHT = 1 << 6;

bool enabled(int effect)
{
    return frame.debugView == 0 && (frame.enabledEffects & effect) != 0;
}

float3 worldPositionAt(float2 uv, float depth)
{
    float4 world = mul(frame.inverseViewProjection, float4(uv * 2.0 - 1.0, depth, 1.0));
    return world.xyz / world.w;
}
