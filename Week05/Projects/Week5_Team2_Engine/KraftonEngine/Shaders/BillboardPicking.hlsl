#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"

cbuffer PickingConstants : register(b4)
{
    uint PickingId;
    float3 _pad1;
};

Texture2D    SpriteTexture : register(t0);
SamplerState SpriteSampler : register(s0);

PS_Input_Tex VS(VS_Input_PT In)
{
    PS_Input_Tex Out;
    Out.position = ApplyMVP(In.position);
    Out.texcoord = In.texcoord;
    return Out;
}

uint PS(PS_Input_Tex In) : SV_TARGET
{
    float4 col = SpriteTexture.Sample(SpriteSampler, In.texcoord);
    if (col.a < 0.01f)
    {
        discard;
    }
    return PickingId;
}
