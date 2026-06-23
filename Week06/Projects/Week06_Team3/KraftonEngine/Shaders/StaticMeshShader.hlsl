#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"
#include "Common/ConstantBuffers.hlsl"

Texture2D g_txColor : register(t0);
SamplerState g_Sample : register(s0);

PS_Input_Full VS(VS_Input_PNCT input)
{
    PS_Input_Full output;
    output.position = ApplyMVP(input.position);
    output.normal = normalize(mul(input.normal, (float3x3) Model));
    output.color = input.color * SectionColor;
    output.worldPos = mul(float4(input.position, 1.0f), Model).xyz;

    float2 texcoord = input.texcoord;
    if (bIsUVScroll != 0)
    {
        texcoord.x += Time * 0.5f; // 가로 방향으로 스크롤 예시
    }
    output.texcoord = texcoord;

    return output;
}

float4 PS(PS_Input_Full input) : SV_TARGET
{
    float4 texColor = g_txColor.Sample(g_Sample, input.texcoord);

    // Unbound SRV는 (0,0,0,0)을 반환 — 텍스처 미바인딩 시 white로 대체
    if (texColor.a < 0.001f)
    {
        //알파값이 0에 가까운 정상 텍스처, 투명 텍스처에서 문제 발생 가능
        texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    //float3 lightDir = normalize(float3(1.0f, -1.0f, 1.0f));
    //float diffuse = max(dot(input.normal, -lightDir), 0.0f);
    //float ambient = 0.2f;
    
    // 픽셀의 기본적인 색상
    float4 finalColor = texColor * input.color /* * (diffuse + ambient)*/;

    for (uint i = 0; i < LocalTintCount; ++i)
    {
        float radius = LocalTints[i].PositionRadius.w;
        if (radius <= 0.0f)
        {
            continue;
        }

        //현재 픽셀과 로컬 틴트 중심 사이의 거리를 계산하여, 반경 내에서만 틴트 효과가 적용되도록 함
        float distanceToTintCenter = distance(input.worldPos, LocalTints[i].PositionRadius.xyz);
        float normalizedDistance = saturate(distanceToTintCenter / max(radius, 0.0001f));
        
        //멀어질 수록 약하게 작용
        float attenuation = pow(saturate(1.0f - normalizedDistance), max(LocalTints[i].Params.y, 0.0001f));
        float localTintWeight = saturate(LocalTints[i].Params.x * attenuation);
        finalColor.rgb = lerp(finalColor.rgb, LocalTints[i].Color.rgb, localTintWeight);
    }

    finalColor.rgb = saturate(finalColor.rgb);
    finalColor.a = texColor.a * input.color.a;

    return float4(ApplyWireframe(finalColor.rgb), finalColor.a);
}
