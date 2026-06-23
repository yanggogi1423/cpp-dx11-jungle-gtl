#include "../Common/Common.hlsli"

Texture2D    SpriteAtlas  : register(t0);
SamplerState SpriteSampler : register(s0);

// SubUV Atlas grid 메타데이터. b3(UberConstants) 등과 충돌 회피를 위해 미사용 슬롯 b8 사용.
cbuffer SpriteParticleBuffer : register(b8)
{
    uint   SubUVColumns;
    uint   SubUVRows;
    float2 SubUVAtlasPadding;
};

struct VSInput
{
    // Slot 0: per-vertex
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD;

    // Slot 1: per-instance
    float3 InstancePosition   : INSTANCE_POSITION;
    float2 InstanceSize       : INSTANCE_SIZE;
    float4 InstanceColor      : INSTANCE_COLOR;
    float  InstanceRotation   : INSTANCE_ROTATION;
    uint   InstanceSubUVIndex : INSTANCE_SUBUV_INDEX;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : COLOR;
};

PSInput SpriteParticleVS(VSInput input)
{
    PSInput output;

    // Camera-facing billboard:
    // quad의 Position(X,Y는 [-0.5,0.5], Z=0)을 view-space에서 size 적용 후 rotation, 그리고 instance position 더하기
    float2 LocalXY = input.Position.xy * input.InstanceSize;

    float s = sin(input.InstanceRotation);
    float c = cos(input.InstanceRotation);
    float2 RotatedXY = float2(LocalXY.x * c - LocalXY.y * s,
                              LocalXY.x * s + LocalXY.y * c);

    // BillboardComponent::MakeBillboardWorldMatrix와 동일한 spherical billboard
    // View columns: col0=Forward, col1=Right, col2=Up (Unreal-style engine basis)
    float3 CameraForward = float3(View._11, View._21, View._31);
    float3 CameraRight = -float3(View._12, View._22, View._32); // BillboardComponent 와 동일하게 부호 반전
    float3 CameraUp = float3(View._13, View._23, View._33);

    
    
    // Forward 퇴화 처리
    if (dot(CameraForward, CameraForward) < 1e-8f)
    {
        CameraForward = float3(-1.0f, 0.0f, 0.0f);
    }
    else
    {
        CameraForward = normalize(CameraForward);
    }

    // Right/Up 퇴화 처리 (cross product로 재구성)
    if (dot(CameraRight, CameraRight) < 1e-8f || dot(CameraUp, CameraUp) < 1e-8f)
    {
        float3 FallbackUp = float3(0.0f, 0.0f, 1.0f); // FVector::UpVector (Z-up)
        if (abs(dot(CameraForward, FallbackUp)) > 0.99f)
        {
            FallbackUp = float3(0.0f, 1.0f, 0.0f);    // FVector::RightVector
        }
        CameraRight = normalize(cross(FallbackUp, CameraForward));
        CameraUp    = normalize(cross(CameraForward, CameraRight));
    }
    else
    {
        CameraRight = normalize(CameraRight);
        CameraUp    = normalize(CameraUp);
    }

    float3 WorldPos = input.InstancePosition
                    + CameraRight * RotatedXY.x
                    + CameraUp    * RotatedXY.y;

    output.Position = mul(mul(float4(WorldPos, 1.0f), View), Projection);

    // SubUV: atlas grid에서 InstanceSubUVIndex가 가리키는 셀의 UV 영역으로 매핑
    uint Columns = max(SubUVColumns, 1u);
    uint Rows    = max(SubUVRows,    1u);
    uint Col = input.InstanceSubUVIndex % Columns;
    uint Row = (input.InstanceSubUVIndex / Columns) % Rows;
    float2 CellSize = float2(1.0f / (float)Columns, 1.0f / (float)Rows);
    float2 CellOffset = float2((float)Col * CellSize.x, (float)Row * CellSize.y);
    output.TexCoord = CellOffset + input.TexCoord * CellSize;

    output.Color = input.InstanceColor;
    return output;
}

float4 SpriteParticlePS(PSInput input) : SV_TARGET
{
    float4 Sample = SpriteAtlas.Sample(SpriteSampler, input.TexCoord);
    float4 Final = Sample * input.Color;
    // Component/Emitter opacity multiplier — Builder 에서 AlphaBlend 일 때만 1.0 외 값 주입.
    Final.a *= PrimitiveColor.w;
    if (Final.a < 0.01f)
    {
        discard;
    }
    return Final;
}
