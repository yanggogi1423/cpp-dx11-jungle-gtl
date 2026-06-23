// Generated from Content/Material/Auto/rus_build_dec_01.uasset
// Domain: Surface

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

struct FMaterialPixelInput
{
    float2 UV0;
    float2 UV1;
    float2 UV2;
    float4 ParticleColor;
    float4 VertexColor;
    float  Time;
    float  SubImageIndex;
    float4 DynamicParam;
};

struct FMaterialResult
{
    float3 BaseColor;
    float3 Normal;
    float Roughness;
    float Metallic;
    float3 Emissive;
    float Opacity;
};

float MaterialRoughnessToShininess(float Roughness)
{
    float R = saturate(Roughness);
    return lerp(256.0f, 2.0f, R * R);
}

float3 ApplyMaterialMetallicDiffuse(float3 BaseColor, float Metallic)
{
    return BaseColor * (1.0f - saturate(Metallic));
}

float3 ApplyMaterialMetallicSpecular(float3 SpecularLight, float3 BaseColor, float Metallic)
{
    float M = saturate(Metallic);
    float3 SpecularColor = lerp(float3(0.04f, 0.04f, 0.04f), BaseColor, M);
    return SpecularLight * SpecularColor;
}

Texture2D Tex_Diffuse : register(t0);

cbuffer PerMaterial : register(b2)
{
    float4 Param_EmissiveColor;
    float Param_EmissiveIntensity;
    float3 _Pad0;
};

float3 GetCommonMaterialEmissive()
{
    return Param_EmissiveColor.rgb * max(Param_EmissiveIntensity, 0.0f);
}

FMaterialResult EvaluateMaterial(FMaterialPixelInput Input)
{
    float4 n_17 = Tex_Diffuse.Sample(LinearWrapSampler, Input.UV0);
    float n_3 = 0.000000f;
    FMaterialResult Result;
    Result.BaseColor = (n_17).rgb;
    Result.Normal = float3(0, 0, 1);
    Result.Roughness = 0.5f;
    Result.Metallic = 0.0f;
    Result.Emissive = float3(0, 0, 0);
    Result.Opacity = n_3;
    return Result;
}


struct MaterialSurfaceVSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

MaterialSurfaceVSOutput VS(VS_Input_PNCTT input)
{
    MaterialSurfaceVSOutput output;
    float4 worldPos = mul(float4(input.position, 1.0f), Model);
    output.worldPos = worldPos.xyz;
    output.position = mul(mul(worldPos, View), Projection);
    output.normal = normalize(mul(input.normal, (float3x3)NormalMatrix));
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}


float4 PS(MaterialSurfaceVSOutput input) : SV_TARGET
{

    FMaterialPixelInput MaterialInput;
    MaterialInput.UV0           = input.texcoord;
    MaterialInput.UV1           = float2(0, 0);
    MaterialInput.UV2           = float2(0, 0);
    MaterialInput.ParticleColor = float4(1, 1, 1, 1);
    MaterialInput.VertexColor   = input.color;
    MaterialInput.Time          = Time;
    MaterialInput.SubImageIndex = 0.0f;
    MaterialInput.DynamicParam  = float4(0, 0, 0, 0);

    FMaterialResult Result = EvaluateMaterial(MaterialInput);
    float3 N = normalize(input.normal);

    float3 finalRgb = Result.BaseColor + Result.Emissive + GetCommonMaterialEmissive();

    return float4(finalRgb, saturate(Result.Opacity));
}
