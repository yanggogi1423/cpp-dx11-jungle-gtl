#include "Common/ConstantBuffers.hlsl"
#include "Common/VertexLayouts.hlsl"

Texture2D g_txColor : register(t0);
Texture2D<float> DepthTex : register(t1);
SamplerState g_Sample : register(s0);

struct PS_Input_Decal
{
    float4 position : SV_POSITION;
    float3 localPos : TEXCOORD0;
};

cbuffer DecalObjectBuffer : register(b2)
{
    float4x4 InverseDecalModel;
}

PS_Input_Decal VS(VS_Input_PC input)
{
    PS_Input_Decal output;
    float4 world = mul(float4(input.position, 1.0f), Model);
    float4 view = mul(world, View);
    output.position = mul(view, Projection);
    output.localPos = input.position;
    return output;
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float2 ndcXY = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 clipPos = float4(ndcXY, depth, 1.0f);
    float4 worldPos = mul(clipPos, InverseViewProjection);
    return worldPos.xyz / max(worldPos.w, 0.0001f);
}

float4 PS(PS_Input_Decal input) : SV_TARGET
{
    // Small reconstruction bias to keep deferred decal slightly off the surface
    // and reduce edge flicker from depth precision/quantization.
    static const float DecalDepthBias = 1e-4f;
    static const float DecalBoundsEpsilon = 0.002f;

    uint width, height;
    DepthTex.GetDimensions(width, height);

    const int2 pixel = int2(input.position.xy);
    if (pixel.x < 0 || pixel.y < 0 || pixel.x >= int(width) || pixel.y >= int(height))
    {
        discard;
    }

    const float2 invSize = 1.0f / float2(width, height);
    const float2 uv = (float2(pixel) + 0.5f) * invSize;

    const float depth = DepthTex.Load(int3(pixel, 0));
    if (depth <= 0.0f || depth >= 1.0f)
    {
        discard;
    }

    const float depthBiased = saturate(depth - DecalDepthBias);
    if (depthBiased <= 0.0f || depthBiased >= 1.0f)
    {
        discard;
    }

    const float3 worldPos = ReconstructWorldPosition(uv, depthBiased);
    const float4 localDecalPos = mul(float4(worldPos, 1.0f), InverseDecalModel);

    if (abs(localDecalPos.x) > (0.5f + DecalBoundsEpsilon)
        || abs(localDecalPos.y) > (0.5f + DecalBoundsEpsilon)
        || abs(localDecalPos.z) > (0.5f + DecalBoundsEpsilon))
    {
        discard;
    }

    const float2 decalUV = float2(localDecalPos.y + 0.5f, 0.5f - localDecalPos.z);
    if (decalUV.x < 0.0f || decalUV.x > 1.0f || decalUV.y < 0.0f || decalUV.y > 1.0f)
    {
        discard;
    }

    float4 texColor = g_txColor.Sample(g_Sample, decalUV);
    if (texColor.a < 0.001f)
    {
        discard;
    }

    float4 finalColor = texColor * SectionColor * PrimitiveColor;
    finalColor.rgb = saturate(finalColor.rgb);
    finalColor.rgb = lerp(finalColor.rgb, WireframeRGB, bIsWireframe);
    
    return finalColor;
}
