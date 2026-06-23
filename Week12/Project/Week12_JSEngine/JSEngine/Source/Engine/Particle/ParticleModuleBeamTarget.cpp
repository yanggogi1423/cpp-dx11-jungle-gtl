#include "Particle/ParticleModuleBeamTarget.h"

// Spawn/Update 동작 없음 — 본 모듈은 단순 데이터 컨테이너 (Target Component 보유).
// Beam instance 가 Tick 시 LOD->GetModules() 순회로 본 모듈을 찾고 GetTargetComponent() 호출.
