#pragma once

#include "Particle/ParticleBeamTypes.h"
#include "Particle/ParticleModuleTypeData.h"
#include "Render/Resource/Material.h"

// Beam emitter용 TypeData (Cycle 13a, 결정 13 옵션 A + 결정 15 옵션 B).
// RequiredPayloadBytes()가 sizeof(FParticleBeamPayload)를 반환 — container Stride에 자동 가산 (Cycle 10d 의 ξ 해소 패턴 세 번째 실측).
// CreateInstance()가 FParticleBeamEmitterInstance를 반환해 SpawnParticles/Tick override 가 작동한다.
//
// 결정 12 옵션 B: bRenderNoise 등 Noise 관련 멤버는 본 sub-cycle 제외 — 후속 cycle (13b) 에서 추가.
// 결정 15 옵션 B (PEB2M_Target only): BeamMethod enum 미도입. Target 모듈 nullptr 시 fallback 으로
// Source + Forward * FallbackDistance 사용 (BuildVertexBuffer 내부에서 처리).
UCLASS()
class UBeamTypeData : public UParticleModuleTypeDataBase
{
public:
    GENERATED_BODY(UBeamTypeData, UParticleModuleTypeDataBase)

    int32 RequiredPayloadBytes() const override { return sizeof(FParticleBeamPayload); }
    EParticleEmitterRenderMode GetRenderMode() const override { return EParticleEmitterRenderMode::Beam; }
    FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* Component, int32 EmitterIndex) const override;

    int32 GetMaxBeamCount() const { return MaxBeamCount; }
    int32 GetInterpolationPoints() const { return InterpolationPoints; }
    float GetFallbackDistance() const { return FallbackDistance; }
    float GetTextureTile() const { return TextureTile; }
    float GetTextureTileDistance() const { return TextureTileDistance; }
    UMaterialInterface* GetMaterial() const { return Material; }

    // Detail panel 의 picker 가 호출 — Material 만 변경. 다른 멤버는 reflection 으로 자동 노출됨.
    void SetMaterial(UMaterialInterface* InMaterial) { Material = InMaterial; }
    void SetMaxBeamCount(int32 InCount) { MaxBeamCount = InCount; }
    void SetInterpolationPoints(int32 InCount) { InterpolationPoints = InCount; }
    void SetFallbackDistance(float InValue) { FallbackDistance = InValue; }
    void SetTextureTile(float InValue) { TextureTile = InValue; }
    void SetTextureTileDistance(float InValue) { TextureTileDistance = InValue; }

private:
    // 결정 13 옵션 A: emitter 당 동시 beam 수. 1 이면 single beam, >1 이면 multi-beam.
    // Ribbon 의 MaxTrailCount 와 동일 패턴 — round-robin spawn 분배에 사용.
    UPROPERTY(DisplayName = "Max Beam Count", Category = "Beam", Min = 1)
    int32 MaxBeamCount = 1;

    // beam 직선을 InterpolationPoints + 1 등분한 분할점 수. 0 이면 Source/Target 직접 연결 (2 정점만).
    // 위험 8 방어: UPROPERTY Min/Max 로 음수 / 과대 값 1차 차단 (BuildVertexBuffer 의 clamp 가 2차 방어).
    UPROPERTY(DisplayName = "Interpolation Points", Category = "Beam", Min = 0, Max = 64)
    int32 InterpolationPoints = 0;

    // Target 모듈 부재 시 Source + Forward * FallbackDistance 로 가짜 target 생성 (PEB2M_Distance fallback).
    UPROPERTY(DisplayName = "Fallback Distance", Category = "Beam", Min = 0.0f)
    float FallbackDistance = 100.0f;

    // UV.U 반복 — TextureTile=1 이면 strip 전체에 텍스처 1회 stretch.
    UPROPERTY(DisplayName = "Texture Tile", Category = "Beam", Min = 0.0f)
    float TextureTile = 1.0f;

    // 0 이면 stretch 모드 (TexCoordU = 0..1 비율). >0 이면 distance / TextureTileDistance 로 누적 반복.
    UPROPERTY(DisplayName = "Texture Tile Distance", Category = "Beam", Min = 0.0f)
    float TextureTileDistance = 0.0f;

    UPROPERTY(DisplayName = "Material", Category = "Beam", ReferenceKind = Asset)
    UMaterialInterface* Material = nullptr;
};
