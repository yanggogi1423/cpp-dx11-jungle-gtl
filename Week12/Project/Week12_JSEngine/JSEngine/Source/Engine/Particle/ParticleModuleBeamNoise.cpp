#include "Particle/ParticleModuleBeamNoise.h"

// Spawn/Update 동작 없음 — 본 모듈은 단순 데이터 컨테이너 (Frequency / NoiseRange / bTargetNoise / bSmooth 보유).
// Beam instance 가 SpawnParticles 와 BuildVertexBuffer 시 LOD->GetModules() 순회로 본 모듈을 찾고 getter 호출.
