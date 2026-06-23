#pragma once

#include "Render/Pipeline/IRenderPipeline.h"
#include "Render/Pipeline/ViewContext.h"

class UObjViewerEngine;
class FViewport;

class FObjViewerRenderPipeline : public IRenderPipeline
{
public:
	FObjViewerRenderPipeline(UObjViewerEngine* InEngine, FRenderer& InRenderer);
	~FObjViewerRenderPipeline() override;

	void Execute(float DeltaTime, FRenderer& Renderer) override;
	void Reset() override;

private:
	void RenderPreviewViewport(FRenderer& Renderer);

private:
	UObjViewerEngine* Engine = nullptr;
	FViewContext Bus;
};
