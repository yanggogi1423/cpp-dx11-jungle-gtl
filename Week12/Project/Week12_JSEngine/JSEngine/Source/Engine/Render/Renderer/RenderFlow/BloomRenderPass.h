#pragma once

#include "RenderPass.h"
#include "Render/Renderer/RenderTarget/RenderTarget.h"
#include "Render/Resource/Buffer.h"

class FBloomRenderPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release() override;

protected:
    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
    bool ShouldApply(const FRenderPassContext* Context) const;
    bool EnsureResources(ID3D11Device* Device, uint32 Width, uint32 Height);
    bool EnsureConstantBuffers(ID3D11Device* Device);
    void ReleaseTargets();
    void ClearComputeBindings(ID3D11DeviceContext* DeviceContext) const;

    bool DispatchThreshold(const FRenderPassContext* Context);
    ID3D11ShaderResourceView* DispatchBlur(const FRenderPassContext* Context);
    bool DispatchBlurPass(
        const FRenderPassContext* Context,
        ID3D11ShaderResourceView* InputSRV,
        ID3D11UnorderedAccessView* OutputUAV);
    bool DispatchComposite(const FRenderPassContext* Context, ID3D11ShaderResourceView* BloomSRV);

private:
    FRenderTarget BrightTarget;
    FRenderTarget BlurTarget;
    FRenderTarget CompositeTarget;

    FConstantBuffer ThresholdCB;
    FConstantBuffer BlurCB;
    FConstantBuffer CompositeCB;

    uint32 TargetWidth = 0;
    uint32 TargetHeight = 0;
    bool bConstantBuffersCreated = false;
};
