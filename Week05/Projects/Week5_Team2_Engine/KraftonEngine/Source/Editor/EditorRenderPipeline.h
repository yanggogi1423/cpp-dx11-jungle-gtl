#pragma once
#include "Render/Pipeline/IRenderPipeline.h"
#include "Render/Pipeline/ViewContext.h"

class UEditorEngine;
class FViewport;
class FLevelEditorViewportClient;

class FEditorRenderPipeline : public IRenderPipeline
{
public:
	FEditorRenderPipeline(UEditorEngine* InEditor, FRenderer& InRenderer);
	~FEditorRenderPipeline() override;

	void Execute(float DeltaTime, FRenderer& Renderer) override;
	void Reset() override;

private:
	// 단일 뷰포트 렌더 단위 — ViewportClient의 렌더 옵션을 사용
	void RenderViewport(FLevelEditorViewportClient* VC, FRenderer& Renderer);

private:
	UEditorEngine* Editor = nullptr;
	FViewContext ViewContext;
};
