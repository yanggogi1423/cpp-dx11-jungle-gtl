#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

#define MAX_CAMERA_SHOCK_WAVES 4

struct FShockWaveGPU
{
    float4 CenterAndRadius;
    float4 DirectionAndStrength;
    float4 FalloffAgeDuration;
};

cbuffer CameraShockWaveCB : register(b2)
{
    uint ShockWaveCount;
    float2 InvViewportSize;
    float _Pad0;
    FShockWaveGPU ShockWaves[MAX_CAMERA_SHOCK_WAVES];
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float SoftBand(float value, float center, float width, float falloff)
{
    float x = (value - center) / max(width, 0.0001f);
    return pow(exp(-x * x), max(falloff, 0.01f));
}

float2 ResolveWaveOffset(float2 uv, FShockWaveGPU wave)
{
    float2 center = wave.CenterAndRadius.xy;
    float radius = max(wave.CenterAndRadius.z, 0.0001f);
    float width = max(wave.CenterAndRadius.w, 0.0001f);
    float2 direction = normalize(wave.DirectionAndStrength.xy + float2(0.0001f, 0.0f));
    float strength = wave.DirectionAndStrength.z;
    float stretch = max(wave.DirectionAndStrength.w, 0.0f);
    float falloff = max(wave.FalloffAgeDuration.x, 0.01f);
    float age01 = saturate(wave.FalloffAgeDuration.y);
    float enabled = wave.FalloffAgeDuration.w;

    float aspect = InvViewportSize.x > 0.0f ? InvViewportSize.y / InvViewportSize.x : 1.0f;
    float2 delta = uv - center;
    float2 metricDelta = float2(delta.x * aspect, delta.y);

    float2 metricDirection = normalize(float2(direction.x * aspect, direction.y) + float2(0.0001f, 0.0f));
    float2 tangent = float2(-metricDirection.y, metricDirection.x);
    float axial = dot(metricDelta, metricDirection);
    float lateral = dot(metricDelta, tangent);
    float dist = length(metricDelta);
    float2 radialDir = normalize(metricDelta + float2(0.0001f, 0.0f));

    float noiseSeed = dot(center, float2(37.23f, 91.17f)) + age01 * 8.31f;
    float angleNoise = sin(atan2(radialDir.y, radialDir.x) * 7.0f + noiseSeed);
    float axialNoise = sin((axial * 42.0f + lateral * 28.0f) + noiseSeed * 1.37f);
    float turbulent = angleNoise * 0.55f + axialNoise * 0.45f;
    float noisyDist = dist + turbulent * width * 0.55f;

    float ringA = SoftBand(noisyDist, radius, width * 1.45f, falloff);
    float ringB = SoftBand(noisyDist, radius + width * 2.35f, width * 1.05f, falloff * 0.92f);
    float ringC = SoftBand(noisyDist, max(radius - width * 2.0f, 0.0f), width * 0.95f, falloff * 0.85f);
    float ringD = SoftBand(noisyDist, radius + width * 4.75f, width * 0.82f, falloff * 0.75f);
    float layeredRing = ringA * 0.85f + ringB * 0.52f + ringC * 0.42f + ringD * 0.26f;
    layeredRing *= 0.78f + 0.22f * sin(noisyDist * 72.0f + turbulent * 2.1f + age01 * 3.5f);

    float behind = saturate((-axial + radius * 0.42f) / max(radius * (1.4f + stretch * 0.45f), 0.0001f));
    float frontCull = 1.0f - smoothstep(radius * 0.25f, radius * 1.25f, axial);
    float wakeWidth = radius * (0.42f + stretch * 0.12f) + width * 4.0f;
    float wakeCore = exp(-(lateral * lateral) / max(wakeWidth * wakeWidth, 0.0001f));
    float wakeBands = 0.55f + 0.45f * sin((-axial * 58.0f) + abs(lateral) * 76.0f + turbulent * 1.9f);
    float wake = behind * frontCull * wakeCore * wakeBands;

    float temporalFade = 1.0f - smoothstep(0.82f, 1.0f, age01);
    float ringAmount = layeredRing * 0.82f;
    float wakeAmount = wake * saturate(stretch * 0.35f) * 0.95f;
    float shimmer = sin((axial - lateral) * 93.0f + noiseSeed * 1.6f) * 0.24f;
    float2 metricOffset =
        radialDir * ringAmount +
        (metricDirection * 0.62f + tangent * shimmer) * wakeAmount;

    metricOffset *= strength * temporalFade * enabled;
    return metricOffset * float2(1.0f / max(aspect, 0.0001f), 1.0f);
}

float4 PS(PS_Input_UV input) : SV_Target
{
    float2 uv = input.uv;
    float2 offset = float2(0.0f, 0.0f);
    [unroll]
    for (uint i = 0; i < MAX_CAMERA_SHOCK_WAVES; ++i)
    {
        if (i >= ShockWaveCount)
        {
            break;
        }
        offset += ResolveWaveOffset(uv, ShockWaves[i]);
    }

    float2 sampleUV = saturate(uv - offset);
    return SceneColorTexture.SampleLevel(LinearClampSampler, sampleUV, 0);
}
