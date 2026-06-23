#include "Common/ConstantBuffers.hlsl"

Texture2D<float> DepthTex : register(t0);

struct PS_Input
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

PS_Input VS(uint vertexID : SV_VertexID)
{
    PS_Input output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float ConvertFromDeviceZ(float DeviceZ)
{
    if (abs(Projection[2][3]) < 1e-6f)
    {
        float proj33 = Projection[2][2];
        float safeProj33 = (abs(proj33) > 1e-6f) ? proj33 : 1e-6f;
        return (DeviceZ - Projection[3][2]) / safeProj33;
    }

    float denom = DeviceZ * InvDeviceZToWorldZTransform2 - InvDeviceZToWorldZTransform3;
    return 1.0f / max(denom, 1e-6f);
}

float4 PS(PS_Input input) : SV_Target
{
    const int2 coord = int2(input.position.xy);
    float deviceZ = DepthTex.Load(int3(coord, 0));

    // Background / untouched depth stays black.
    if (deviceZ >= 0.999999f)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float viewDepth = ConvertFromDeviceZ(deviceZ);
    float nearDepth = max(NearPlane, 1e-6f);
    float farDepth = max(FarPlane, nearDepth + 1e-6f);
    float safeViewDepth = clamp(viewDepth, nearDepth, farDepth);
    float depth01 = saturate(log2(1.0f + safeViewDepth) / log2(1.0f + farDepth));
    float contrastDepth = pow(depth01, 1.35f) * 0.85f;

    // UE-style scene depth: near is dark, far gets brighter, background stays black.
    float gray = saturate(contrastDepth);
    return float4(gray, gray, gray, 1.0);
}
