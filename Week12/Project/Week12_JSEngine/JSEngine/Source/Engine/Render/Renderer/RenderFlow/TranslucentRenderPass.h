#pragma once
#include "RenderPass.h"

class FParticleRenderPass;

class FTranslucentRenderPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release() override;

    void SetParticleRenderPass(FParticleRenderPass* InParticleRenderPass) { ParticleRenderPass = InParticleRenderPass; }

private:
    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

    FParticleRenderPass* ParticleRenderPass = nullptr;
};
