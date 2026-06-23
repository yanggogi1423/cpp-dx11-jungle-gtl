#include "../Common/Common.hlsli"
#include "../Common/Skinning.hlsli"

struct VSInput
{
    float3 Position : POSITION;
};

float4 BuildShadowPosition(float3 localPosition)
{
    float4 worldPos = mul(float4(localPosition, 1.0f), Model);
    float4 post = worldPos;

#ifdef SHADOW_MAP_PSM
    float4 camClip = mul(post, VirtualViewProj);
    if (abs(camClip.w) > 1e-5f)
    {
        post = float4(camClip.xyz / camClip.w, 1.0f);
    }
#endif

    return mul(post, ShadowViewProj);
}

float4 ShadowVS(VSInput input) : SV_POSITION
{
    return BuildShadowPosition(input.Position);
}

float4 SkeletalShadowVS(SkeletalVSInput input) : SV_POSITION
{
    float3 skinnedPosition = ApplySkeletalSkinningPosition(input);
    return BuildShadowPosition(skinnedPosition);
}

void ShadowPS()
{
}
