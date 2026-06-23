// Mesh Particle Shader (Cycle 11, 옵션 B)
// VS: per-vertex (FNormalVertex, slot 0) + per-instance (FMeshParticleInstanceData, slot 1) → World 변환 후 MVP.
// PS: unlit albedo × InstanceColor — lit/lighting은 후속 cycle.
// PerObject CB의 Model은 Identity로 들어옴 (Builder에서 PerObjectConstants 셋팅 안 함 — instance VB가 World 합성 담당).

#include "../Common/Common.hlsli"

Texture2D    MeshAlbedo : register(t0);
SamplerState MeshSampler : register(s0);

struct VSInput
{
    // Slot 0: per-vertex (FNormalVertex)
    float3 Position : POSITION;
    float4 VertexColor : COLOR;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD;
    float4 Tangent  : TANGENT;

    // Slot 1: per-instance (FMeshParticleInstanceData)
    float3 InstancePosition : INSTANCE_POSITION;
    float3 InstanceRotation : INSTANCE_ROTATION;   // Euler radians (옵션 B 3축)
    float3 InstanceScale    : INSTANCE_SCALE;
    float4 InstanceColor    : INSTANCE_COLOR;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR;
};

// Function : Build 3x3 rotation matrix from Euler (ZYX order: yaw → pitch → roll)
// input : Euler (x = roll around X, y = pitch around Y, z = yaw around Z) in radians
// output : 3x3 row-major rotation matrix applied as (point * matrix)
float3x3 EulerZYXToMatrix(float3 Euler)
{
    float sx = sin(Euler.x); float cx = cos(Euler.x);
    float sy = sin(Euler.y); float cy = cos(Euler.y);
    float sz = sin(Euler.z); float cz = cos(Euler.z);

    // R = Rz * Ry * Rx (row-major, point on left)
    float3x3 Rx = float3x3(
        1, 0,   0,
        0, cx, -sx,
        0, sx,  cx);
    float3x3 Ry = float3x3(
        cy, 0, sy,
        0,  1, 0,
       -sy, 0, cy);
    float3x3 Rz = float3x3(
         cz, -sz, 0,
         sz,  cz, 0,
         0,   0,  1);
    return mul(mul(Rx, Ry), Rz);
}

PSInput MeshParticleVS(VSInput input)
{
    PSInput output;

    // World = Translation(InstancePosition) * RotationZYX(InstanceRotation) * Scale(InstanceScale)
    float3 ScaledLocal = input.Position * input.InstanceScale;
    float3x3 RotMat = EulerZYXToMatrix(input.InstanceRotation);
    float3 RotatedLocal = mul(ScaledLocal, RotMat);
    float3 WorldPos = RotatedLocal + input.InstancePosition;

    output.Position = mul(mul(float4(WorldPos, 1.0f), View), Projection);
    output.TexCoord = input.TexCoord;
    output.Color    = input.InstanceColor * input.VertexColor;
    return output;
}

float4 MeshParticlePS(PSInput input) : SV_TARGET
{
    float4 Sample = MeshAlbedo.Sample(MeshSampler, input.TexCoord);
    float4 Final = Sample * input.Color;
    // Component/Emitter opacity multiplier — Builder 에서 Material BlendType 이 AlphaBlend 일 때만 1.0 외 값 주입.
    Final.a *= PrimitiveColor.w;
    return Final;
}
