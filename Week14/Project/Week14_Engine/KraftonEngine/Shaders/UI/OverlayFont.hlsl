#include "Common/VertexLayouts.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D FontAtlas : register(t0);

PS_Input_TexColor VS(VS_Input_PTC input)
{
    PS_Input_TexColor output;
    output.position = float4(input.position, 1.0f);
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}

float4 PS(PS_Input_TexColor input) : SV_TARGET
{
    float4 col = FontAtlas.Sample(PointClampSampler, input.texcoord);

    if (col.r < 0.1f)
        discard;

    return float4(input.color.rgb, col.r * input.color.a);
}
