#include "BloomRenderPass.h"

#include "Core/ResourceManager.h"
#include "Render/Common/ViewTypes.h"
#include "Render/Resource/ComputeShader.h"
#include "Render/Renderer/RenderTarget/RenderTargetBuilder.h"

#include <algorithm>

struct FBloomThresholdConstants
{
    float Threshold;
    float Knee;
    float Pad[2];
};

struct FBloomBlurConstants
{
    float TexelSize[2];
    float Pad[2];
};

struct FBloomCompositeConstants
{
    float BloomIntensity;
    float Pad[3];
};

bool FBloomRenderPass::Initialize()
{
    return true;
}

bool FBloomRenderPass::Release()
{
    ReleaseTargets();
    ThresholdCB.Release();
    BlurCB.Release();
    CompositeCB.Release();
    bConstantBuffersCreated = false;
    return true;
}

bool FBloomRenderPass::Begin(const FRenderPassContext* Context)
{
    OutSRV = PrevPassSRV;
    OutRTV = PrevPassRTV;

    if (!ShouldApply(Context))
    {
        return true;
    }

    const uint32 Width = static_cast<uint32>(Context->RenderTargets->Width);
    const uint32 Height = static_cast<uint32>(Context->RenderTargets->Height);
    if (!EnsureResources(Context->Device, Width, Height))
    {
        return false;
    }

    Context->DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

    OutSRV = CompositeTarget.SRV.Get();
    OutRTV = nullptr;
    return true;
}

bool FBloomRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    if (!ShouldApply(Context))
    {
        return true;
    }

    if (!DispatchThreshold(Context))
    {
        return false;
    }

    ID3D11ShaderResourceView* BloomSRV = DispatchBlur(Context);
    if (!BloomSRV)
    {
        return false;
    }

    return DispatchComposite(Context, BloomSRV);
}

bool FBloomRenderPass::End(const FRenderPassContext* Context)
{
    if (Context && Context->DeviceContext)
    {
        ClearComputeBindings(Context->DeviceContext);
    }
    return true;
}

bool FBloomRenderPass::ShouldApply(const FRenderPassContext* Context) const
{
    if (!Context || !Context->RenderBus || !Context->RenderTargets || !Context->Device || !Context->DeviceContext)
    {
        return false;
    }

    const FShowFlags& ShowFlags = Context->RenderBus->GetShowFlags();
    return ShowFlags.bBloom && ShowFlags.BloomIntensity > 0.0f && PrevPassSRV != nullptr;
}

bool FBloomRenderPass::EnsureResources(ID3D11Device* Device, uint32 Width, uint32 Height)
{
    if (!Device || Width == 0 || Height == 0)
    {
        return false;
    }

    if (TargetWidth == Width &&
        TargetHeight == Height &&
        BrightTarget.SRV &&
        BrightTarget.UAV &&
        BlurTarget.SRV &&
        BlurTarget.UAV &&
        CompositeTarget.SRV &&
        CompositeTarget.UAV &&
        bConstantBuffersCreated)
    {
        return true;
    }

    ReleaseTargets();

    BrightTarget = FRenderTargetBuilder()
        .SetSize(Width, Height)
        .SetFormat(DXGI_FORMAT_R16G16B16A16_FLOAT)
        .WithSRV()
        .WithUAV()
        .Build(Device);

    BlurTarget = FRenderTargetBuilder()
        .SetSize(Width, Height)
        .SetFormat(DXGI_FORMAT_R16G16B16A16_FLOAT)
        .WithSRV()
        .WithUAV()
        .Build(Device);

    CompositeTarget = FRenderTargetBuilder()
        .SetSize(Width, Height)
        .SetFormat(DXGI_FORMAT_R16G16B16A16_FLOAT)
        .WithSRV()
        .WithUAV()
        .Build(Device);

    TargetWidth = Width;
    TargetHeight = Height;

    return BrightTarget.SRV &&
        BrightTarget.UAV &&
        BlurTarget.SRV &&
        BlurTarget.UAV &&
        CompositeTarget.SRV &&
        CompositeTarget.UAV &&
        EnsureConstantBuffers(Device);
}

bool FBloomRenderPass::EnsureConstantBuffers(ID3D11Device* Device)
{
    if (bConstantBuffersCreated)
    {
        return true;
    }

    ThresholdCB.Create(Device, sizeof(FBloomThresholdConstants));
    BlurCB.Create(Device, sizeof(FBloomBlurConstants));
    CompositeCB.Create(Device, sizeof(FBloomCompositeConstants));
    bConstantBuffersCreated = true;
    return true;
}

void FBloomRenderPass::ReleaseTargets()
{
    BrightTarget = FRenderTarget{};
    BlurTarget = FRenderTarget{};
    CompositeTarget = FRenderTarget{};
    TargetWidth = 0;
    TargetHeight = 0;
}

void FBloomRenderPass::ClearComputeBindings(ID3D11DeviceContext* DeviceContext) const
{
    ID3D11ShaderResourceView* NullSRVs[2] = { nullptr, nullptr };
    ID3D11UnorderedAccessView* NullUAV = nullptr;
    ID3D11Buffer* NullCB = nullptr;

    DeviceContext->CSSetShaderResources(0, 2, NullSRVs);
    DeviceContext->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
    DeviceContext->CSSetConstantBuffers(0, 1, &NullCB);
    DeviceContext->CSSetShader(nullptr, nullptr, 0);
}

bool FBloomRenderPass::DispatchThreshold(const FRenderPassContext* Context)
{
    FComputeShader* ThresholdCS = FResourceManager::Get().GetComputeShader("BloomThresholdCS");
    if (!ThresholdCS)
    {
        return false;
    }

    const FShowFlags& ShowFlags = Context->RenderBus->GetShowFlags();
    FBloomThresholdConstants Constants = {};
    Constants.Threshold = std::max(0.0f, ShowFlags.BloomThreshold);
    Constants.Knee = std::max(0.0f, ShowFlags.BloomKnee);
    ThresholdCB.Update(Context->DeviceContext, &Constants, sizeof(Constants));

    ID3D11ShaderResourceView* SRV = PrevPassSRV;
    ID3D11UnorderedAccessView* UAV = BrightTarget.UAV.Get();
    ID3D11Buffer* CB = ThresholdCB.GetBuffer();

    Context->DeviceContext->CSSetShaderResources(0, 1, &SRV);
    Context->DeviceContext->CSSetUnorderedAccessViews(0, 1, &UAV, nullptr);
    Context->DeviceContext->CSSetConstantBuffers(0, 1, &CB);
    ThresholdCS->Bind(Context->DeviceContext);
    ThresholdCS->Dispatch(Context->DeviceContext, (TargetWidth + 7) / 8, (TargetHeight + 7) / 8, 1);
    ClearComputeBindings(Context->DeviceContext);
    return true;
}

ID3D11ShaderResourceView* FBloomRenderPass::DispatchBlur(const FRenderPassContext* Context)
{
    const FShowFlags& ShowFlags = Context->RenderBus->GetShowFlags();
    const int32 Iterations = std::clamp<int32>(ShowFlags.BloomBlurIterations, 0, 8);

    ID3D11ShaderResourceView* CurrentSRV = BrightTarget.SRV.Get();
    if (Iterations == 0)
    {
        return CurrentSRV;
    }

    for (int32 Index = 0; Index < Iterations; ++Index)
    {
        const bool bWriteBlurTarget = (Index % 2) == 0;
        ID3D11UnorderedAccessView* OutputUAV = bWriteBlurTarget ? BlurTarget.UAV.Get() : BrightTarget.UAV.Get();
        if (!DispatchBlurPass(Context, CurrentSRV, OutputUAV))
        {
            return nullptr;
        }
        CurrentSRV = bWriteBlurTarget ? BlurTarget.SRV.Get() : BrightTarget.SRV.Get();
    }

    return CurrentSRV;
}

bool FBloomRenderPass::DispatchBlurPass(
    const FRenderPassContext* Context,
    ID3D11ShaderResourceView* InputSRV,
    ID3D11UnorderedAccessView* OutputUAV)
{
    FComputeShader* BlurCS = FResourceManager::Get().GetComputeShader("BloomBlurCS");
    if (!BlurCS)
    {
        return false;
    }

    FBloomBlurConstants Constants = {};
    Constants.TexelSize[0] = TargetWidth > 0 ? 1.0f / static_cast<float>(TargetWidth) : 0.0f;
    Constants.TexelSize[1] = TargetHeight > 0 ? 1.0f / static_cast<float>(TargetHeight) : 0.0f;
    BlurCB.Update(Context->DeviceContext, &Constants, sizeof(Constants));

    ID3D11Buffer* CB = BlurCB.GetBuffer();
    Context->DeviceContext->CSSetShaderResources(0, 1, &InputSRV);
    Context->DeviceContext->CSSetUnorderedAccessViews(0, 1, &OutputUAV, nullptr);
    Context->DeviceContext->CSSetConstantBuffers(0, 1, &CB);
    BlurCS->Bind(Context->DeviceContext);
    BlurCS->Dispatch(Context->DeviceContext, (TargetWidth + 7) / 8, (TargetHeight + 7) / 8, 1);
    ClearComputeBindings(Context->DeviceContext);
    return true;
}

bool FBloomRenderPass::DispatchComposite(const FRenderPassContext* Context, ID3D11ShaderResourceView* BloomSRV)
{
    FComputeShader* CompositeCS = FResourceManager::Get().GetComputeShader("BloomCompositeCS");
    if (!CompositeCS)
    {
        return false;
    }

    FBloomCompositeConstants Constants = {};
    Constants.BloomIntensity = std::max(0.0f, Context->RenderBus->GetShowFlags().BloomIntensity);
    CompositeCB.Update(Context->DeviceContext, &Constants, sizeof(Constants));

    ID3D11ShaderResourceView* SRVs[2] = { PrevPassSRV, BloomSRV };
    ID3D11UnorderedAccessView* UAV = CompositeTarget.UAV.Get();
    ID3D11Buffer* CB = CompositeCB.GetBuffer();

    Context->DeviceContext->CSSetShaderResources(0, 2, SRVs);
    Context->DeviceContext->CSSetUnorderedAccessViews(0, 1, &UAV, nullptr);
    Context->DeviceContext->CSSetConstantBuffers(0, 1, &CB);
    CompositeCS->Bind(Context->DeviceContext);
    CompositeCS->Dispatch(Context->DeviceContext, (TargetWidth + 7) / 8, (TargetHeight + 7) / 8, 1);
    ClearComputeBindings(Context->DeviceContext);
    return true;
}
