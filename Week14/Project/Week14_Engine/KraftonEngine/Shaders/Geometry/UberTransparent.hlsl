// UberTransparent.hlsl - Surface transparent forward shader

#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Common/Skinning.hlsli"
#include "Common/Fog.hlsli"
#include "Common/NormalMapping.hlsli"

#if !defined(LIGHTING_MODEL_UNLIT)
#include "Common/ForwardLighting.hlsli"
#endif

#if !defined(LIGHTING_MODEL_GOURAUD) && !defined(LIGHTING_MODEL_LAMBERT) && !defined(LIGHTING_MODEL_PHONG) && !defined(LIGHTING_MODEL_UNLIT)
#define LIGHTING_MODEL_PHONG 1
#endif

Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture : register(t1);

cbuffer PerShader1 : register(b2)
{
    float4 SectionColor;
    float HasNormalMap;
    float Opacity;
    float Shininess;
    float _pad;
    float4 EmissiveColor;
    float EmissiveIntensity;
    float3 SpecularColor;
};

cbuffer ForwardFogParams : register(b7)
{
    float4 FwdFogColor;
    float  FwdFogDensity;
    float  FwdFogHeightFalloff;
    float  FwdFogBaseHeight;
    float  FwdFogStartDistance;
    float  FwdFogCutoffDistance;
    float  FwdFogMaxOpacity;
    float2 _fwdFogPad;
};

float3 GetMaterialEmissive()
{
    return EmissiveColor.rgb * max(EmissiveIntensity, 0.0f);
}

struct UberTransparentVS_Output
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float4 tangent : TANGENT;
#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    float3 litDiffuse : TEXCOORD2;
    float3 litSpecular : TEXCOORD3;
#endif
};

UberTransparentVS_Output BuildVS(float3 position, float3 normal, float4 color, float2 texcoord, float4 tangent)
{
    UberTransparentVS_Output output;

    float4 worldPos = mul(float4(position, 1.0f), Model);
    output.worldPos = worldPos.xyz;
    output.position = mul(mul(worldPos, View), Projection);
    output.normal = normalize(mul(normal, (float3x3)NormalMatrix));
    output.color = color * SectionColor;
    output.texcoord = texcoord;
    output.tangent = tangent;

#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    float3 N = output.normal;
    if (HasNormalMap >= 0.5)
    {
        float3 T = BuildOrthonormalTangent(N, mul(tangent.xyz, (float3x3)Model));
        float3 tangentNormal = SampleTangentSpaceNormalLevel(NormalTexture, LinearWrapSampler, texcoord, 0);
        N = ApplyTangentSpaceNormal(N, T, tangent.w, tangentNormal);
    }

    float3 V = normalize(CameraWorldPos - output.worldPos);
    output.litDiffuse = AccumulateDiffuseVS(output.worldPos, N);
    output.litSpecular = AccumulateSpecularVS(output.worldPos, N, V, Shininess);
#endif

    return output;
}

UberTransparentVS_Output VS_StaticMesh(VS_Input_PNCTT input)
{
    return BuildVS(input.position, input.normal, input.color, input.texcoord, input.tangent);
}

UberTransparentVS_Output VS_SkeletalMesh(VS_Input_PNCTTBB input)
{
    FSkinningResult skinned = ApplyLinearBlendSkinning(
        input.position,
        input.normal,
        input.tangent.xyz,
        input.boneIndices,
        input.boneWeights);

    return BuildVS(skinned.position.xyz, skinned.normal, input.color, input.texcoord, float4(skinned.tangent, input.tangent.w));
}

float4 PS(UberTransparentVS_Output input) : SV_TARGET
{
    float4 texColor = DiffuseTexture.Sample(LinearWrapSampler, input.texcoord);
    if (texColor.a < 0.001f)
        texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

    float4 baseColor = texColor * input.color;

    float3 N = normalize(input.normal);

#if !defined(LIGHTING_MODEL_GOURAUD)
    if (HasNormalMap >= 0.5)
    {
        float3 tangentNormal = SampleTangentSpaceNormal(NormalTexture, LinearWrapSampler, input.texcoord);
        N = ApplyTangentSpaceNormal(N, input.tangent.xyz, input.tangent.w, tangentNormal);
    }
#endif

    float3 V = normalize(CameraWorldPos - input.worldPos);

#if defined(LIGHTING_MODEL_UNLIT) && LIGHTING_MODEL_UNLIT
    float3 finalColor = baseColor.rgb + GetMaterialEmissive();
#else
    float3 diffuse = float3(0, 0, 0);
    float3 specular = float3(0, 0, 0);

#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    diffuse = input.litDiffuse;
    specular = input.litSpecular * saturate(SpecularColor);
#elif defined(LIGHTING_MODEL_LAMBERT) && LIGHTING_MODEL_LAMBERT
    diffuse = AccumulateDiffuse(input.worldPos, N, input.position);
#elif defined(LIGHTING_MODEL_PHONG) && LIGHTING_MODEL_PHONG
    diffuse = AccumulateDiffuse(input.worldPos, N, input.position);
    specular = AccumulateSpecular(input.worldPos, N, V, Shininess, input.position) * saturate(SpecularColor);
#endif

    float3 finalColor = baseColor.rgb * diffuse + specular + GetMaterialEmissive();
#endif

    float fwdFog = ComputeHeightFogFactor(
        input.worldPos, CameraWorldPos,
        FwdFogDensity, FwdFogHeightFalloff, FwdFogBaseHeight,
        FwdFogStartDistance, FwdFogCutoffDistance, FwdFogMaxOpacity);
    finalColor = lerp(finalColor, FwdFogColor.rgb, fwdFog);

    return float4(finalColor, baseColor.a * Opacity);
}
