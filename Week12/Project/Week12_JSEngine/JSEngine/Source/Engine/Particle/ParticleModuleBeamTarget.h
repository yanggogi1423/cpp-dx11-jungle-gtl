#pragma once

#include "Component/SceneComponent.h"
#include "Math/Vector.h"
#include "Object/ObjectPtr.h"
#include "Particle/ParticleModule.h"

// Beam emitter 의 Target 위치 공급 모듈 (Cycle 13a, 결정 10 옵션 A + 결정 11 옵션 B + 결정 15 옵션 B).
//
// Target 위치 결정 우선순위 (Beam instance 의 BuildVertexBuffer 에서 처리):
//   (1) bUseLocalTarget=true  → emitter 의 local space TargetLocalVector 사용
//       (Source + Forward*X + Right*Y + Up*Z, 여기서 Forward/Right/Up = OwningComp 의 world axes)
//       → TargetComponent 무시 (명시 override).
//   (2) bUseLocalTarget=false && TargetComponent != nullptr → TargetComponent->GetWorldLocation()
//   (3) 둘 다 부재 → Source + Forward * UParticleBeamRendererProperties::FallbackDistance (PEB2M_Distance fallback)
//
// (1) 의 use case: actor 가 회전하면서 beam 끝점도 회전 추적 (예: 무기 muzzle 에서 일정 offset, 또는
//                  static lightning that follows actor pose without needing a separate target component).
// (2) 의 use case: 다른 actor (enemy 등) 의 component 를 target 으로 lock-on.
UCLASS()
class UParticleModuleBeamTarget : public UParticleModule
{
public:
    GENERATED_BODY(UParticleModuleBeamTarget, UParticleModule)

    USceneComponent* GetTargetComponent() const { return TargetComponent.Get(); }
    void SetTargetComponent(USceneComponent* InComponent) { TargetComponent.Set(InComponent); }

    bool    IsUseLocalTarget() const          { return bUseLocalTarget; }
    void    SetUseLocalTarget(bool bIn)       { bUseLocalTarget = bIn; }

    FVector GetTargetLocalVector() const      { return TargetLocalVector; }
    void    SetTargetLocalVector(const FVector& In) { TargetLocalVector = In; }

    FVector GetTargetTangent() const                  { return TargetTangent; }
    void    SetTargetTangent(const FVector& In)       { TargetTangent = In; }

    float   GetTargetTangentStrength() const          { return TargetTangentStrength; }
    void    SetTargetTangentStrength(float In)        { TargetTangentStrength = In; }

private:
    UPROPERTY(DisplayName = "Target Component", Category = "Beam Target")
    TObjectPtr<USceneComponent> TargetComponent;

    // true 면 TargetComponent 를 무시하고 TargetLocalVector 를 emitter local space 좌표로 해석.
    // false 면 기존 동작 (TargetComponent → world location, 또는 fallback).
    UPROPERTY(DisplayName = "Use Local Target", Category = "Beam Target")
    bool bUseLocalTarget = false;

    // emitter (UParticleSystemComponent) 의 local space 좌표 — Forward(X) / Right(Y) / Up(Z) 축 기반.
    // Default (100, 0, 0): emitter 의 forward 방향으로 100 단위 — FallbackDistance default 와 동일 의미.
    UPROPERTY(DisplayName = "Target Local Vector", Category = "Beam Target")
    FVector TargetLocalVector = FVector(100.0f, 0.0f, 0.0f);

    // emitter-local 단위 방향 (Forward/Right/Up 기반). Source 측과 동일 좌표계.
    // Default (-1,0,0): emitter forward 의 반대 — 자연스러운 "도착" 방향. strength=0 일 때 무의미.
    UPROPERTY(DisplayName = "Target Tangent", Category = "Beam Target")
    FVector TargetTangent = FVector(-1.0f, 0.0f, 0.0f);

    // Hermite tangent 의 크기 (cm). SourceTangentStrength + TargetTangentStrength 양쪽 모두 0 이면 linear fallback.
    UPROPERTY(DisplayName = "Target Tangent Strength", Category = "Beam Target", Min = 0.0f)
    float TargetTangentStrength = 0.0f;
};
