#include "Common/VertexLayouts.hlsli"
#include "Common/ConstantBuffers.hlsli"
#include "Common/Skinning.hlsli"

Texture2D PickTexture : register(t0);

cbuffer EditorPickingBuffer : register(b12)
{
    uint PickingId;
    uint UseAlphaTest;
    float AlphaCutoff;
    float PickingPadding0;
    float2 UVOffset;
    float2 UVScale;
};

struct IDPickVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

IDPickVSOutput VS_StaticMesh(VS_Input_PNCTT input)
{
    IDPickVSOutput output;
    float4 worldPos = mul(float4(input.position, 1.0f), Model);
    output.position = mul(mul(worldPos, View), Projection);
    output.uv = UVOffset + input.texcoord * UVScale;
    return output;
}

IDPickVSOutput VS_EditorIcon(VS_Input_PNCT input)
{
    IDPickVSOutput output;
    float4 worldPos = mul(float4(input.position, 1.0f), Model);
    output.position = mul(mul(worldPos, View), Projection);
    output.uv = UVOffset + input.texcoord * UVScale;
    return output;
}

IDPickVSOutput VS_SkeletalMesh(VS_Input_PNCTTBB input)
{
    FSkinningResult skinned = ApplyLinearBlendSkinning(
        input.position,
        input.normal,
        input.tangent.xyz,
        input.boneIndices,
        input.boneWeights);

    IDPickVSOutput output;
    float4 worldPos = mul(skinned.position, Model);
    output.position = mul(mul(worldPos, View), Projection);
    output.uv = UVOffset + input.texcoord * UVScale;
    return output;
}

uint PS(IDPickVSOutput input) : SV_TARGET
{
    if (UseAlphaTest != 0)
    {
        float alpha = PickTexture.Sample(LinearWrapSampler, input.uv).a;
        if (alpha <= AlphaCutoff)
        {
            discard;
        }
    }

    return PickingId;
}
