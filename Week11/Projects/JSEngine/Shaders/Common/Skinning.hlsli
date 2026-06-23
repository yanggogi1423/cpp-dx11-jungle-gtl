struct FBoneMatrix
{
    row_major float4x4 Mat;
};

StructuredBuffer<FBoneMatrix> BoneMatrices : register(t16);

cbuffer SkinningBuffer : register(b5)
{
    uint BoneCount;
    uint bUseGPUSkinning;
    uint BoneMatrixOffset;
    uint SkinningPadding;
};

struct SkeletalVSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float4 Tangent : TANGENT;
    float4 Color : COLOR;
    uint4 BoneIndices : BLENDINDICES;
    float4 BoneWeights : BLENDWEIGHT;
};

float3 ApplySkeletalSkinningPosition(SkeletalVSInput input)
{
    if (bUseGPUSkinning == 0)
    {
        return input.Position;
    }

    float validWeightSum = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        uint boneIndex = input.BoneIndices[i];
        float weight = input.BoneWeights[i];
        if (boneIndex < BoneCount && weight > 0.0f)
        {
            validWeightSum += weight;
        }
    }

    if (validWeightSum <= 1e-6f)
    {
        return input.Position;
    }

    float3 skinnedPosition = 0.0f;

    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        uint boneIndex = input.BoneIndices[j];
        float rawWeight = input.BoneWeights[j];

        if (boneIndex >= BoneCount || rawWeight <= 0.0f)
        {
            continue;
        }

        float weight = rawWeight / validWeightSum;
        uint matrixIndex = BoneMatrixOffset + boneIndex;
        skinnedPosition += mul(float4(input.Position, 1.0f), BoneMatrices[matrixIndex].Mat).xyz * weight;
    }

    return skinnedPosition;
}
