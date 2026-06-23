#include "ViewContext.h"
#include "Viewport/Viewport.h"
#include "Render/Pipeline/RenderSorting.h"
#include <algorithm>

void FViewContext::Clear()
{
	for (uint32 i = 0; i < (uint32)ERenderPass::MAX; ++i)
	{
		PassQueues[i].Clear();
		PassSectionOffsets[i] = 0;
	}

	GlobalSectionDraws.clear();

	FontEntries.clear();
	OverlayFontEntries.clear();
	SubUVEntries.clear();
	AABBEntries.clear();
	GridProxies.clear();
	CandidateProxies.clear();

	ViewportRTV = nullptr;
	ViewportDSV = nullptr;
	ViewportDepthSRV = nullptr;
	ViewportStencilSRV = nullptr;
}

void FViewContext::Reset()
{
	Clear();
	for (uint32 i = 0; i < (uint32)ERenderPass::MAX; ++i)
	{
		PassQueues[i].Reset();
	}
	TArray<FMeshSectionDraw>().swap(GlobalSectionDraws);

	TArray<FFontEntry>().swap(FontEntries);
	TArray<FFontEntry>().swap(OverlayFontEntries);
	TArray<FSubUVEntry>().swap(SubUVEntries);
	TArray<FAABBEntry>().swap(AABBEntries);
	TArray<FGridProxy>().swap(GridProxies);
	TArray<FPrimitiveProxy*>().swap(CandidateProxies);
}

void FViewContext::AddCommand(ERenderPass Pass, const FRenderCommand& InCommand)
{
	FPassQueueSoA& Queue = PassQueues[(uint32)Pass];

	// 첫 명령어가 추가될 때 해당 패스의 섹션 시작 오프셋 기록
	if (Queue.MeshBuffers.empty())
	{
		PassSectionOffsets[(uint32)Pass] = (uint32)GlobalSectionDraws.size();
	}

	Queue.MeshBuffers.push_back(InCommand.MeshBuffer);
	Queue.Shaders.push_back(InCommand.Shader);
	Queue.FirstSRVs.push_back(InCommand.SectionDraws.empty() ? nullptr : InCommand.SectionDraws[0].DiffuseSRV);
	Queue.SpriteSRVs.push_back(InCommand.SpriteSRV);
	Queue.PickingIds.push_back(InCommand.PickingId);
	Queue.Constants.push_back(InCommand.PerObjectConstants);
	Queue.ExtraCBs.push_back(InCommand.ExtraCB);

	// 섹션 정보 저장 (GlobalSectionDraws 내의 오프셋)
	Queue.SectionStart.push_back((uint32)GlobalSectionDraws.size());
	Queue.SectionCount.push_back((uint32)InCommand.SectionDraws.size());

	for (const auto& Section : InCommand.SectionDraws)
	{
		GlobalSectionDraws.push_back(Section);
	}

	// 정렬 인덱스 초기화
	Queue.SortedIndices.push_back((uint32)Queue.SortedIndices.size());
}

void FViewContext::AddCommand(ERenderPass Pass, FRenderCommand&& InCommand)
{
	// move 버전도 일단 동일하게 처리 (성능 최적화 여지 있음)
	AddCommand(Pass, static_cast<const FRenderCommand&>(InCommand));
}

void FViewContext::SortPass(ERenderPass Pass)
{
	FRenderSorting::SortIndices(PassQueues[(uint32)Pass]);
}

void FViewContext::AddFontEntry(FFontEntry&& Entry)
{
	FontEntries.push_back(std::move(Entry));
}

void FViewContext::AddOverlayFontEntry(FFontEntry&& Entry)
{
	OverlayFontEntries.push_back(std::move(Entry));
}

void FViewContext::AddSubUVEntry(FSubUVEntry&& Entry)
{
	SubUVEntries.push_back(std::move(Entry));
}

void FViewContext::AddAABBEntry(FAABBEntry&& Entry)
{
	AABBEntries.push_back(std::move(Entry));
}

void FViewContext::AddGridProxy(FGridProxy&& Proxy)
{
	GridProxies.push_back(std::move(Proxy));
}

void FViewContext::AddCandidateProxy(FPrimitiveProxy* Proxy)
{
	CandidateProxies.push_back(Proxy);
}

void FViewContext::AddCandidateProxyUnique(FPrimitiveProxy* Proxy)
{
	if (!Proxy)
	{
		return;
	}

	if (std::find(CandidateProxies.begin(), CandidateProxies.end(), Proxy) == CandidateProxies.end())
	{
		CandidateProxies.push_back(Proxy);
	}
}

void FViewContext::SetCameraInfo(
	const FMatrix& InView,
	const FMatrix& InProj,
	const FVector& InCameraForward,
	const FVector& InCameraRight,
	const FVector& InCameraUp,
	bool bInIsOrtho,
	float InOrthoWidth)
{
	View = InView;
	Proj = InProj;
	CameraForward = InCameraForward;
	CameraRight = InCameraRight;
	CameraUp = InCameraUp;
	bIsOrtho = bInIsOrtho;
	OrthoWidth = InOrthoWidth;
}

void FViewContext::SetViewportInfo(const FViewport* VP)
{
	Viewport = VP;
	viewportWidth = static_cast<float>(VP->GetWidth());
	viewportHeight = static_cast<float>(VP->GetHeight());
	ViewportRTV = VP->GetRTV();
	ViewportDSV = VP->GetDSV();
	ViewportDepthSRV = VP->GetDepthSRV();
	ViewportStencilSRV = VP->GetStencilSRV();
}

void FViewContext::SetRenderOptions(const FViewportRenderOptions& InOptions)
{
	Options = InOptions;
}

void FViewContext::SetViewportSize(float InWidth, float InHeight)
{
	viewportWidth = InWidth;
	viewportHeight = InHeight;
}

void FViewContext::CollectViewElements()
{
	if (Options.ShowFlags.bGrid)
	{
		FGridProxy Proxy;
		Proxy.Grid.GridSpacing = Options.GridSpacing;
		Proxy.Grid.GridHalfLineCount = Options.GridHalfLineCount;
		AddGridProxy(std::move(Proxy));
	}
}
