#ifndef FORWARD_FOG_HLSLI
#define FORWARD_FOG_HLSLI

#include "Common/Functions.hlsli"
#include "Common/Fog.hlsli"

cbuffer ForwardFogParams : register(b7)
{
    float4 FwdFogColor;
    float FwdFogDensity;
    float FwdFogHeightFalloff;
    float FwdFogBaseHeight;
    float FwdFogStartDistance;
    float FwdFogCutoffDistance;
    float FwdFogMaxOpacity;
    float2 _fwdFogPad;
};

float GetForwardHeightFogFactor(float3 worldPos)
{
    return ComputeHeightFogFactor(
        worldPos, CameraWorldPos,
        FwdFogDensity, FwdFogHeightFalloff, FwdFogBaseHeight,
        FwdFogStartDistance, FwdFogCutoffDistance, FwdFogMaxOpacity);
}

float3 ApplyForwardHeightFog(float3 color, float3 worldPos)
{
    return lerp(color, FwdFogColor.rgb, GetForwardHeightFogFactor(worldPos));
}

#endif // FORWARD_FOG_HLSLI
