#include "../Common/Common.hlsli"
#include "../Common/Skinning.hlsli"

struct VSInput
{
    float3 Position : POSITION;
};

float4 DepthPrepassVS(VSInput input) : SV_POSITION
{
    return ApplyMVP(input.Position);
}

float4 SkeletalDepthPrepassVS(SkeletalVSInput input) : SV_POSITION
{
    float3 position = ApplySkeletalSkinningPosition(input);
    return ApplyMVP(position);
}

void DepthPrepassPS() {}
