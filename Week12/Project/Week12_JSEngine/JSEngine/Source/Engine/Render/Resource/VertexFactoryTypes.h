#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/String.h"
#include "Particle/ParticleBeamTypes.h"
#include "Particle/ParticleRibbonTypes.h"
#include "Render/Resource/ShaderPaths.h"
#include "Render/Resource/ShaderTypes.h"
#include "Render/Resource/VertexTypes.h"

#include <cstddef>

struct ID3D11Buffer;
struct ID3D11DeviceContext;
struct FBoneMatrixConstants;
struct FRenderResources;
class FConstantBuffer;

// Mesh Vertex 데이터를 어떤 방식으로 해석할지 나타내는 타입입니다.
// Material이 Static/Skeletal 여부를 알지 않도록 RenderCommand가 이 값을 들고 갑니다.
enum class EVertexFactoryType : uint8
{
    StaticMesh,
    SkeletalMesh,
    ProceduralMesh,
    Primitive,
    Billboard,
    SubUV,
    Line,
    Text,
    Gizmo,
    Decal,
    SpriteParticle,
    // 본 enum entry 추가는 silent bug §7-1 회피의 필수 절반.
    // 나머지 절반은 Registry::Get switch에 명시 case 추가 (아래 참조).
    // Layout/Desc 본문은 Cycle 11+ 각 emitter cycle에서 채움.
    MeshParticle,
    RibbonParticle,
    BeamParticle,
};

// VertexFactory별 Shader Entry 정책입니다.
// 같은 Material PS라도 StaticMeshVS / SkeletalMeshVS처럼 VS만 갈아끼울 수 있게 분리합니다.
struct FVertexFactoryDesc
{
    FString VertexShaderPath;
    FString DepthPassVSPath;
    FString ShadowPassVSPath;
    FString SelectionPassVSPath;
    FString BasePassVSEntry;
    FString DepthPassVSEntry;
    FString ShadowPassVSEntry;
    FString SelectionPassVSEntry;
    FVertexLayoutDesc VertexLayout;
    FVertexLayoutDesc PositionOnlyLayout;
    FVertexLayoutDesc SelectionLayout;
};

class FVertexFactoryRegistry
{
public:
    // 초기 단계에서는 과한 상속 구조 대신 Enum -> Desc 매핑으로 관리합니다.
    // GPU Skinning처럼 리소스 바인딩 규칙이 복잡해지면 객체 모델로 확장하면 됩니다.
    static const FVertexFactoryDesc& Get(EVertexFactoryType Type)
    {
        static const FVertexLayoutDesc NormalVertexLayout = {
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<uint32>(offsetof(FNormalVertex, Position)) },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FNormalVertex, Color)) },
                { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<uint32>(offsetof(FNormalVertex, Normal)) },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<uint32>(offsetof(FNormalVertex, UVs)) },
                { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FNormalVertex, Tangent)) },
            },
            sizeof(FNormalVertex)
        };
        static const FVertexLayoutDesc SkeletalVertexLayout = {
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<uint32>(offsetof(FSkeletalMeshVertex, Position)) },
                { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<uint32>(offsetof(FSkeletalMeshVertex, Normal)) },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<uint32>(offsetof(FSkeletalMeshVertex, UVs)) },
                { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FSkeletalMeshVertex, Tangent)) },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FSkeletalMeshVertex, Color)) },
                { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, static_cast<uint32>(offsetof(FSkeletalMeshVertex, BoneIndices)) },
                { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FSkeletalMeshVertex, BoneWeights)) },
            },
            sizeof(FSkeletalMeshVertex)
        };
        static const FVertexLayoutDesc PrimitiveVertexLayout = {
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<uint32>(offsetof(FVertex, Position)) },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FVertex, Color)) },
            },
            sizeof(FVertex)
        };
        static const FVertexLayoutDesc TextureVertexLayout = {
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<uint32>(offsetof(FTextureVertex, Position)) },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<uint32>(offsetof(FTextureVertex, TexCoord)) },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FTextureVertex, Color)) },
            },
            sizeof(FTextureVertex)
        };
        static const FVertexLayoutDesc TexturePositionUVLayout = {
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<uint32>(offsetof(FTextureVertex, Position)) },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<uint32>(offsetof(FTextureVertex, TexCoord)) },
            },
            sizeof(FTextureVertex)
        };
        static const FVertexLayoutDesc PositionOnlyLayout = {
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 },
            },
            0
        };
        // Slot 0: per-vertex mesh (FNormalVertex)
        // Slot 1: per-instance (FMeshParticleInstanceData, 56B)
        // Cycle 11 옵션 B: INSTANCE_ROTATION = R32G32B32_FLOAT (FVector Euler 3축).
        static const FVertexLayoutDesc MeshParticleLayout = {
            {
                { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FNormalVertex, Position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FNormalVertex, Color)),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FNormalVertex, Normal)),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<uint32>(offsetof(FNormalVertex, UVs)),      D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TANGENT",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FNormalVertex, Tangent)),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "INSTANCE_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    1, static_cast<uint32>(offsetof(FMeshParticleInstanceData, InstancePosition)), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "INSTANCE_ROTATION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    1, static_cast<uint32>(offsetof(FMeshParticleInstanceData, InstanceRotation)), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "INSTANCE_SCALE",    0, DXGI_FORMAT_R32G32B32_FLOAT,    1, static_cast<uint32>(offsetof(FMeshParticleInstanceData, InstanceScale)),    D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "INSTANCE_COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<uint32>(offsetof(FMeshParticleInstanceData, InstanceColor)),    D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            },
            sizeof(FMeshParticleInstanceData)
        };
        // Ribbon Particle (Cycle 12). Slot 0: per-vertex (FRibbonParticleVertex, 48B), no instancing.
        // topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP (RenderRibbonEmitter 에서 IASetPrimitiveTopology).
        static const FVertexLayoutDesc RibbonParticleLayout = {
            {
                { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FRibbonParticleVertex, Position)),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FRibbonParticleVertex, Tangent)),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FRibbonParticleVertex, Color)),     D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD",  0, DXGI_FORMAT_R32_FLOAT,          0, static_cast<uint32>(offsetof(FRibbonParticleVertex, TexCoordU)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD",  1, DXGI_FORMAT_R32_FLOAT,          0, static_cast<uint32>(offsetof(FRibbonParticleVertex, Size)),      D3D11_INPUT_PER_VERTEX_DATA, 0 },
            },
            sizeof(FRibbonParticleVertex)
        };
        // Beam Particle (Cycle 13a). Slot 0: per-vertex (FBeamParticleVertex, 48B), no instancing.
        // Ribbon 와 동일 layout (48B, 5 입력) — semantic 은 Beam 전용 struct 로 분리 (진단 §12 옵션 Y).
        // topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP (RenderBeamEmitter 에서 IASetPrimitiveTopology).
        static const FVertexLayoutDesc BeamParticleLayout = {
            {
                { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FBeamParticleVertex, Position)),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FBeamParticleVertex, Tangent)),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FBeamParticleVertex, Color)),     D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD",  0, DXGI_FORMAT_R32_FLOAT,          0, static_cast<uint32>(offsetof(FBeamParticleVertex, TexCoordU)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD",  1, DXGI_FORMAT_R32_FLOAT,          0, static_cast<uint32>(offsetof(FBeamParticleVertex, Size)),      D3D11_INPUT_PER_VERTEX_DATA, 0 },
            },
            sizeof(FBeamParticleVertex)
        };
        // Slot 0: per-vertex quad (FSpriteParticleVertex, 20B)
        // Slot 1: per-instance (FSpriteParticleInstanceData, 44B)
        static const FVertexLayoutDesc SpriteParticleLayout = {
            {
                { "POSITION",          0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FSpriteParticleVertex, Position)),     D3D11_INPUT_PER_VERTEX_DATA,   0 },
                { "TEXCOORD",          0, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<uint32>(offsetof(FSpriteParticleVertex, TexCoord)),     D3D11_INPUT_PER_VERTEX_DATA,   0 },
                { "INSTANCE_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    1, static_cast<uint32>(offsetof(FSpriteParticleInstanceData, Position)),   D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "INSTANCE_SIZE",     0, DXGI_FORMAT_R32G32_FLOAT,       1, static_cast<uint32>(offsetof(FSpriteParticleInstanceData, Size)),       D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "INSTANCE_COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<uint32>(offsetof(FSpriteParticleInstanceData, Color)),      D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "INSTANCE_ROTATION", 0, DXGI_FORMAT_R32_FLOAT,          1, static_cast<uint32>(offsetof(FSpriteParticleInstanceData, Rotation)),   D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "INSTANCE_SUBUV_INDEX", 0, DXGI_FORMAT_R32_UINT,        1, static_cast<uint32>(offsetof(FSpriteParticleInstanceData, SubUVIndex)), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            },
            sizeof(FSpriteParticleInstanceData)
        };

        static const FVertexFactoryDesc StaticMeshDesc = {
            FShaderPaths::MaterialUberLit,
            FShaderPaths::DepthPrepass,
            FShaderPaths::Shadow,
            FShaderPaths::EditorSelectionMask,
            "mainVS",
            "DepthPrepassVS",
            "ShadowVS",
            "VSStaticMesh",
            NormalVertexLayout,
            PositionOnlyLayout,
            NormalVertexLayout
        };
        static const FVertexFactoryDesc SkeletalMeshDesc = {
            FShaderPaths::MaterialUberLit,
            FShaderPaths::DepthPrepass,
            FShaderPaths::Shadow,
            FShaderPaths::EditorSelectionMask,
            "SkeletalMeshVS",
            "SkeletalDepthPrepassVS",
            "SkeletalShadowVS",
            "VSSkeletalMesh",
            SkeletalVertexLayout,
            SkeletalVertexLayout,
            SkeletalVertexLayout
        };
        static const FVertexFactoryDesc DecalDesc = {
            FShaderPaths::MaterialDecal,
            FShaderPaths::DepthPrepass,
            FShaderPaths::Shadow,
            FShaderPaths::EditorSelectionMask,
            "mainVS",
            "DepthPrepassVS",
            "ShadowVS",
            "VSStaticMesh",
            NormalVertexLayout,
            PositionOnlyLayout,
            NormalVertexLayout
        };
        static const FVertexFactoryDesc GizmoDesc = {
            FShaderPaths::EditorGizmo,
            FShaderPaths::EditorGizmo,
            FShaderPaths::EditorGizmo,
            FShaderPaths::EditorGizmo,
            "VS",
            "VS",
            "VS",
            "VS",
            PrimitiveVertexLayout,
            PrimitiveVertexLayout,
            PrimitiveVertexLayout
        };
        static const FVertexFactoryDesc PrimitiveDesc = {
            FShaderPaths::EditorPrimitive,
            FShaderPaths::DepthPrepass,
            FShaderPaths::Shadow,
            FShaderPaths::EditorSelectionMask,
            "VS",
            "DepthPrepassVS",
            "ShadowVS",
            "VSPrimitive",
            PrimitiveVertexLayout,
            PositionOnlyLayout,
            PrimitiveVertexLayout
        };
        static const FVertexFactoryDesc TexturedQuadDesc = {
            FShaderPaths::UISubUV,
            FShaderPaths::DepthPrepass,
            FShaderPaths::Shadow,
            FShaderPaths::EditorSelectionMask,
            "VS",
            "DepthPrepassVS",
            "ShadowVS",
            "VSBillboard",
            TextureVertexLayout,
            PositionOnlyLayout,
            PrimitiveVertexLayout
        };
        static const FVertexFactoryDesc TextDesc = {
            FShaderPaths::UIFont,
            FShaderPaths::DepthPrepass,
            FShaderPaths::Shadow,
            FShaderPaths::EditorSelectionMask,
            "VS",
            "DepthPrepassVS",
            "ShadowVS",
            "VSBillboard",
            TexturePositionUVLayout,
            PositionOnlyLayout,
            PrimitiveVertexLayout
        };
        // MeshParticle은 DepthPrepass/Shadow/Selection 패스에 들어가지 않습니다 (Cycle 11).
        // 해당 entry들은 후속 cycle에서 필요해지면 채웁니다.
        static const FVertexFactoryDesc MeshParticleDesc = {
            FShaderPaths::ParticleMesh,
            FShaderPaths::ParticleMesh,
            FShaderPaths::ParticleMesh,
            FShaderPaths::ParticleMesh,
            "MeshParticleVS",
            "MeshParticleVS",
            "MeshParticleVS",
            "MeshParticleVS",
            MeshParticleLayout,
            MeshParticleLayout,
            MeshParticleLayout
        };
        // RibbonParticle (Cycle 12). DepthPrepass/Shadow/Selection 패스에 들어가지 않습니다 — Particle pass 전용.
        static const FVertexFactoryDesc RibbonParticleDesc = {
            FShaderPaths::ParticleRibbon,
            FShaderPaths::ParticleRibbon,
            FShaderPaths::ParticleRibbon,
            FShaderPaths::ParticleRibbon,
            "RibbonParticleVS",
            "RibbonParticleVS",
            "RibbonParticleVS",
            "RibbonParticleVS",
            RibbonParticleLayout,
            RibbonParticleLayout,
            RibbonParticleLayout
        };
        // BeamParticle (Cycle 13a). DepthPrepass/Shadow/Selection 패스에 들어가지 않습니다 — Particle pass 전용.
        // Ribbon 와 동일 구조 — shader path / entry point 만 분리.
        static const FVertexFactoryDesc BeamParticleDesc = {
            FShaderPaths::ParticleBeam,
            FShaderPaths::ParticleBeam,
            FShaderPaths::ParticleBeam,
            FShaderPaths::ParticleBeam,
            "BeamParticleVS",
            "BeamParticleVS",
            "BeamParticleVS",
            "BeamParticleVS",
            BeamParticleLayout,
            BeamParticleLayout,
            BeamParticleLayout
        };
        // SpriteParticle은 DepthPrepass/Shadow/Selection 패스에 들어가지 않습니다.
        // 해당 entry들은 cascade 포팅의 후속 사이클에서 필요해지면 채웁니다.
        static const FVertexFactoryDesc SpriteParticleDesc = {
            FShaderPaths::ParticleSprite,
            FShaderPaths::ParticleSprite,
            FShaderPaths::ParticleSprite,
            FShaderPaths::ParticleSprite,
            "SpriteParticleVS",
            "SpriteParticleVS",
            "SpriteParticleVS",
            "SpriteParticleVS",
            SpriteParticleLayout,
            SpriteParticleLayout,
            SpriteParticleLayout
        };

        // Mesh/Ribbon/Beam Particle는 본 cycle (10a)에서 enum + case만 wire-up.
        // Layout/Desc 본문은 Cycle 11+ 각 emitter cycle에서 채움.
        // 명시 case가 없으면 default(StaticMeshDesc) fallback → silent bug §7-1 직접 충돌.
        static const FVertexFactoryDesc EmptyParticleDesc = {};

        switch (Type)
        {
        case EVertexFactoryType::SkeletalMesh:
            return SkeletalMeshDesc;
        case EVertexFactoryType::Decal:
            return DecalDesc;
        case EVertexFactoryType::Gizmo:
            return GizmoDesc;
        case EVertexFactoryType::Primitive:
        case EVertexFactoryType::Line:
            return PrimitiveDesc;
        case EVertexFactoryType::Billboard:
        case EVertexFactoryType::SubUV:
            return TexturedQuadDesc;
        case EVertexFactoryType::Text:
            return TextDesc;
        // SpriteParticle은 default fallback(StaticMesh)으로 떨어지면 silent bug가 됩니다.
        // 신규 EVertexFactoryType 추가 시 여기에 명시 case 추가 필수.
        case EVertexFactoryType::SpriteParticle:
            return SpriteParticleDesc;
        // Cycle 11: MeshParticle 본문 채움. Cycle 12: RibbonParticle 본문 채움. Cycle 13a: BeamParticle 본문 채움.
        case EVertexFactoryType::MeshParticle:
            return MeshParticleDesc;
        case EVertexFactoryType::RibbonParticle:
            return RibbonParticleDesc;
        case EVertexFactoryType::BeamParticle:
            return BeamParticleDesc;
        case EVertexFactoryType::StaticMesh:
        case EVertexFactoryType::ProceduralMesh:
        default:
            return StaticMeshDesc;
        }
    }
};

void BindVertexFactoryResources(
    ID3D11DeviceContext* Context,
    EVertexFactoryType Type,
    const FBoneMatrixConstants* BoneMatrixConstants,
    FRenderResources* RenderResources,
    FConstantBuffer* BoneMatrixConstantBuffer = nullptr);
