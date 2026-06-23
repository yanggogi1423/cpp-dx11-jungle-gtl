#pragma once
#include "IRenderPipeline.h"
#include "Render/Pipeline/ViewContext.h"

class UEngine;

class FDefaultRenderPipeline : public IRenderPipeline
{
public:
	FDefaultRenderPipeline(UEngine* InEngine, FRenderer& InRenderer);
	~FDefaultRenderPipeline() override;

	void Execute(float DeltaTime, FRenderer& Renderer) override;
	void Reset() override;

private:
	UEngine* Engine = nullptr;
	FViewContext Bus;
};
