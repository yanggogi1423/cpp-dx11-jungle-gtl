#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"
#include "Common/ConstantBuffers.hlsl"

Texture2D g_txColor : register(t0);
SamplerState g_Sample : register(s0);

struct PS_Input_ProjectionDecal
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float3 localPos : TEXCOORD0;
    float3 localNormal : TEXCOORD1;
};

PS_Input_ProjectionDecal VS(VS_Input_PNCT input)
{
    PS_Input_ProjectionDecal output;
    output.position = ApplyMVP(input.position);
    output.color = input.color * SectionColor * PrimitiveColor;
    output.localPos = input.position;
    output.localNormal = normalize(input.normal);
    return output;
}

float4 PS(PS_Input_ProjectionDecal input) : SV_TARGET
{
    if (abs(input.localPos.x) > 0.5f || abs(input.localPos.y) > 0.5f || abs(input.localPos.z) > 0.5f)
    {
        discard;
    }

    // Projection decals are authored to project along the component's default forward axis.
    // Use the aligned normal sign so artists do not need to add a corrective 180-degree rotation.
    const float projectorFacing = saturate(input.localNormal.x);
    const float facingFade = lerp(0.65f, 1.0f, projectorFacing);

    const float2 decalUV = float2(input.localPos.y + 0.5f, 0.5f - input.localPos.z);
    if (decalUV.x < 0.0f || decalUV.x > 1.0f || decalUV.y < 0.0f || decalUV.y > 1.0f)
    {
        discard;
    }

    float4 texColor = g_txColor.Sample(g_Sample, decalUV);
    if (texColor.a < 0.001f)
    {
        discard;
    }

    float4 finalColor = texColor * input.color;
    finalColor.a *= facingFade;
    finalColor.rgb = saturate(finalColor.rgb);
    finalColor.rgb = lerp(finalColor.rgb, WireframeRGB, bIsWireframe);
    return finalColor;
}

