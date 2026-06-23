#pragma once

#include "Core/Reflection/ReflectionMacros.h"
#include "Math/Vector.h"

// Cycle 14 (M1): Mesh emitter의 alignment (forward axis 정렬) 모드.
// UE Cascade EParticleScreenAlignment 와 의미 호환 — 본 cycle 은 결정 17 옵션 B 채택으로 2값만 도입.
// 후속 cycle 에서 PSA_LockedAxis / PSA_Rectangle 등 추가 시 enum 값 추가만으로 확장 가능 (UPROPERTY 자동 노출).
//
//   PSA_Velocity                — particle 의 velocity 방향으로 mesh 의 local +X 축을 정렬.
//                                  velocity 가 zero 면 alignment 적용 안 함 (identity).
//   PSA_FacingCameraPosition    — camera position 방향으로 mesh 의 local +X 축을 정렬 (look-at).
//                                  Component-cached camera 미설정 시 PSA_Velocity 로 fallback (위험 12 방어).
UENUM()
enum class EMeshAlignment : uint8
{
    PSA_Velocity              UMETA(DisplayName = "Velocity"),
    PSA_FacingCameraPosition  UMETA(DisplayName = "Facing Camera Position"),
};

// Mesh emitter의 per-particle payload (Cycle 11, 옵션 B).
// FBaseParticle 뒤에 PayloadOffset 위치에 인터리브 배치된다.
// container.Allocate(ParticleSize + RequiredPayloadBytes())가 stride에 자동 가산 — Cycle 10d 의 ξ 해소 실측.
//
// 3축 자유 회전 지원:
//   InitialOrientation : spawn 시점에 결정된 base Euler 각 (0,0,0)부터 모듈러 설정 가능.
//   Rotation           : 매 frame BuildInstanceData에서 RotRate * DeltaTime 누적된 현재 Euler.
//   RotRate            : 축별 각속도. spawn 시 모듈러 설정, Cycle 11 본 cycle에서는 0 고정.
//
// SlotIndex(physical) 기반으로 접근해야 swap-pop 안전 (linked list 의존 없음 — 단순 payload).
struct FMeshRotationPayload
{
    FVector InitialOrientation;  // 12B (offset 0)
    FVector Rotation;            // 12B (offset 12)
    FVector RotRate;             // 12B (offset 24)
};                               // 36B

static_assert(sizeof(FMeshRotationPayload) == 36,
    "FMeshRotationPayload must be tight-packed at 36 bytes — mesh renderer RequiredPayloadBytes() depends on this");
