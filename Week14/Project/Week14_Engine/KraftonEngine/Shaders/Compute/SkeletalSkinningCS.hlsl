#include "Common/Skinning.hlsli"

struct FSkeletalSkinningSourceVertex
{
    float3 Position;
    float3 Normal;
    float4 Color;
    float2 TexCoord;
    float4 Tangent;
    int4 BoneIndices;
    float4 BoneWeights;
};

StructuredBuffer<FSkeletalSkinningSourceVertex> SourceVertices : register(t0);
RWByteAddressBuffer OutVertices : register(u0);

cbuffer SkinningParams : register(b0)
{
    uint VertexCount;
    uint3 Padding;
};

void StoreSkinnedVertex(uint vertexIndex, FSkeletalSkinningSourceVertex source, FSkinningResult skinned)
{
    const uint baseOffset = vertexIndex * 64;

    float3 normal = skinned.normal;
    if (dot(normal, normal) > 1.0e-8f)
    {
        normal = normalize(normal);
    }
    else
    {
        normal = source.Normal;
    }

    float3 tangent = skinned.tangent;
    if (dot(tangent, tangent) > 1.0e-8f)
    {
        tangent = normalize(tangent);
    }
    else
    {
        tangent = source.Tangent.xyz;
    }

    OutVertices.Store3(baseOffset + 0, asuint(skinned.position.xyz));
    OutVertices.Store3(baseOffset + 12, asuint(normal));
    OutVertices.Store4(baseOffset + 24, asuint(source.Color));
    OutVertices.Store2(baseOffset + 40, asuint(source.TexCoord));
    OutVertices.Store4(baseOffset + 48, asuint(float4(tangent, source.Tangent.w)));
}

[numthreads(128, 1, 1)]
void CSMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    const uint vertexIndex = DispatchThreadId.x;
    if (vertexIndex >= VertexCount)
    {
        return;
    }

    FSkeletalSkinningSourceVertex source = SourceVertices[vertexIndex];
    FSkinningResult skinned = ApplyLinearBlendSkinning(
        source.Position,
        source.Normal,
        source.Tangent.xyz,
        source.BoneIndices,
        source.BoneWeights);

    StoreSkinnedVertex(vertexIndex, source, skinned);
}
