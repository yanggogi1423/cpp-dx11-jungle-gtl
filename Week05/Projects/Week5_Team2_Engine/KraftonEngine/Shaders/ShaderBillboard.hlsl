// Billboard sprite shader.
// Vertices arrive already in world space (CPU-side billboard matrix applied by FBillboardProxy),
// so only VP is applied in the VS — identical to the Font/SubUV pattern but owned by the Proxy.
#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"

Texture2D    SpriteTexture : register(t0);
SamplerState SpriteSampler : register(s0);

PS_Input_Tex VS(VS_Input_PT input)
{
    PS_Input_Tex output;
    output.position = ApplyMVP(input.position);
    output.texcoord = input.texcoord;
    return output;
}

float4 PS(PS_Input_Tex input) : SV_TARGET
{
    float4 col = SpriteTexture.Sample(SpriteSampler, input.texcoord);
    if (col.a < 0.01f) discard;
    return col;
}
