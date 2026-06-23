#pragma once

#include "Core/CoreTypes.h"
#include "Math/Color.h"
#include "Math/Vector.h"

// Cycle 13b 분기 1 B-2: per-particle 영구 NoiseSample 모드 + 컴파일 타임 최대 frequency 8.
// 사용자가 NoiseModule.Frequency 를 1~BeamNoiseMaxFrequency 사이로 설정. spawn 시점에 Frequency 개만큼만
// 채우고 나머지 슬롯은 미사용 (BuildVertexBuffer 가 Frequency 까지만 lookup).
// 컴파일 타임 max 로 고정한 이유: 동적 할당 회피 + payload sizeof 정적 결정 → silent bug 8 (payload mismatch) 자동 방어.
inline constexpr int32 BeamNoiseMaxFrequency = 8;

// Beam emitter의 per-particle payload (Cycle 13a 초기 + Cycle 13b 확장).
// FBaseParticle 뒤에 PayloadOffset 위치에 인터리브 배치된다.
// container.Allocate(ParticleSize + RequiredPayloadBytes()) 가 stride 에 자동 가산 — Cycle 10d ξ 해소 패턴 네 번째 실측.
//
// Tick 추적 모드 (결정 11 B): Source/Target Location 은 payload 에 캐싱하지 않고 instance 가 매 frame
// SourceComponent/TargetComponent (Source/Target 모듈 보유) 의 GetWorldLocation() 으로 직접 조회.
//
// 결정 13 옵션 A (multi-beam): BeamIndex 가 0 ~ MaxBeamCount-1 범위. SpawnParticles 에서 round-robin 분배.
//
// 분기 1 B-2 (Cycle 13b): per-particle 영구 NoiseSamples — spawn 시 1회 random 호출 + lifetime 동안 고정 → 자동 결정성 (위험 6 방어).
struct FParticleBeamPayload
{
    int32   BeamIndex;                              // 4B  (offset 0)  — multi-beam 식별
    FVector NoiseSamples[BeamNoiseMaxFrequency];    // 96B (offset 4)  — 8 × FVector(12B) — Beam-local noise sample (분기 3 B 의 local 좌표 의미)
};                                                  // 100B

static_assert(sizeof(FParticleBeamPayload) == 100,
    "FParticleBeamPayload must be 100 bytes (4B BeamIndex + 8*12B NoiseSamples) — UParticleBeamRendererProperties::RequiredPayloadBytes() depends on this");

// Beam strip 정점 (slot 0 per-vertex, no instancing — Ribbon 와 동일 카테고리).
// 각 interpolation point 마다 strip 양쪽 (perpendicular 방향) 으로 2 vertex 생성.
// topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP.
//
// Ribbon vertex 와 동일 layout(48B) 이나 별도 struct (진단 §12 옵션 Y):
//   (a) RenderCommand.h 의 forward declaration `struct FBeamParticleVertex;` 와 alias 충돌 회피
//   (b) Cycle 13b 의 Noise sample index 등 Beam 전용 멤버 추가 여지 보존
//   (c) Ribbon 과 의도 분리 (semantic clarity)
struct FBeamParticleVertex
{
    FVector Position;       // 12 (offset 0)
    FVector Tangent;        // 12 (offset 12)
    FColor  Color;          // 16 (offset 24)
    float   TexCoordU;      // 4  (offset 40) — V 는 strip 양쪽 (0/1) 에서 SV_VertexID 로 자동 결정
    float   Size;           // 4  (offset 44) — strip 폭
};                          // 48B

static_assert(sizeof(FBeamParticleVertex) == 48,
    "FBeamParticleVertex must be tight-packed at 48 bytes — BeamParticleLayout offsets depend on this");
