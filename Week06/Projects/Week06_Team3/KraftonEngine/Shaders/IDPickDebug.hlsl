#include "Common/Functions.hlsl"

Texture2D<uint> g_IdTex : register(t0);

struct VS_Out
{
    float4 Position : SV_POSITION;
};

VS_Out VS(uint VertexID : SV_VertexID)
{
    VS_Out Out;

    // Full-screen triangle
    float2 Pos = float2((VertexID == 2) ? 3.0f : -1.0f, (VertexID == 1) ? -3.0f : 1.0f);
    Out.Position = float4(Pos, 0.0f, 1.0f);
    return Out;
}

float4 PS(VS_Out In) : SV_TARGET
{
    const int2 PixelCoord = int2(In.Position.xy);
    const uint Picked = g_IdTex.Load(int3(PixelCoord, 0));

    if (Picked == 0u)
    {
        return float4(0.03f, 0.03f, 0.03f, 1.0f);
    }

    // Hash id -> stable debug color
    const uint h = Picked * 1664525u + 1013904223u;
    const float r = ((h >> 0) & 255u) / 255.0f;
    const float g = ((h >> 8) & 255u) / 255.0f;
    const float b = ((h >> 16) & 255u) / 255.0f;
    return float4(0.25f + 0.75f * r, 0.25f + 0.75f * g, 0.25f + 0.75f * b, 1.0f);
}
