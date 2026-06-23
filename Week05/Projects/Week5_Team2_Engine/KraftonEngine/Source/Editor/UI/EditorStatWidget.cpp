#include "Editor/UI/EditorStatWidget.h"

#include "Editor/EditorEngine.h"
#include "Profiling/Stats.h"
#include "Render/Pipeline/RenderStats.h"
#include "Render/Pipeline/WorldRenderProxy.h"
#include "GameFramework/World.h"
#include "GameFramework/Level.h"
#include "Editor/Selection/PickingTypes.h"
#include "ImGui/imgui.h"

#include <algorithm>
#include <sstream>

// ────────────────────────────────────────────────────────────
// 메인 Render
// ────────────────────────────────────────────────────────────
void FEditorStatWidget::Render(float DeltaTime)
{
#if STATS
	ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(700.0f, 600.0f), ImGuiCond_Once);
	ImGui::Begin("Stat Profiler");

	if (!bPaused)
	{
		FrozenDeltaTime = DeltaTime;
	}

	float FPS = FrozenDeltaTime > 0.0f ? 1.0f / FrozenDeltaTime : 0.0f;
	ImGui::Text("FPS: %.1f  (%.2f ms)", FPS, FrozenDeltaTime * 1000.0f);
	ImGui::Separator();

	// Pause / Resume 버튼
	if (bPaused)
	{
		if (ImGui::Button("Resume"))
		{
			bPaused = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Copy"))
		{
			std::ostringstream oss;
			auto FormatTable = [&](const char* Title, const TArray<FStatEntry>& Entries)
			{
				oss << "=== " << Title << " ===\n";
				oss << "Name\tMax(ms)\tMin(ms)\tLast(ms)\n";
				for (const FStatEntry& E : Entries)
				{
					double MinVal = E.MinTime == DBL_MAX ? 0.0 : E.MinTime;
					oss << E.Name << "\t"
						<< E.MaxTime * 1000.0 << "\t"
						<< MinVal * 1000.0 << "\t"
						<< E.LastTime * 1000.0 << "\n";
				}
				oss << "\n";
			};
			FormatTable("CPU Stats", FrozenCPUEntries);
			ImGui::SetClipboardText(oss.str().c_str());
		}
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "PAUSED");
	}
	else
	{
		if (ImGui::Button("Pause"))
		{
			FrozenCPUEntries = FStatManager::Get().GetSnapshot();
			bPaused = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Stats"))
		{
			FStatManager::Get().ResetStats();
		}
	}

	ImGui::Separator();

	// ── Render API Stats ──
	RenderRenderStats();

	// ── Culling Stats ──
	RenderCullingStats();

	// ── Picking Detail ──
	RenderPickingDetail();

	// ── Raw CPU Stats ──
	const TArray<FStatEntry>& CPUSource = bPaused ? FrozenCPUEntries : FStatManager::Get().GetSnapshot();
	if (ImGui::CollapsingHeader("CPU Stats"))
	{
		RenderStatTable("CPUStatTable", CPUSource);
	}

	ImGui::End();
#endif
}

// ────────────────────────────────────────────────────────────
// Render API Stats: DrawCall / CB / State 카운터
// ────────────────────────────────────────────────────────────
void FEditorStatWidget::RenderRenderStats()
{
	if (!ImGui::CollapsingHeader("Render Stats"))
		return;

	const FRenderStats& S = GRenderStatsSnapshot;

	ImGui::Text("Draw Calls: %u (Indexed: %u, Vertex: %u)",
		S.DrawCalls, S.DrawIndexedCalls, S.DrawVertexCalls);
	ImGui::Text("Triangles: %u", S.TrianglesRendered);

	float MBUploaded = static_cast<float>(S.CBBytesUploaded) / (1024.0f * 1024.0f);
	ImGui::Text("CB Updates: %u (%.2f MB uploaded)", S.CBMapCount, MBUploaded);

	ImGui::Text("VB/IB Binds: %u (%u redundant)", S.MeshBinds, S.RedundantMeshBinds);
	ImGui::Text("Shader Binds: %u (%u redundant)", S.ShaderBinds, S.RedundantShaderBinds);
	ImGui::Text("SRV Changes: %u (%u redundant)", S.SRVChanges, S.RedundantSRVChanges);
}

// ────────────────────────────────────────────────────────────
// Culling Stats: Proxy/Spatial(BVH) 통계 + Culling 효율
// ────────────────────────────────────────────────────────────
void FEditorStatWidget::RenderCullingStats()
{
	if (!ImGui::CollapsingHeader("Culling Stats"))
		return;

	if (!EditorEngine) return;

	UWorld* World = EditorEngine->GetWorld();
	if (!World) return;

	FWorldProxyCullingStats Total = {};
	auto AccumulateLevel = [&Total](ULevel* Level)
	{
		if (!Level) return;
		const FWorldProxyCullingStats& S = Level->GetRenderProxy().GetLastCullingStats();
		Total.RegisteredProxyCount          += S.RegisteredProxyCount;
		Total.InsertedProxyCount            += S.InsertedProxyCount;
		Total.CandidateProxyCount           += S.CandidateProxyCount;
		Total.RenderedProxyCount            += S.RenderedProxyCount;
		Total.SpatialTotalNodes             += S.SpatialTotalNodes;
		Total.SpatialTotalItems             += S.SpatialTotalItems;
		Total.SpatialOutsideItems           += S.SpatialOutsideItems;
		Total.SpatialFrustumIntersectedNodes += S.SpatialFrustumIntersectedNodes;
		Total.SpatialFrustumCandidateItems  += S.SpatialFrustumCandidateItems;
	};
	AccumulateLevel(World->GetPersistentLevel());
	AccumulateLevel(World->GetActiveLevel());

	int32 Culled = Total.RegisteredProxyCount - Total.RenderedProxyCount;
	float Efficiency = Total.RegisteredProxyCount > 0
		? static_cast<float>(Culled) / static_cast<float>(Total.RegisteredProxyCount) : 0.0f;

	ImGui::Text("Total: %d  Rendered: %d  Culled: %d",
		Total.RegisteredProxyCount, Total.RenderedProxyCount, Culled);

	// char EffBuf[64];
	// snprintf(EffBuf, sizeof(EffBuf), "%.1f%%", Efficiency * 100.0f);
	// ImGui::ProgressBar(Efficiency, ImVec2(-1, 0), EffBuf);

	ImGui::Text("Spatial BVH: %d nodes, %d intersected",
		Total.SpatialTotalNodes, Total.SpatialFrustumIntersectedNodes);
	ImGui::Text("Spatial Items: %d total, %d outside, %d in frustum",
		Total.SpatialTotalItems, Total.SpatialOutsideItems, Total.SpatialFrustumCandidateItems);
}

// ────────────────────────────────────────────────────────────
// Picking Detail: Ray / Broad / Narrow / ID 각각
// ────────────────────────────────────────────────────────────
void FEditorStatWidget::RenderPickingDetail()
{
	if (!ImGui::CollapsingHeader("Picking Detail"))
		return;

	ImGui::SeparatorText("Algorithm Metric (Unified Core)");
	if (EditorEngine && EditorEngine->GetPickingMode() == EPickingMode::IDBuffer)
	{
		ImGui::TextUnformatted("[Pick Core] IDBuffer mode");
	}
	else
	{
		ImGui::TextUnformatted("[Pick Core] Ray mode");
	}

	if (EditorEngine)
	{
		if (UWorld* World = EditorEngine->GetWorld())
		{
			FRayBroadDebugCounters TotalCounters = {};
			auto AccumulateRayBroadCounters = [&TotalCounters](ULevel* Level)
			{
				if (!Level) return;
				const FRayBroadDebugCounters& C = Level->GetRenderProxy().GetLastRayBroadDebugCounters();
				TotalCounters.AABBTests += C.AABBTests;
				TotalCounters.AABBHits += C.AABBHits;
				TotalCounters.CandidatesEmitted += C.CandidatesEmitted;
				TotalCounters.CandidatesAfterFilter += C.CandidatesAfterFilter;
				TotalCounters.NodeVisits += C.NodeVisits;
				TotalCounters.LinearAABBTests += C.LinearAABBTests;
				TotalCounters.BVHAABBTests += C.BVHAABBTests;
			};

			AccumulateRayBroadCounters(World->GetPersistentLevel());
			AccumulateRayBroadCounters(World->GetActiveLevel());

			ImGui::Text("[Ray Broad Count] AABB Tests: %llu  Hits: %llu  NodeVisits: %llu",
				static_cast<unsigned long long>(TotalCounters.AABBTests),
				static_cast<unsigned long long>(TotalCounters.AABBHits),
				static_cast<unsigned long long>(TotalCounters.NodeVisits));
			ImGui::Text("[Ray Broad Count] Emitted: %llu  AfterFilter: %llu",
				static_cast<unsigned long long>(TotalCounters.CandidatesEmitted),
				static_cast<unsigned long long>(TotalCounters.CandidatesAfterFilter));
			ImGui::Text("[Ray Broad Count] Tests (Linear/BVH): %llu / %llu",
				static_cast<unsigned long long>(TotalCounters.LinearAABBTests),
				static_cast<unsigned long long>(TotalCounters.BVHAABBTests));
		}
	}
}

// ────────────────────────────────────────────────────────────
// Raw Stat Table (기존 구현 유지)
// ────────────────────────────────────────────────────────────
void FEditorStatWidget::RenderStatTable(const char* TableID, const TArray<FStatEntry>& Source)
{
#if STATS
	if (Source.empty())
	{
		ImGui::Text("No stats recorded.");
		return;
	}

	const char* Headers[] = { "Name", "Max(ms)", "Min(ms)", "Last(ms)" };
	constexpr int NumColumns = 4;

	if (ImGui::BeginTable(TableID, NumColumns,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable,
		ImVec2(0.0f, 400.0f)))
	{
		for (int i = 0; i < NumColumns; i++)
		{
			ImGui::TableSetupColumn(Headers[i], ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
		}
		ImGui::TableHeadersRow();

		TArray<FStatEntry> SortedSource = Source;
		if (ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs())
		{
			if (sorts_specs->SpecsCount > 0)
			{
				const ImGuiTableColumnSortSpecs* sort_spec = &sorts_specs->Specs[0];
				std::sort(SortedSource.begin(), SortedSource.end(), [sort_spec](const FStatEntry& A, const FStatEntry& B)
				{
					int delta = 0;
					if (sort_spec->ColumnIndex == 0)
					{
						delta = strcmp(A.Name, B.Name);
					}
					else if (sort_spec->ColumnIndex == 1)
					{
						delta = (A.MaxTime > B.MaxTime) ? 1 : ((A.MaxTime < B.MaxTime) ? -1 : 0);
					}
					else if (sort_spec->ColumnIndex == 2)
					{
						double MinA = A.MinTime == DBL_MAX ? 0.0 : A.MinTime;
						double MinB = B.MinTime == DBL_MAX ? 0.0 : B.MinTime;
						delta = (MinA > MinB) ? 1 : ((MinA < MinB) ? -1 : 0);
					}
					else if (sort_spec->ColumnIndex == 3)
					{
						delta = (A.LastTime > B.LastTime) ? 1 : ((A.LastTime < B.LastTime) ? -1 : 0);
					}

					if (delta > 0)
						return sort_spec->SortDirection == ImGuiSortDirection_Descending;
					if (delta < 0)
						return sort_spec->SortDirection == ImGuiSortDirection_Ascending;
					return strcmp(A.Name, B.Name) < 0;
				});
			}
		}

		for (const FStatEntry& E : SortedSource)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::Text("%s", E.Name);
			ImGui::TableSetColumnIndex(1); ImGui::Text("%.8f", E.MaxTime * 1000.0);
			ImGui::TableSetColumnIndex(2); ImGui::Text("%.8f", E.MinTime == DBL_MAX ? 0.0 : E.MinTime * 1000.0);
			ImGui::TableSetColumnIndex(3); ImGui::Text("%.8f", E.LastTime * 1000.0);
		}

		ImGui::EndTable();
	}
#endif
}
