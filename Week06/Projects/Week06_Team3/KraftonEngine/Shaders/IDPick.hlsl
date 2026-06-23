#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"

Texture2D g_txColor : register(t0);
SamplerState g_Sample : register(s0);

struct VS_Output_PC
{
    float4 position : SV_POSITION;
};

struct VS_Output_Tex
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VS_Output_PC VS_PC(VS_Input_PC input)
{
    VS_Output_PC output;
    output.position = ApplyMVP(input.position);
    return output;
}

VS_Output_Tex VS_PNCT(VS_Input_PNCT input)
{
    VS_Output_Tex output;
    output.position = ApplyMVP(input.position);
    output.texcoord = input.texcoord;
    return output;
}

VS_Output_Tex VS_Billboard(VS_Input_PC input)
{
    VS_Output_Tex output;
    output.position = ApplyMVP(input.position);

    // Billboard quad local space:
    // y: [-0.5, 0.5], z: [-0.5, 0.5]
    const float u = input.position.y + 0.5f;
    const float v = 0.5f - input.position.z;
    output.texcoord = float2(u, v);
    return output;
}

uint PS_Primitive(VS_Output_PC input) : SV_TARGET
{
    return PickingId;
}

uint PS_TexturedCutout(VS_Output_Tex input) : SV_TARGET
{
    const float alpha = g_txColor.Sample(g_Sample, input.texcoord).a;
    if (alpha <= 1e-4f)
    {
        discard;
    }

    return PickingId;
}

// Billboard 컬러 패스(ShaderBillboard.hlsl)와 동일한 컷오프로 맞춘다.
// 화면에 실제로 그려지지 않는 픽셀(alpha < 0.5)은 ID 패스에서도 버린다.
uint PS_BillboardCutout(VS_Output_Tex input) : SV_TARGET
{
    const float2 uv = saturate(input.texcoord);
    const float alpha = g_txColor.Sample(g_Sample, uv).a;
    if (alpha < 0.01f)
    {
        discard;
    }

    return PickingId;
}
