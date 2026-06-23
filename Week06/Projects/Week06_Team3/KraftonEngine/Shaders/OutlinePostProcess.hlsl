#include "Common/ConstantBuffers.hlsl"

Texture2D<float4> SceneColorTex : register(t0);
Texture2D<float4> OutlineMaskTex : register(t1);

struct PS_Input
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PS_Input VS(uint vertexID : SV_VertexID)
{
    PS_Input output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

//float4 PS(PS_Input input) : SV_TARGET
//{
//    const int2 coord = int2(input.position.xy);
//    const float4 sceneColor = SceneColorTex.Load(int3(coord, 0));
//    const float centerMask = OutlineMaskTex.Load(int3(coord, 0)).a;

//    // 내부 채우기 대신 외곽선만 그린다.
//    if (centerMask > 0.0f)
//    {
//        return sceneColor;
//    }

//    const int radius = max((int)OutlineThickness, 1);
//    float edgeAlpha = 0.0f;

//    [loop]
//    for (int y = -radius; y <= radius; ++y)
//    {
//        [loop]
//        for (int x = -radius; x <= radius; ++x)
//        {
//            if (x == 0 && y == 0)
//            {
//                continue;
//            }

//            const float neighborMask = OutlineMaskTex.Load(int3(coord + int2(x, y), 0)).a;
//            if (neighborMask <= 0.0f)
//            {
//                continue;
//            }

//            const float dist = sqrt((float)(x * x + y * y));
//            const float normDist = saturate(dist / max((float)radius, 0.0001f));
//            const float falloff = max(OutlineFalloff, 0.01f);
//            const float fade = pow(saturate(1.0f - normDist), falloff);
//            edgeAlpha = max(edgeAlpha, fade);
//        }
//    }

//    if (edgeAlpha <= 0.0f)
//    {
//        return sceneColor;
//    }

//    const float outlineAlpha = saturate(edgeAlpha * OutlineColor.a);
//    const float3 blended = lerp(sceneColor.rgb, OutlineColor.rgb, outlineAlpha);
//    return float4(blended, sceneColor.a);
//}

float4 PS(PS_Input input) : SV_TARGET
{
    const int2 coord = int2(input.position.xy);
    const float4 sceneColor = SceneColorTex.Load(int3(coord, 0));
    const float centerMask = OutlineMaskTex.Load(int3(coord, 0)).a;

    // 1. 기본값은 원본 씬 컬러
    float3 finalColor = sceneColor.rgb;

    // 2. 외곽선 계산 로직
    if (centerMask <= 0.0f)
    {
        const int radius = max((int) OutlineThickness, 1);
        float edgeAlpha = 0.0f;

        [loop]
        for (int y = -radius; y <= radius; ++y)
        {
            [loop]
            for (int x = -radius; x <= radius; ++x)
            {
                if (x == 0 && y == 0)
                    continue;

                const float neighborMask = OutlineMaskTex.Load(int3(coord + int2(x, y), 0)).a;
                if (neighborMask <= 0.0f)
                    continue;

                const float dist = sqrt((float) (x * x + y * y));
                const float normDist = saturate(dist / max((float) radius, 0.0001f));
                const float falloff = max(OutlineFalloff, 0.01f);
                const float fade = pow(saturate(1.0f - normDist), falloff);
                
                edgeAlpha = max(edgeAlpha, fade);
            }
        }

        // 외곽선이 발견되었다면 최종 색상을 블렌딩된 색상으로 교체
        if (edgeAlpha > 0.0f)
        {
            const float outlineAlpha = saturate(edgeAlpha * OutlineColor.a);
            finalColor = lerp(sceneColor.rgb, OutlineColor.rgb, outlineAlpha);
        }
    }

    // 3. 최종 결정된 RGB를 바탕으로 Luma(밝기) 계산
    // 현재는 다음 단계가 FXAA이므로, Luma를 Alpha 채널에 담아서 전달합니다.
    // 이후 다음단계가 FXAA가 아니게 되면 위치를 수정해야함
    float luma = dot(finalColor, float3(0.299f, 0.587f, 0.114f));

    // 4. RGB와 Luma(Alpha 채널)를 함께 반환!
    float outAlpha = (bOutputLumaToAlpha > 0.5f) ? luma : OutputAlpha;
    return float4(finalColor, outAlpha);
}
