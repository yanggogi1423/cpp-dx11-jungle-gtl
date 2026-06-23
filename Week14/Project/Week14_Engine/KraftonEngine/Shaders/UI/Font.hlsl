#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D FontAtlas : register(t0);

PS_Input_TexColor VS(VS_Input_PTC input)
{
    PS_Input_TexColor output;
    output.position = ApplyVP(input.position);
    output.texcoord = input.texcoord;
    output.color = input.color;
    return output;
}

float4 PS(PS_Input_TexColor input) : SV_TARGET
{
    float4 col = FontAtlas.Sample(PointClampSampler, input.texcoord);
    float coverage = max(col.a, col.r);
    if (!bIsWireframe && ShouldDiscardFontPixel(coverage))
        discard;

    float alpha = bIsWireframe ? 1.0f : (coverage * input.color.a);
    return float4(ApplyWireframe(input.color.rgb), alpha);
}
