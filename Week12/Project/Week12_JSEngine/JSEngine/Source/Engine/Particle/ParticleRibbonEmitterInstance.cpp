#include "Particle/ParticleRibbonEmitterInstance.h"

#include <algorithm>

#include "Particle/ParticleRendererProperties.h"
#include "Particle/ParticleSystem.h"

#include <cmath>

namespace ParticleRibbonUtils
{
    // dt 보정용 (결정 7 옵션 B): 0-division 방어 + tangent 추정 시 임계값.
    constexpr float RibbonSmallNumber = 1.0e-6f;

    // strip 폭 방향 perpendicular 생성 — world Up (0,0,1) 가정 (Cycle 12 작업 7 직전 결정).
    // 후속 cycle (12c: View-aligned ribbon) 에서 camera up 으로 교체 가능.
    // Tangent 가 Up 과 평행한 경우 fallback 으로 X 축 사용 — degenerate strip 회피.
    FVector ComputePerpendicular(const FVector& Tangent)
    {
        const FVector Up(0.0f, 0.0f, 1.0f);
        FVector Perp = FVector::CrossProduct(Tangent, Up);
        if (Perp.SizeSquared() < RibbonSmallNumber)
        {
            Perp = FVector::CrossProduct(Tangent, FVector(1.0f, 0.0f, 0.0f));
        }
        Perp.Normalize();
        return Perp;
    }

    FVector RotateAroundAxis(const FVector& Vector, const FVector& Axis, float Radians)
    {
        const FVector UnitAxis = Axis.GetSafeNormal();
        const float CosAngle = std::cos(Radians);
        const float SinAngle = std::sin(Radians);
        return Vector * CosAngle
            + FVector::CrossProduct(UnitAxis, Vector) * SinAngle
            + UnitAxis * FVector::DotProduct(UnitAxis, Vector) * (1.0f - CosAngle);
    }
}

// Function : Lookup ribbon payload by physical slot index
// input : SlotIndex (physical slot in ParticleStorage.ParticleData)
// output : pointer to interleaved FRibbonParticlePayload, or nullptr when storage not ready / SlotIndex invalid
//
// 위험 1 방어 (진단 §5.3): SlotIndex 음수 또는 MaxParticles 초과면 nullptr — chain dead-end / sentinel.
FRibbonParticlePayload* FParticleRibbonEmitterInstance::GetRibbonPayload(int32 SlotIndex)
{
    if (!ParticleStorage.ParticleData || SlotIndex < 0 || SlotIndex >= GetMaxActiveParticleCount())
    {
        return nullptr;
    }
    uint8* ParticleBase = ParticleStorage.ParticleData + SlotIndex * ParticleStorage.GetStride();
    return reinterpret_cast<FRibbonParticlePayload*>(ParticleBase + PayloadOffset);
}

// Function : Lookup base particle by physical slot index (not active index)
// input : SlotIndex
// output : pointer to FBaseParticle stored at slot, or nullptr when invalid
FBaseParticle* FParticleRibbonEmitterInstance::GetParticleBySlot(int32 SlotIndex)
{
    if (!ParticleStorage.ParticleData || SlotIndex < 0 || SlotIndex >= GetMaxActiveParticleCount())
    {
        return nullptr;
    }
    return reinterpret_cast<FBaseParticle*>(ParticleStorage.ParticleData + SlotIndex * ParticleStorage.GetStride());
}

// Function : Resize HeadIndices to MaxTrailCount and reset all heads to -1 (empty trail)
// input : None
// output : HeadIndices.size() == MaxTrailCount, all entries -1, NextTrailIndex = 0
//
// 첫 Tick 진입 시 또는 HeadIndices 가 비어있을 때 호출. RendererProperties의 MaxTrailCount 가 frame 중 변하지 않는다고 가정.
void FParticleRibbonEmitterInstance::EnsureTrailState()
{
    int32 MaxTrails = 1;
    const FCompiledParticleLODData* CompiledLOD = GetCurrentCompiledLODData();
    const UParticleRibbonRendererProperties* RibbonRenderer =
        CompiledLOD ? Cast<UParticleRibbonRendererProperties>(CompiledLOD->RendererProperties) : nullptr;

    if (!RibbonRenderer)
    {
        if (UParticleLODLevel* LOD = GetCurrentLODLevel())
            RibbonRenderer = Cast<UParticleRibbonRendererProperties>(LOD->GetEffectiveRendererProperties());
    }

    if (RibbonRenderer)
        MaxTrails = std::max(RibbonRenderer->GetMaxTrailCount(), 1);

    if (static_cast<int32>(HeadIndices.size()) != MaxTrails)
    {
        HeadIndices.assign(MaxTrails, -1);
        NextTrailIndex = 0;
    }
}

// Function : Spawn ribbon particles — base spawn + linked list prepend + tangent init
// input : Count, StartTime, Increment, InitialLocation, InitialVelocity, EventPayload
// output : Base particles spawned + payload initialized + chain prepended + HeadIndices updated
//
// 위험 1/4 방어 (진단 §5.3):
//   - SlotIndex < 0 또는 payload nullptr 검사
//   - 모든 신규 slot 의 Prev/Next/TrailIndex/Tangent/SpawnedTangentStrength/Distance 명시 초기화 (garbage 회피)
void FParticleRibbonEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment,
                                                    const FVector& InitialLocation, const FVector& InitialVelocity,
                                                    FParticleEventInstancePayload* EventPayload)
{
    EnsureTrailState();

    const int32 OldActiveCount = ActiveParticles;
    FParticleEmitterInstance::SpawnParticles(Count, StartTime, Increment, InitialLocation, InitialVelocity, EventPayload);

    // RendererProperties 재조회 (TangentSpawningScalar 사용)
    const FCompiledParticleLODData* CompiledLOD = GetCurrentCompiledLODData();
    const UParticleRibbonRendererProperties* RibbonRenderer =
        CompiledLOD ? Cast<UParticleRibbonRendererProperties>(CompiledLOD->RendererProperties) : nullptr;
    if (!RibbonRenderer)
    {
        if (UParticleLODLevel* LOD = GetCurrentLODLevel())
            RibbonRenderer = Cast<UParticleRibbonRendererProperties>(LOD->GetEffectiveRendererProperties());
    }
    const int32 MaxTrails = std::max(static_cast<int32>(HeadIndices.size()), 1);

    // base 가 spawn 한 신규 particle range [OldActiveCount, ActiveParticles) — payload 초기화 + chain prepend.
    for (int32 ActiveIdx = OldActiveCount; ActiveIdx < ActiveParticles; ++ActiveIdx)
    {
        const int32 SlotIndex = static_cast<int32>(ParticleStorage.ParticleIndices[ActiveIdx]);
        FRibbonParticlePayload* Payload = GetRibbonPayload(SlotIndex);
        if (!Payload)
        {
            continue; // 위험 1 방어
        }

        // (a) trail 선택 — round-robin
        const int32 TrailIdx = NextTrailIndex;
        NextTrailIndex = (NextTrailIndex + 1) % MaxTrails;

        // (b) chain prepend (이 particle 이 새 head)
        const int32 OldHead = HeadIndices[TrailIdx];
        Payload->TrailIndex = TrailIdx;
        Payload->PrevIndex = -1;        // head 는 prev 없음
        Payload->NextIndex = OldHead;   // 이전 head 가 next

        if (OldHead >= 0)
        {
            if (FRibbonParticlePayload* OldHeadPayload = GetRibbonPayload(OldHead))
            {
                OldHeadPayload->PrevIndex = SlotIndex;
            }
        }
        HeadIndices[TrailIdx] = SlotIndex;

        // (c) tangent 초기화 — Velocity 기반 (variable dt 보정, 결정 7 옵션 B)
        FBaseParticle* Particle = GetParticleBySlot(SlotIndex);
        const FVector Velocity = Particle ? Particle->Velocity : FVector::ZeroVector;
        const float Speed = Velocity.Size();
        const float TangentScalar = RibbonRenderer ? RibbonRenderer->GetTangentSpawningScalar() : 0.0f;
        Payload->Tangent = (Speed > ParticleRibbonUtils::RibbonSmallNumber) ? (Velocity / Speed) : FVector(0.0f, 0.0f, 1.0f);
        Payload->SpawnedTangentStrength = std::max(TangentScalar, 0.0f) * Speed;
        Payload->Distance = 0.0f;
    }
}

// Function : Kill ribbon particle — detach from chain + update HeadIndices then call base swap-pop
// input : Index (active index)
// output : ActiveParticles decremented + linked list re-stitched + HeadIndices updated if head dies
//
// 위험 2/3 방어 (진단 §5.3):
//   - PrevSlot < 0 (죽는 particle 이 head) → HeadIndices[trail] = NextSlot 명시 갱신
//   - payload nullptr 검사로 invalid SlotIndex 참조 방어 (chain dead-end)
// base KillParticle 의 swap-pop 은 ParticleIndices 만 swap → SlotIndex 불변 → chain 의 다른 slot 영향 0.
void FParticleRibbonEmitterInstance::KillParticle(int32 Index)
{
    if (Index < 0 || Index >= ActiveParticles)
    {
        return;
    }

    const int32 DyingSlot = static_cast<int32>(ParticleStorage.ParticleIndices[Index]);
    if (FRibbonParticlePayload* Dying = GetRibbonPayload(DyingSlot))
    {
        const int32 PrevSlot = Dying->PrevIndex;
        const int32 NextSlot = Dying->NextIndex;
        const int32 TrailIdx = Dying->TrailIndex;

        // (a) prev 의 next 갱신
        if (PrevSlot >= 0)
        {
            if (FRibbonParticlePayload* Prev = GetRibbonPayload(PrevSlot))
            {
                Prev->NextIndex = NextSlot;
            }
        }
        else
        {
            // 죽는 particle 이 head — HeadIndices 갱신 (위험 2 방어)
            if (TrailIdx >= 0 && TrailIdx < static_cast<int32>(HeadIndices.size()))
            {
                HeadIndices[TrailIdx] = NextSlot;
            }
        }

        // (b) next 의 prev 갱신
        if (NextSlot >= 0)
        {
            if (FRibbonParticlePayload* Next = GetRibbonPayload(NextSlot))
            {
                Next->PrevIndex = PrevSlot;
            }
        }
    }

    // (c) base 호출 — swap-pop (진단 §3.1: ParticleIndices 만 swap, SlotIndex 불변)
    FParticleEmitterInstance::KillParticle(Index);
}

// Function : Tick ribbon emitter — base update + chain traversal for tangent/distance + strip rebuild
// input : DeltaTime
// output : Particle simulation advanced (base) + payload tangent/distance refreshed + VertexBuffer rebuilt
//
// 위험 3 방어 (진단 §5.3): chain 순회 중 payload nullptr 만나면 break (invalid SlotIndex 참조 회피).
// 결정 7 옵션 B (variable dt): tangent 는 위치 변화 (Delta / Step) 기반 — frame rate 비종속.
void FParticleRibbonEmitterInstance::Tick(float DeltaTime, bool bAllowSpawning)
{
    FParticleEmitterInstance::Tick(DeltaTime, bAllowSpawning);

    EnsureTrailState();

    // chain 순회 — head 부터 NextIndex 따라 tangent/distance 갱신.
    for (int32 TrailIdx = 0; TrailIdx < static_cast<int32>(HeadIndices.size()); ++TrailIdx)
    {
        int32 SlotIndex = HeadIndices[TrailIdx];
        float AccumDist = 0.0f;
        FVector PrevLocation = FVector::ZeroVector;
        bool bFirst = true;

        while (SlotIndex >= 0)
        {
            FRibbonParticlePayload* Payload = GetRibbonPayload(SlotIndex);
            if (!Payload)
            {
                break; // 위험 3 dead-end 방어
            }
            FBaseParticle* P = GetParticleBySlot(SlotIndex);
            if (!P)
            {
                break;
            }

            if (!bFirst)
            {
                const FVector Delta = P->Location - PrevLocation;
                const float Step = Delta.Size();
                AccumDist += Step;
                Payload->Distance = AccumDist;
                if (Step > ParticleRibbonUtils::RibbonSmallNumber)
                {
                    Payload->Tangent = Delta / Step;
                }
            }
            else
            {
                Payload->Distance = 0.0f;
            }

            PrevLocation = P->Location;
            bFirst = false;
            SlotIndex = Payload->NextIndex;
        }
    }

    BuildVertexBuffer();
}

// Function : Build strip vertices for all trails — slot 0 dynamic VB source
// input : None
// output : VertexBuffer cleared + filled with 2 vertices per active particle + degenerate seams between trails
//
// topology = TRIANGLESTRIP. multi-trail 사이에 마지막 vertex 1개 복제 (degenerate triangle) → strip 연결 끊김.
// 단일 trail 이거나 빈 trail 이 섞여있으면 seam 자동 생략.
void FParticleRibbonEmitterInstance::BuildVertexBuffer()
{
    VertexBuffer.clear();
    const FCompiledParticleLODData* CompiledLOD = GetCurrentCompiledLODData();
    const UParticleRibbonRendererProperties* RibbonRenderer =
        CompiledLOD ? Cast<UParticleRibbonRendererProperties>(CompiledLOD->RendererProperties) : nullptr;
    if (!RibbonRenderer)
    {
        if (UParticleLODLevel* LOD = GetCurrentLODLevel())
            RibbonRenderer = Cast<UParticleRibbonRendererProperties>(LOD->GetEffectiveRendererProperties());
    }
    const int32 MaxParticleInTrailCount = RibbonRenderer
        ? RibbonRenderer->GetMaxParticleInTrailCount()
        : 64;
    const int32 SheetsPerTrail = RibbonRenderer
        ? std::max(static_cast<int32>(std::round(RibbonRenderer->GetSheetsPerTrail())), 1)
        : 1;

    for (int32 TrailIdx = 0; TrailIdx < static_cast<int32>(HeadIndices.size()); ++TrailIdx)
    {
        for (int32 SheetIdx = 0; SheetIdx < SheetsPerTrail; ++SheetIdx)
        {
            int32 SlotIndex = HeadIndices[TrailIdx];
            const size_t TrailStartCount = VertexBuffer.size();
            int32 TrailParticleCount = 0;
            const float SheetAngle = (SheetsPerTrail > 1)
                ? (static_cast<float>(SheetIdx) / static_cast<float>(SheetsPerTrail)) * 3.1415926535f
                : 0.0f;

            while (SlotIndex >= 0 && TrailParticleCount < MaxParticleInTrailCount)
            {
                FRibbonParticlePayload* Payload = GetRibbonPayload(SlotIndex);
                if (!Payload)
                {
                    break;
                }
                FBaseParticle* P = GetParticleBySlot(SlotIndex);
                if (!P)
                {
                    break;
                }

                FVector Perp = ParticleRibbonUtils::ComputePerpendicular(Payload->Tangent);
                if (SheetsPerTrail > 1)
                {
                    Perp = ParticleRibbonUtils::RotateAroundAxis(Perp, Payload->Tangent, SheetAngle).GetSafeNormal();
                }
                const float HalfSize = P->Size.X * 0.5f;

                FRibbonParticleVertex V0;
                V0.Position = P->Location + Perp * HalfSize;
                V0.Tangent = Payload->Tangent;
                V0.Color = P->Color;
                V0.TexCoordU = Payload->Distance;
                V0.Size = P->Size.X;
                VertexBuffer.push_back(V0);

                FRibbonParticleVertex V1 = V0;
                V1.Position = P->Location - Perp * HalfSize;
                VertexBuffer.push_back(V1);

                ++TrailParticleCount;
                SlotIndex = Payload->NextIndex;
            }

            // multi-trail/sheet seam — 다음 strip 이 존재하면 마지막 vertex 복제 (degenerate)
            if (VertexBuffer.size() > TrailStartCount &&
                (SheetIdx + 1 < SheetsPerTrail || TrailIdx + 1 < static_cast<int32>(HeadIndices.size())) &&
                !VertexBuffer.empty())
            {
                VertexBuffer.push_back(VertexBuffer.back());
            }
        }
    }
}

// Function : Expose ribbon vertex buffer to Builder (Ribbon path override)
// input : OutCount (out-param)
// output : Pointer to first element + count, or nullptr/0 when empty
const FRibbonParticleVertex* FParticleRibbonEmitterInstance::GetRibbonVertexData(uint32& OutCount) const
{
    OutCount = static_cast<uint32>(VertexBuffer.size());
    return VertexBuffer.empty() ? nullptr : VertexBuffer.data();
}
