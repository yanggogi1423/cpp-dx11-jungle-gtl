#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"

Texture2D<uint> IdTexture : register(t0);

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    uint2 pixel = uint2(input.position.xy);
    uint id = IdTexture.Load(int3(pixel, 0));
    if (id == 0)
    {
        return float4(0.02f, 0.02f, 0.025f, 1.0f);
    }

    uint hash = id * 1664525u + 1013904223u;
    float r = ((hash >> 0) & 0xffu) / 255.0f;
    float g = ((hash >> 8) & 0xffu) / 255.0f;
    float b = ((hash >> 16) & 0xffu) / 255.0f;
    return float4(0.25f + r * 0.75f, 0.25f + g * 0.75f, 0.25f + b * 0.75f, 1.0f);
}
