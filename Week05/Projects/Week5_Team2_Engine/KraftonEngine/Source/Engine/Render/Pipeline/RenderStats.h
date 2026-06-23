#pragma once

#include "Core/CoreTypes.h"

// ────────────────────────────────────────────────────────────
// FRenderStats — 매 프레임 렌더 API 호출 카운터
// FRenderer가 멤버로 보유. BeginFrame()에서 Reset().
// ────────────────────────────────────────────────────────────
struct FRenderStats
{
	// DrawCall
	uint32 DrawCalls         = 0;
	uint32 DrawIndexedCalls  = 0;
	uint32 DrawVertexCalls   = 0;
	uint32 TrianglesRendered = 0;

	// Constant Buffer
	uint32 CBMapCount        = 0;
	uint64 CBBytesUploaded   = 0;

	// State Changes
	uint32 ShaderBinds       = 0;
	uint32 MeshBinds         = 0;
	uint32 SRVChanges        = 0;

	// Redundancy detection
	uint32 RedundantShaderBinds = 0;
	uint32 RedundantMeshBinds   = 0;
	uint32 RedundantSRVChanges  = 0;

	void Reset() { *this = {}; }
};

// 전역 접근용 — FRenderer::BeginFrame()에서 Reset, 각 호출부에서 ++
// Renderer 외부(Buffer.cpp 등)에서도 카운트 가능하도록 전역으로 노출
extern FRenderStats GRenderStats;

// 직전 프레임의 스냅샷 — BeginFrame()의 Reset 직전에 복사됨
// ImGui 등 UI에서는 이 값을 읽어야 함
extern FRenderStats GRenderStatsSnapshot;
