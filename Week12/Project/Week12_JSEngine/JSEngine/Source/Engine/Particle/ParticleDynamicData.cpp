#include "Particle/ParticleDynamicData.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "Component/SceneComponent.h"
#include "Math/Matrix.h"
#include "Math/Utils.h"
#include "Math/Vector.h"
#include "Particle/ParticleBeamEmitterInstance.h"
#include "Particle/ParticleBeamTypes.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleMeshEmitterInstance.h"
#include "Particle/ParticleMeshTypes.h"
#include "Particle/ParticleModuleBeamNoise.h"
#include "Particle/ParticleModuleBeamSource.h"
#include "Particle/ParticleModuleBeamTarget.h"
#include "Particle/ParticleRendererProperties.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemComponent.h"
#include "Particle/ParticleTypes.h"
#include "Render/Resource/InstanceBuffer.h"
#include "Render/Resource/VertexTypes.h"

// ──────────────────────────────────────────────────────────────────────────────
// Phase 5 이관: Mesh/Beam build helpers (Mesh/Beam EmitterInstance.cpp 의 anonymous namespace 본문).
// 본 위치로 옮겨 BuildInstanceData/BuildVertexBuffer 자체를 instance 에서 제거할 수 있게 함.
// ──────────────────────────────────────────────────────────────────────────────

namespace
{
    // ===== Mesh helpers =====
    constexpr float MeshAlignSmallNumber = 1.0e-6f;
    constexpr float MeshAlignParallelDot = 0.99f;

    FMatrix MakeShaderEulerRotation(const FVector& EulerRad)
    {
        const float sx = std::sin(EulerRad.X), cx = std::cos(EulerRad.X);
        const float sy = std::sin(EulerRad.Y), cy = std::cos(EulerRad.Y);
        const float sz = std::sin(EulerRad.Z), cz = std::cos(EulerRad.Z);
        return FMatrix(
             cy * cz,                 -cy * sz,                  sy,      0.0f,
             sx * sy * cz + cx * sz,  -sx * sy * sz + cx * cz,  -sx * cy, 0.0f,
            -cx * sy * cz + sx * sz,   cx * sy * sz + sx * cz,   cx * cy, 0.0f,
             0.0f,                     0.0f,                     0.0f,    1.0f);
    }

    FVector ExtractShaderEuler(const FMatrix& M)
    {
        const float SinY = std::clamp(M.M[0][2], -1.0f, 1.0f);
        const float Y = std::asin(SinY);
        const float Cy = std::cos(Y);
        FVector Result;
        if (std::fabs(Cy) > MeshAlignSmallNumber)
        {
            Result.X = std::atan2(-M.M[1][2], M.M[2][2]);
            Result.Y = Y;
            Result.Z = std::atan2(-M.M[0][1], M.M[0][0]);
        }
        else
        {
            Result.X = std::atan2(M.M[1][0], M.M[1][1]);
            Result.Y = Y;
            Result.Z = 0.0f;
        }
        return Result;
    }

    FMatrix MakeAlignmentMatrix(const FVector& Forward, const FVector& UpHint)
    {
        const FVector X = Forward.GetSafeNormal();
        if (X.IsNearlyZero())
        {
            return FMatrix::Identity;
        }
        FVector Up = UpHint.GetSafeNormal();
        if (Up.IsNearlyZero() || std::fabs(X.DotProduct(Up)) > MeshAlignParallelDot)
        {
            Up = (std::fabs(X.X) < MeshAlignParallelDot)
                ? FVector(1.0f, 0.0f, 0.0f)
                : FVector(0.0f, 1.0f, 0.0f);
        }
        const FVector Y = FVector::CrossProduct(Up, X).GetSafeNormal();
        const FVector Z = FVector::CrossProduct(X, Y).GetSafeNormal();
        return FMatrix(
            X.X, X.Y, X.Z, 0.0f,
            Y.X, Y.Y, Y.Z, 0.0f,
            Z.X, Z.Y, Z.Z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
    }

    // ===== Beam helpers =====
    constexpr float BeamSmallNumber = 1.0e-6f;
    constexpr int32 BeamInterpolationPointsMax = 64;
    constexpr float BeamAxisParallelDot = 0.99f;

    FVector ComputeBeamPerpendicular(const FVector& Tangent)
    {
        const FVector Up(0.0f, 0.0f, 1.0f);
        FVector Perp = FVector::CrossProduct(Tangent, Up);
        if (Perp.SizeSquared() < BeamSmallNumber)
        {
            Perp = FVector::CrossProduct(Tangent, FVector(1.0f, 0.0f, 0.0f));
        }
        Perp.Normalize();
        return Perp;
    }

    void ComputeBeamLocalAxes(const FVector& Tangent, FVector& OutPerp1, FVector& OutPerp2)
    {
        const FVector WorldUp(0.0f, 0.0f, 1.0f);
        const FVector WorldRight(1.0f, 0.0f, 0.0f);
        const FVector RefAxis = (MathUtil::Abs(Tangent.DotProduct(WorldUp)) > BeamAxisParallelDot)
            ? WorldRight
            : WorldUp;
        OutPerp1 = Tangent.CrossProduct(RefAxis).GetSafeNormal();
        OutPerp2 = Tangent.CrossProduct(OutPerp1).GetSafeNormal();
    }

    // Hermite cubic 보간: P(t) = h00*P0 + h10*T0 + h01*P1 + h11*T1
    //   h00 = 2t³-3t²+1, h10 = t³-2t²+t, h01 = -2t³+3t², h11 = t³-t²
    FVector EvaluateHermite(const FVector& P0, const FVector& T0, const FVector& P1, const FVector& T1, float t)
    {
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        const float h10 = t3 - 2.0f * t2 + t;
        const float h01 = -2.0f * t3 + 3.0f * t2;
        const float h11 = t3 - t2;
        return P0 * h00 + T0 * h10 + P1 * h01 + T1 * h11;
    }

    // Hermite derivative: dP/dt = h00'*P0 + h10'*T0 + h01'*P1 + h11'*T1
    //   h00' = 6t²-6t, h10' = 3t²-4t+1, h01' = -6t²+6t, h11' = 3t²-2t
    FVector EvaluateHermiteDerivative(const FVector& P0, const FVector& T0, const FVector& P1, const FVector& T1, float t)
    {
        const float t2 = t * t;
        const float dh00 = 6.0f * t2 - 6.0f * t;
        const float dh10 = 3.0f * t2 - 4.0f * t + 1.0f;
        const float dh01 = -6.0f * t2 + 6.0f * t;
        const float dh11 = 3.0f * t2 - 2.0f * t;
        return P0 * dh00 + T0 * dh10 + P1 * dh01 + T1 * dh11;
    }

    template <typename T>
    T* FindFirstModule(UParticleLODLevel* LOD)
    {
        if (!LOD)
        {
            return nullptr;
        }
        for (UParticleModule* Module : LOD->GetModules())
        {
            T* Casted = Cast<T>(Module);
            if (Casted)
            {
                return Casted;
            }
        }
        return nullptr;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Sprite DynamicData (Phase 2)
// ──────────────────────────────────────────────────────────────────────────────

// Function : Build Sprite instance buffer from instance simulation state
// input : Instance — source emitter instance (read-only access to FBaseParticle stream)
// output : SpriteInstanceDataBuffer filled with one entry per active particle
//
// Migrated from FParticleEmitterInstance::BuildInstanceData() (Sprite path).
// Read sequence per particle: GetParticle(active_idx) → Location/Size/Color/Rotation/SubUVIndex.
void FDynamicSpriteEmitterData::BuildFromInstance(const FParticleEmitterInstance& Instance)
{
    SpriteInstanceDataBuffer.clear();
    const int32 ActiveCount = Instance.GetActiveParticleCount();
    if (ActiveCount <= 0)
    {
        return;
    }

    SpriteInstanceDataBuffer.reserve(ActiveCount);
    for (int32 i = 0; i < ActiveCount; ++i)
    {
        const FBaseParticle* Particle = Instance.GetParticle(i);
        if (!Particle)
        {
            continue;
        }
        FSpriteParticleInstanceData Data;
        Data.Position   = Instance.ResolveParticleLocationForRender(Particle->Location);
        Data.Size       = FVector2(Particle->Size.X, Particle->Size.Y);
        Data.Color      = Particle->Color;
        Data.Rotation   = Particle->Rotation;
        Data.SubUVIndex = Particle->SubUVIndex;
        SpriteInstanceDataBuffer.push_back(Data);
    }
}

// Function : Upload CPU sprite instance data to GPU slot-1 instance buffer
// input : Device, DeviceContext — D3D state. InOutBuffer — RenderPass-owned FInstanceBuffer (D12).
// output : InOutBuffer holds latest sprite instance data, ready for DrawIndexedInstanced.
void FDynamicSpriteEmitterData::FillVertexBuffer(ID3D11Device* Device, ID3D11DeviceContext* DeviceContext, FInstanceBuffer& InOutBuffer)
{
    InOutBuffer.Update(Device, DeviceContext,
        SpriteInstanceDataBuffer.empty() ? nullptr : SpriteInstanceDataBuffer.data(),
        static_cast<uint32>(SpriteInstanceDataBuffer.size()));
}

// Function : Sort sprite particles by view-projection depth (D9 ViewProjDepth)
// input : CameraPos — world-space camera position (Component->GetCachedCameraPosition())
// output : SpriteInstanceDataBuffer is reordered back-to-front
//
// 알고리즘:
//   1. 각 SpriteInstanceDataBuffer entry의 view depth = (Position - CameraPos).Size()
//   2. 큰 depth 먼저 (back-to-front for alpha blending)
//   3. SpriteInstanceDataBuffer를 정렬 (이 buffer가 GPU에 push되므로 직접 정렬)
//
// Note: ParticleIndices 재배치 대신 SpriteInstanceDataBuffer를 직접 정렬하는 이유 —
//       Sprite는 raw FBaseParticle을 vertex로 변환한 결과(SpriteInstanceDataBuffer)를 GPU에 push.
//       raw ParticleData는 어차피 GPU가 안 보므로 indices 재배치 효과 없음.
//       buffer 직접 정렬이 더 간결.
void FDynamicSpriteEmitterData::Sort(const FVector& CameraPos)
{
    switch (Source.SortMode)
    {
    case ESortMode::ViewProjDepth:
    {
        // back-to-front: 큰 depth 먼저.
        // squared distance로 비교 (sqrt 생략 — 단조 증가 함수).
        std::sort(SpriteInstanceDataBuffer.begin(), SpriteInstanceDataBuffer.end(),
            [&CameraPos](const FSpriteParticleInstanceData& A, const FSpriteParticleInstanceData& B)
            {
                const float DepthA = (A.Position - CameraPos).SizeSquared();
                const float DepthB = (B.Position - CameraPos).SizeSquared();
                return DepthA > DepthB; // 큰 depth 먼저
            });
        break;
    }
    case ESortMode::DistanceToView:
        // TODO: implement DistanceToView — ViewProjDepth 와 의미 유사하나 perspective projection 적용된 depth 사용.
        break;
    case ESortMode::Age_OldestFirst:
        // TODO: implement Age_OldestFirst — Particle->RelativeTime 기반 (1.0에 가까운 것 먼저).
        // 현재 SpriteInstanceDataBuffer 에는 RelativeTime 없음 — BuildFromInstance 단계에서 capture 필요.
        break;
    case ESortMode::Age_NewestFirst:
        // TODO: implement Age_NewestFirst — Particle->RelativeTime 기반 (0.0에 가까운 것 먼저).
        break;
    case ESortMode::None:
    default:
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Mesh DynamicData (Phase 3)
// ──────────────────────────────────────────────────────────────────────────────

// Function : Build Mesh InstanceData directly from particle simulation state
// input : Instance — Mesh derived emitter instance (read-only access)
// output : MeshInstanceDataBuffer filled with one entry per active mesh particle (alignment + spin combined)
//
// Phase 5 본문 이관: 기존 FParticleMeshEmitterInstance::BuildInstanceData() 의 본문을 본 함수로.
// alignment (PSA_Velocity / PSA_FacingCameraPosition) + spin (M2 payload Rotation) 결합 후 Euler 추출.
//
// Cycle 14 (M1+M2, 결정 21 A): Final = SpinMatrix * AlignmentMatrix (row-vector convention).
// 위험 12 방어: bCachedCameraValid=false 면 PSA_FacingCameraPosition → PSA_Velocity fallback.
void FDynamicMeshEmitterData::BuildFromInstance(const FParticleEmitterInstance& Instance)
{
    MeshInstanceDataBuffer.clear();
    const int32 ActiveCount = Instance.GetActiveParticleCount();
    if (ActiveCount <= 0)
    {
        return;
    }

    // alignment 모드 lookup (frame 단위 1회).
    EMeshAlignment AlignmentMode = EMeshAlignment::PSA_Velocity;
    const FCompiledParticleLODData* CompiledLOD = Instance.GetCurrentCompiledLODData();
    if (const UParticleMeshRendererProperties* MeshRenderer =
        CompiledLOD ? Cast<UParticleMeshRendererProperties>(CompiledLOD->RendererProperties) : nullptr)
    {
        AlignmentMode = MeshRenderer->GetAlignment();
    }

    // Component cached camera (Cycle 14 결정 18 β).
    const UParticleSystemComponent* OwningComp = Instance.GetOwningComponent();
    const FVector ComponentScale = OwningComp ? OwningComp->GetWorldScale() : FVector::OneVector;
    const bool bCameraValid = OwningComp && OwningComp->IsCachedCameraValid();
    const FVector CameraPos = bCameraValid ? OwningComp->GetCachedCameraPosition() : FVector::ZeroVector;
    const FVector WorldUp(0.0f, 0.0f, 1.0f);

    const EMeshAlignment EffectiveAlignment =
        (AlignmentMode == EMeshAlignment::PSA_FacingCameraPosition && !bCameraValid)
            ? EMeshAlignment::PSA_Velocity
            : AlignmentMode;

    // Mesh derived 의 GetMeshPayload helper 가 필요 — base instance interface 가 noun-typed access path 미제공.
    // Phase 5 이관 시 const-correct path 가 필요: derived 의 public helper 가 non-const 인 경우 cast.
    // GetMeshPayload 는 SlotIndex 만 받고 internal pointer 반환 — 데이터 read 전용.
    FParticleMeshEmitterInstance& NonConstInstance = const_cast<FParticleMeshEmitterInstance&>(
        static_cast<const FParticleMeshEmitterInstance&>(Instance));

    MeshInstanceDataBuffer.reserve(ActiveCount);
    for (int32 i = 0; i < ActiveCount; ++i)
    {
        const FBaseParticle* Particle = Instance.GetParticle(i);
        if (!Particle)
        {
            continue;
        }
        const uint16* Indices = Instance.GetParticleIndices();
        if (!Indices)
        {
            continue;
        }
        const int32 SlotIndex = static_cast<int32>(Indices[i]);
        FMeshRotationPayload* Payload = NonConstInstance.GetMeshPayload(SlotIndex);

        const FVector PayloadRotation = Payload ? Payload->Rotation : FVector::ZeroVector;
        const FMatrix SpinMatrix = MakeShaderEulerRotation(PayloadRotation);

        const FVector ParticleWorldLocation = Instance.ResolveParticleLocationForRender(Particle->Location);
        FMatrix AlignmentMatrix = FMatrix::Identity;
        switch (EffectiveAlignment)
        {
        case EMeshAlignment::PSA_Velocity:
            AlignmentMatrix = MakeAlignmentMatrix(Instance.ResolveParticleVectorForRender(Particle->Velocity), WorldUp);
            break;
        case EMeshAlignment::PSA_FacingCameraPosition:
        {
            const FVector ToCamera = CameraPos - ParticleWorldLocation;
            AlignmentMatrix = MakeAlignmentMatrix(ToCamera, WorldUp);
            break;
        }
        default:
            break;
        }

        const FMatrix Final = SpinMatrix * AlignmentMatrix;

        FMeshParticleInstanceData Data;
        Data.InstancePosition = ParticleWorldLocation;
        Data.InstanceRotation = ExtractShaderEuler(Final);
        Data.InstanceScale    = Particle->Size * ComponentScale;
        Data.InstanceColor    = Particle->Color;
        MeshInstanceDataBuffer.push_back(Data);
    }
}

void FDynamicMeshEmitterData::FillVertexBuffer(ID3D11Device* Device, ID3D11DeviceContext* DeviceContext, FInstanceBuffer& InOutBuffer)
{
    InOutBuffer.Update(Device, DeviceContext,
        MeshInstanceDataBuffer.empty() ? nullptr : MeshInstanceDataBuffer.data(),
        static_cast<uint32>(MeshInstanceDataBuffer.size()));
}

// Function : Sort Mesh particles by view-projection depth (D9 ViewProjDepth, D10 Mesh 포함)
// 알고리즘: Sprite 와 동일 — MeshInstanceDataBuffer 직접 정렬, back-to-front.
void FDynamicMeshEmitterData::Sort(const FVector& CameraPos)
{
    switch (Source.SortMode)
    {
    case ESortMode::ViewProjDepth:
    {
        std::sort(MeshInstanceDataBuffer.begin(), MeshInstanceDataBuffer.end(),
            [&CameraPos](const FMeshParticleInstanceData& A, const FMeshParticleInstanceData& B)
            {
                const float DepthA = (A.InstancePosition - CameraPos).SizeSquared();
                const float DepthB = (B.InstancePosition - CameraPos).SizeSquared();
                return DepthA > DepthB; // 큰 depth 먼저
            });
        break;
    }
    case ESortMode::DistanceToView:
        // TODO: implement DistanceToView
        break;
    case ESortMode::Age_OldestFirst:
        // TODO: implement Age_OldestFirst — RelativeTime capture 필요
        break;
    case ESortMode::Age_NewestFirst:
        // TODO: implement Age_NewestFirst — RelativeTime capture 필요
        break;
    case ESortMode::None:
    default:
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Beam DynamicData (Phase 3)
// ──────────────────────────────────────────────────────────────────────────────

// Function : Build Beam strip vertices directly from particle simulation state
// input : Instance — Beam derived emitter instance
// output : BeamVertexBuffer filled with 2 vertices per (interpolation point + 1) per active beam + degenerate seams
//
// Phase 5 본문 이관: 기존 FParticleBeamEmitterInstance::BuildVertexBuffer() 본문을 본 함수로.
// 위험 5/7/8 방어 코드 유지 (Source/Target nullptr fallback, zero-length skip, interp clamp).
// Cycle 13b Noise perturbation 그대로 이관.
void FDynamicBeamEmitterData::BuildFromInstance(const FParticleEmitterInstance& Instance)
{
    BeamVertexBuffer.clear();

    const int32 ActiveCount = Instance.GetActiveParticleCount();
    if (ActiveCount <= 0)
    {
        return;
    }

    const FCompiledParticleLODData* CompiledLOD = Instance.GetCurrentCompiledLODData();
    UParticleLODLevel* LOD = CompiledLOD && CompiledLOD->SourceLODLevel
        ? CompiledLOD->SourceLODLevel
        : Instance.GetCurrentLODLevel();
    const UParticleBeamRendererProperties* BeamRenderer =
        CompiledLOD ? Cast<UParticleBeamRendererProperties>(CompiledLOD->RendererProperties) : nullptr;
    if (!LOD || !BeamRenderer)
    {
        return;
    }

    UParticleModuleBeamSource* SourceModule = FindFirstModule<UParticleModuleBeamSource>(LOD);
    UParticleModuleBeamTarget* TargetModule = FindFirstModule<UParticleModuleBeamTarget>(LOD);
    const UParticleModuleBeamNoise* NoiseModule = FindFirstModule<UParticleModuleBeamNoise>(LOD);
    const int32 NoiseFrequency = NoiseModule ? MathUtil::Clamp(NoiseModule->GetFrequency(), 0, BeamNoiseMaxFrequency) : 0;
    const bool bTargetNoise = NoiseModule ? NoiseModule->IsTargetNoise() : false;
    const bool bSmooth = NoiseModule ? NoiseModule->IsSmooth() : false;
    const bool bApplyNoise = (NoiseModule != nullptr) && (NoiseFrequency > 0);

    USceneComponent* SourceComp = SourceModule ? SourceModule->GetSourceComponent() : nullptr;
    USceneComponent* TargetComp = TargetModule ? TargetModule->GetTargetComponent() : nullptr;
    const bool bUseLocalSource = SourceModule ? SourceModule->IsUseLocalSource() : false;
    const bool bUseLocalTarget = TargetModule ? TargetModule->IsUseLocalTarget() : false;


    const FVector EmitterLocation = Instance.GetComponentWorldLocation();
    const UParticleSystemComponent* OwningComp = Instance.GetOwningComponent();
    const FVector EmitterForward = OwningComp ? OwningComp->GetForwardVector() : FVector(1.0f, 0.0f, 0.0f);
    const FVector EmitterRight   = OwningComp ? OwningComp->GetRightVector()   : FVector(0.0f, 1.0f, 0.0f);
    const FVector EmitterUp      = OwningComp ? OwningComp->GetUpVector()      : FVector(0.0f, 0.0f, 1.0f);
    const int32 InterpCount = std::clamp(BeamRenderer->GetInterpolationPoints(), 0, BeamInterpolationPointsMax);
    const int32 SegmentCount = InterpCount + 1;
    FParticleBeamEmitterInstance& NonConstInstance = const_cast<FParticleBeamEmitterInstance&>(
        static_cast<const FParticleBeamEmitterInstance&>(Instance));
    const uint16* Indices = Instance.GetParticleIndices();
    if (!Indices)
    {
        return;
    }

    for (int32 ActiveIdx = 0; ActiveIdx < ActiveCount; ++ActiveIdx)
    {
        const FBaseParticle* Particle = Instance.GetParticle(ActiveIdx);
        if (!Particle)
        {
            continue;
        }
        const float ParticleLifeTime = std::clamp(Particle->RelativeTime, 0.0f, 1.0f);
        const FVector NoiseRange = NoiseModule
            ? NoiseModule->EvaluateVectorDistribution("NoiseRange", NoiseModule->GetNoiseRange(), NoiseModule->GetNoiseRange(), ParticleLifeTime)
            : FVector::ZeroVector;
        const FVector EvaluatedSourceLocalVec = SourceModule
            ? SourceModule->EvaluateVectorDistribution("SourceLocalVector", SourceModule->GetSourceLocalVector(), SourceModule->GetSourceLocalVector(), ParticleLifeTime)
            : FVector::ZeroVector;
        const FVector EvaluatedTargetLocalVec = TargetModule
            ? TargetModule->EvaluateVectorDistribution("TargetLocalVector", TargetModule->GetTargetLocalVector(), TargetModule->GetTargetLocalVector(), ParticleLifeTime)
            : FVector::ZeroVector;
        const FVector EvaluatedSrcTangentLocal = SourceModule
            ? SourceModule->EvaluateVectorDistribution("SourceTangent", SourceModule->GetSourceTangent(), SourceModule->GetSourceTangent(), ParticleLifeTime)
            : FVector(1.0f, 0.0f, 0.0f);
        const float EvaluatedSrcTangentStrength = SourceModule
            ? SourceModule->EvaluateFloatDistribution("SourceTangentStrength", SourceModule->GetSourceTangentStrength(), SourceModule->GetSourceTangentStrength(), ParticleLifeTime)
            : 0.0f;
        const FVector EvaluatedTgtTangentLocal = TargetModule
            ? TargetModule->EvaluateVectorDistribution("TargetTangent", TargetModule->GetTargetTangent(), TargetModule->GetTargetTangent(), ParticleLifeTime)
            : FVector(-1.0f, 0.0f, 0.0f);
        const float EvaluatedTgtTangentStrength = TargetModule
            ? TargetModule->EvaluateFloatDistribution("TargetTangentStrength", TargetModule->GetTargetTangentStrength(), TargetModule->GetTargetTangentStrength(), ParticleLifeTime)
            : 0.0f;
        const bool bUseHermite = (EvaluatedSrcTangentStrength > BeamSmallNumber) || (EvaluatedTgtTangentStrength > BeamSmallNumber);
        const FVector SourceLocation = bUseLocalSource
            ? EmitterLocation + EmitterForward * EvaluatedSourceLocalVec.X + EmitterRight * EvaluatedSourceLocalVec.Y + EmitterUp * EvaluatedSourceLocalVec.Z
            : (SourceComp ? SourceComp->GetWorldLocation() : EmitterLocation);
        const FVector SrcTangentWorld = bUseHermite
            ? (EmitterForward * EvaluatedSrcTangentLocal.X + EmitterRight * EvaluatedSrcTangentLocal.Y + EmitterUp * EvaluatedSrcTangentLocal.Z).GetSafeNormal() * EvaluatedSrcTangentStrength
            : FVector::ZeroVector;
        const FVector TgtTangentWorld = bUseHermite
            ? (EmitterForward * EvaluatedTgtTangentLocal.X + EmitterRight * EvaluatedTgtTangentLocal.Y + EmitterUp * EvaluatedTgtTangentLocal.Z).GetSafeNormal() * EvaluatedTgtTangentStrength
            : FVector::ZeroVector;
        const float EvaluatedFallbackDistance = BeamRenderer->GetFallbackDistance();
        const float TextureTile = BeamRenderer->GetTextureTile();
        const float TextureTileDistance = BeamRenderer->GetTextureTileDistance();

        FVector TargetLocation;
        if (bUseLocalTarget)
        {
            TargetLocation = EmitterLocation
                + EmitterForward * EvaluatedTargetLocalVec.X
                + EmitterRight   * EvaluatedTargetLocalVec.Y
                + EmitterUp      * EvaluatedTargetLocalVec.Z;
        }
        else if (TargetComp)
        {
            TargetLocation = TargetComp->GetWorldLocation();
        }
        else
        {
            TargetLocation = SourceLocation + EmitterForward * EvaluatedFallbackDistance;
        }

        const FVector BeamVector = TargetLocation - SourceLocation;
        const float BeamLength = BeamVector.Size();
        if (BeamLength < BeamSmallNumber)
        {
            continue;
        }

        const FVector LinearTangent = BeamVector / BeamLength;
        const FVector LinearPerp = ComputeBeamPerpendicular(LinearTangent);
        const float HalfSize = Particle->Size.X * 0.5f;

        // Linear 경로용 1회 계산 axes — Hermite 분기에서는 segment 별로 재계산.
        FVector LinearPerp1 = FVector::ZeroVector;
        FVector LinearPerp2 = FVector::ZeroVector;
        const FParticleBeamPayload* NoisePayload = nullptr;
        if (bApplyNoise)
        {
            ComputeBeamLocalAxes(LinearTangent, LinearPerp1, LinearPerp2);
            const int32 SlotIndex = static_cast<int32>(Indices[ActiveIdx]);
            NoisePayload = NonConstInstance.GetBeamPayload(SlotIndex);
        }

        const size_t BeamStartCount = BeamVertexBuffer.size();
        FVector PrevCenterPos = SourceLocation;
        float AccumDist = 0.0f;
        for (int32 SegIdx = 0; SegIdx <= SegmentCount; ++SegIdx)
        {
            const float Alpha = static_cast<float>(SegIdx) / static_cast<float>(SegmentCount);

            // Position / Tangent / Perp / NoiseAxes — Hermite 시 per-segment, linear 시 1회 계산값 재사용.
            FVector CenterPos;
            FVector SegTangent;
            FVector SegPerp;
            FVector SegPerp1;
            FVector SegPerp2;
            if (bUseHermite)
            {
                CenterPos = EvaluateHermite(SourceLocation, SrcTangentWorld, TargetLocation, TgtTangentWorld, Alpha);
                FVector Deriv = EvaluateHermiteDerivative(SourceLocation, SrcTangentWorld, TargetLocation, TgtTangentWorld, Alpha);
                if (Deriv.SizeSquared() < BeamSmallNumber)
                {
                    SegTangent = LinearTangent;
                }
                else
                {
                    SegTangent = Deriv.GetSafeNormal();
                }
                SegPerp = ComputeBeamPerpendicular(SegTangent);
                if (bApplyNoise)
                {
                    ComputeBeamLocalAxes(SegTangent, SegPerp1, SegPerp2);
                }
            }
            else
            {
                CenterPos = SourceLocation + BeamVector * Alpha;
                SegTangent = LinearTangent;
                SegPerp = LinearPerp;
                SegPerp1 = LinearPerp1;
                SegPerp2 = LinearPerp2;
            }

            if (bApplyNoise && NoisePayload && NoiseFrequency > 0 &&
                SegIdx > 0 && !(SegIdx == SegmentCount && !bTargetNoise))
            {
                const float SampleIdxF = Alpha * static_cast<float>(NoiseFrequency - 1);
                FVector Sample;
                if (bSmooth && NoiseFrequency >= 2)
                {
                    const int32 IdxLo = std::clamp(static_cast<int32>(std::floor(SampleIdxF)), 0, NoiseFrequency - 1);
                    const int32 IdxHi = std::clamp(IdxLo + 1, 0, NoiseFrequency - 1);
                    const float Frac = SampleIdxF - static_cast<float>(IdxLo);
                    Sample = NoisePayload->NoiseSamples[IdxLo] * (1.0f - Frac) + NoisePayload->NoiseSamples[IdxHi] * Frac;
                }
                else
                {
                    const int32 Idx = std::clamp(static_cast<int32>(std::round(SampleIdxF)), 0, NoiseFrequency - 1);
                    Sample = NoisePayload->NoiseSamples[Idx];
                }
                const FVector WorldOffset =
                    SegTangent * (Sample.X * NoiseRange.X) +
                    SegPerp1   * (Sample.Y * NoiseRange.Y) +
                    SegPerp2   * (Sample.Z * NoiseRange.Z);
                CenterPos = CenterPos + WorldOffset;
            }

            // AccumDist: linear 일 때는 직선 가정 (BeamLength*Alpha 와 동일 결과), Hermite 일 때는 segment 거리 누적.
            if (SegIdx > 0)
            {
                AccumDist += (CenterPos - PrevCenterPos).Size();
            }
            PrevCenterPos = CenterPos;

            const float TexU = (TextureTileDistance > BeamSmallNumber)
                ? (AccumDist / TextureTileDistance)
                : (Alpha * TextureTile);

            FBeamParticleVertex V0;
            V0.Position = CenterPos + SegPerp * HalfSize;
            V0.Tangent = SegTangent;
            V0.Color = Particle->Color;
            V0.TexCoordU = TexU;
            V0.Size = Particle->Size.X;
            if (SegIdx == 0 && ActiveIdx > 0)
            {
                BeamVertexBuffer.push_back(V0);
            }

            BeamVertexBuffer.push_back(V0);

            FBeamParticleVertex V1 = V0;
            V1.Position = CenterPos - SegPerp * HalfSize;
            BeamVertexBuffer.push_back(V1);
        }

        if (BeamVertexBuffer.size() > BeamStartCount && ActiveIdx + 1 < ActiveCount)
        {
            BeamVertexBuffer.push_back(BeamVertexBuffer.back());
        }
    }
}

void FDynamicBeamEmitterData::FillVertexBuffer(ID3D11Device* Device, ID3D11DeviceContext* DeviceContext, FInstanceBuffer& InOutBuffer)
{
    InOutBuffer.Update(Device, DeviceContext,
        BeamVertexBuffer.empty() ? nullptr : BeamVertexBuffer.data(),
        static_cast<uint32>(BeamVertexBuffer.size()));
}

// ──────────────────────────────────────────────────────────────────────────────
// Ribbon DynamicData placeholder (Phase 4, 옵션 C)
// ──────────────────────────────────────────────────────────────────────────────

// Function : Snapshot Ribbon VertexBuffer from instance's existing buffer
// 옵션 C: Ribbon emitter 시뮬레이션 코드는 무수정 (D6) — instance 의 GetRibbonVertexData() 호출만으로 충분.
void FDynamicRibbonEmitterData::BuildFromInstance(const FParticleEmitterInstance& Instance)
{
    RibbonVertexBuffer.clear();
    uint32 Count = 0;
    const FRibbonParticleVertex* Src = Instance.GetRibbonVertexData(Count);
    if (!Src || Count == 0)
    {
        return;
    }
    RibbonVertexBuffer.assign(Src, Src + Count);
}

void FDynamicRibbonEmitterData::FillVertexBuffer(ID3D11Device* Device, ID3D11DeviceContext* DeviceContext, FInstanceBuffer& InOutBuffer)
{
    InOutBuffer.Update(Device, DeviceContext,
        RibbonVertexBuffer.empty() ? nullptr : RibbonVertexBuffer.data(),
        static_cast<uint32>(RibbonVertexBuffer.size()));
}
