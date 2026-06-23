// Generated from Content/Material/Auto/Car_Material_9537.uasset
// Domain: Surface
// Shading: Phong, receive lighting true, cast shadow true, opacity 1.0

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Common/ForwardLighting.hlsli"
#include "Common/NormalMapping.hlsli"

Texture2D Tex_DiffuseTexture : register(t0);

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

float3 EvaluateBaseColor(MaterialSurfaceVSOutput input)
{
    float4 sampledBase = Tex_DiffuseTexture.Sample(LinearWrapSampler, input.texcoord);
    float3 baseColor = sampledBase.rgb * float3(1.000000f, 1.000000f, 1.000000f);
    if (sampledBase.a < 0.001f)
    {
        baseColor = float3(1.000000f, 1.000000f, 1.000000f);
    }
    return pow(saturate(baseColor), float3(2.2f, 2.2f, 2.2f));
}

float4 PS(MaterialSurfaceVSOutput input) : SV_TARGET
{
    float3 baseColor = EvaluateBaseColor(input);
    float3 N = normalize(input.normal);

    float3 V = normalize(CameraWorldPos - input.worldPos);
    float3 diffuse = AccumulateDiffuse(input.worldPos, N, input.position);
    float3 specular = AccumulateSpecular(input.worldPos, N, V, 32.0f, input.position);
    float3 finalRgb = baseColor * diffuse + specular;
    return float4(finalRgb, 1.0f);
}
