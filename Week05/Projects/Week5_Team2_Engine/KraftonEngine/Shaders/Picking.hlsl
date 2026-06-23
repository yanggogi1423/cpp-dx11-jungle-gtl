#include "Common/Functions.hlsl"

cbuffer PickingConstants : register(b4)
{
    uint PickingId;
    float3 _pad1;
};

struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput VS(VSInput In)
{
    VSOutput Out;
    Out.Position = ApplyMVP(In.Position);
    return Out;
}

uint PS(VSOutput In) : SV_TARGET
{
    return PickingId;
}
