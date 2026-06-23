#pragma once

#include "Math/Color.h"
#include "Math/Vector.h"

// Ribbon emitter의 per-particle payload (Cycle 12, 결정 6 옵션 A).
// FBaseParticle 뒤에 PayloadOffset 위치에 인터리브 배치된다.
// container.Allocate(ParticleSize + RequiredPayloadBytes())가 stride에 자동 가산 — Cycle 10d 의 ξ 해소 패턴 재사용.
//
// linked list 기반 trail 관리:
//   NextIndex / PrevIndex 는 반드시 물리 SlotIndex (ParticleStorage.ParticleIndices[i] 가 가리키는 위치) 저장.
//   base KillParticle의 swap-pop은 ParticleIndices만 swap → SlotIndex 불변 → linked list 안전 (진단 §3.1 검증).
//   sentinel: -1 = chain 끝 (head 의 PrevIndex / tail 의 NextIndex).
struct FRibbonParticlePayload
{
    int32   NextIndex;                // 4 (offset 0)  — chain 다음 (SlotIndex, -1 = tail)
    int32   PrevIndex;                // 4 (offset 4)  — chain 이전 (SlotIndex, -1 = head)
    FVector Tangent;                  // 12 (offset 8) — 현재 tangent vector (정규화)
    float   SpawnedTangentStrength;   // 4 (offset 20) — spawn 시점 tangent 강도
    int32   TrailIndex;               // 4 (offset 24) — 어느 trail 소속 (0 ~ MaxTrailCount-1)
    float   Distance;                 // 4 (offset 28) — head 로부터 누적 거리
};                                    // 32B

static_assert(sizeof(FRibbonParticlePayload) == 32,
    "FRibbonParticlePayload must be tight-packed at 32 bytes — ribbon renderer RequiredPayloadBytes() depends on this");

// Ribbon strip 정점 (slot 0 per-vertex, no instancing — Mesh 와의 핵심 차이).
// 각 active particle 마다 strip 양쪽 (perpendicular 방향) 으로 2 vertex 생성.
// topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP.
struct FRibbonParticleVertex
{
    FVector Position;       // 12 (offset 0)
    FVector Tangent;        // 12 (offset 12)
    FColor  Color;          // 16 (offset 24)
    float   TexCoordU;      // 4  (offset 40) — V 는 strip 양쪽 (0/1) 에서 자동 결정
    float   Size;           // 4  (offset 44) — strip 폭
};                          // 48B

static_assert(sizeof(FRibbonParticleVertex) == 48,
    "FRibbonParticleVertex must be tight-packed at 48 bytes — RibbonParticleLayout offsets depend on this");
