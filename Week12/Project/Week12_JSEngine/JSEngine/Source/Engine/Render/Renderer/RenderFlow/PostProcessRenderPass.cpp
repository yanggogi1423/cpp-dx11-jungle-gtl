#include "PostProcessRenderPass.h"
#include "Core/ResourceManager.h"
#include "Render/Resource/ShaderPaths.h"

namespace
{
    FShaderProgram* GetPostProcessProgram()
    {
        FShaderStageKey VSKey;
        VSKey.FilePath = FShaderPaths::PostProcessMain;
        VSKey.EntryPoint = "mainVS";

        FShaderStageKey PSKey;
        PSKey.FilePath = FShaderPaths::PostProcessMain;
        PSKey.EntryPoint = "mainPS";

        return FResourceManager::Get().GetOrCreateShaderProgram(VSKey, PSKey);
    }
}

bool FPostProcessRenderPass::Initialize()
{
    return true;
}

bool FPostProcessRenderPass::Release()
{
    return true;
}

bool FPostProcessRenderPass::Begin(const FRenderPassContext* Context)
{
    const FShowFlags& ShowFlags = Context->RenderBus->GetShowFlags();
    const bool bToneMapping = ShowFlags.bToneMapping;
    const bool bGammaCorrection = Context->RenderBus->GetShowFlags().bGammaCorrection;
    const bool bVignetteEnabled = Context->RenderBus->GetVignetteIntensity() > 0.001f;

	if (!bToneMapping && !bGammaCorrection && !bVignetteEnabled)
	{
        OutSRV = PrevPassSRV;
        OutRTV = PrevPassRTV;
        return true;
	}

    OutSRV = Context->RenderTargets->ScenePostProcessSRV;
    OutRTV = Context->RenderTargets->ScenePostProcessRTV;

	ID3D11RenderTargetView* RTVs[1] = { Context->RenderTargets->ScenePostProcessRTV };
    Context->DeviceContext->OMSetRenderTargets(1, RTVs, nullptr);

    // 이전 패스 결과 입력
    ID3D11ShaderResourceView* srvs[] = { PrevPassSRV };
    Context->DeviceContext->PSSetShaderResources(0, 1, srvs);

	FPostProcessConstants cb = {};
    cb.InvResolution[0] = 1.0f / Context->RenderTargets->Width;
    cb.InvResolution[1] = 1.0f / Context->RenderTargets->Height;
    cb.VignetteIntensity = Context->RenderBus->GetVignetteIntensity();
    cb.VignetteRadius = Context->RenderBus->GetVignetteRadius();
    cb.VignetteSmoothness = Context->RenderBus->GetVignetteSmoothness();
    cb.GammaCorrectionEnabled = bGammaCorrection ? 1u : 0u;
    cb.GammaValue = ShowFlags.GammaValue;
    cb.ToneMappingEnabled = bToneMapping ? 1u : 0u;
    cb.ToneMappingMode = static_cast<uint32>(ShowFlags.ToneMappingMode);
    cb.Exposure = ShowFlags.Exposure;
    cb.HableWhitePoint = ShowFlags.HableWhitePoint;
    const FColor& VignetteColor = Context->RenderBus->GetVignetteColor();
    cb.VignetteColor[0] = VignetteColor.R;
    cb.VignetteColor[1] = VignetteColor.G;
    cb.VignetteColor[2] = VignetteColor.B;
    cb.VignetteColor[3] = VignetteColor.A;

	Context->RenderResources->PostProcessCB.Update(
							Context->DeviceContext,
							&cb,
							sizeof(cb));

    ID3D11Buffer* buffer = Context->RenderResources->PostProcessCB.GetBuffer();
    Context->DeviceContext->PSSetConstantBuffers(11, 1, &buffer);

    // Shader 바인딩
    FShaderProgram* Program = GetPostProcessProgram();
    if (!Program)
    {
        return false;
    }
    Program->Bind(Context->DeviceContext);

    // Fullscreen triangle
    Context->DeviceContext->IASetInputLayout(nullptr);
    Context->DeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    Context->DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    return true;
}

bool FPostProcessRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    if (!Context->RenderBus->GetShowFlags().bToneMapping &&
        !Context->RenderBus->GetShowFlags().bGammaCorrection &&
        Context->RenderBus->GetVignetteIntensity() <= 0.001f)
        return true;
    Context->DeviceContext->Draw(3, 0);
    return true;
}

bool FPostProcessRenderPass::End(const FRenderPassContext* Context)
{
    if (!Context->RenderBus->GetShowFlags().bToneMapping &&
        !Context->RenderBus->GetShowFlags().bGammaCorrection &&
        Context->RenderBus->GetVignetteIntensity() <= 0.001f)
        return true;
    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    Context->DeviceContext->PSSetShaderResources(0, 1, nullSRVs);
    return true;
}
