// Ribbon Particle Shader (Cycle 12, 결정 6/7/8/9 lock-in).
// VS: per-vertex (FRibbonParticleVertex, slot 0) — instance VB 없음 (Mesh 와의 핵심 차이).
//     Position 은 CPU 측에서 이미 world space 로 계산되어 있어 World 행렬 적용 불요.
// PS: unlit Color (vertex color × material albedo) — lit/lighting 통합은 후속 cycle.
// topology = TRIANGLESTRIP. multi-trail 사이는 degenerate triangle (vertex 1개 복제) 로 연결 끊김.

#include "../Common/Common.hlsli"

Texture2D    RibbonAlbedo : register(t0);
SamplerState RibbonSampler : register(s0);

struct VSInput
{
    // Slot 0: per-vertex (FRibbonParticleVertex)
    float3 Position  : POSITION;
    float3 Tangent   : TANGENT;
    float4 Color     : COLOR;
    float  TexCoordU : TEXCOORD0;
    float  Size      : TEXCOORD1;

    // SV_VertexID 로 strip 양쪽 vertex 식별 (짝수 = +Perp, 홀수 = -Perp).
    // BuildVertexBuffer 가 V0/V1 순서로 push 하므로 짝수 = V (1.0), 홀수 = V (0.0).
    uint VertexID : SV_VertexID;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR;
};

PSInput RibbonParticleVS(VSInput input)
{
    PSInput output;

    // Position 이 이미 world space — Model 행렬 무시 (Builder가 Identity 로 PerObject CB 세팅).
    output.Position = mul(mul(float4(input.Position, 1.0f), View), Projection);

    // V coordinate: 짝수 VertexID = strip 한쪽 (V=1), 홀수 = 반대쪽 (V=0).
    const float V = (input.VertexID & 1u) ? 0.0f : 1.0f;
    output.TexCoord = float2(input.TexCoordU, V);
    output.Color = input.Color;
    return output;
}

float4 RibbonParticlePS(PSInput input) : SV_TARGET
{
    float4 Sample = RibbonAlbedo.Sample(RibbonSampler, input.TexCoord);
    float4 Final = Sample * input.Color;
    // Component/Emitter opacity multiplier — Builder 에서 AlphaBlend 일 때만 1.0 외 값 주입.
    Final.a *= PrimitiveColor.w;
    return Final;
}
