#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FWorldAnchoredShockWavePass final : public FRenderPassBase
{
public:
	FWorldAnchoredShockWavePass();
	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
