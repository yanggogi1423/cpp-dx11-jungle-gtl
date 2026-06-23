#include "TranslucentRenderPass.h"
#include "Particle/ParticleDynamicData.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/ShaderHelper.h"
#include "Render/Resource/VertexFactoryTypes.h"
#include "Render/Renderer/RenderFlow/ParticleRenderPass.h"
#include "Core/ResourceManager.h"

#include <algorithm>

namespace
{
    struct FTransparentCommandRef
    {
        const FRenderCommand* Command = nullptr;
        bool bParticle = false;
    };

    FVector GetTranslucentSortCenter(const FRenderCommand& Cmd)
    {
        if (Cmd.WorldAABB.IsValid())
        {
            return Cmd.WorldAABB.GetCenter();
        }

        return Cmd.PerObjectConstants.Model.GetOrigin();
    }

    bool IsParticleCommand(const FRenderCommand& Cmd)
    {
        switch (Cmd.VertexFactoryType)
        {
        case EVertexFactoryType::SpriteParticle:
        case EVertexFactoryType::MeshParticle:
        case EVertexFactoryType::RibbonParticle:
        case EVertexFactoryType::BeamParticle:
            return true;
        default:
            return false;
        }
    }

    TArray<FTransparentCommandRef> BuildSortedTransparentCommands(const FRenderPassContext* Context)
    {
        TArray<FTransparentCommandRef> SortedCommands;
        const TArray<FRenderCommand>& MeshCommands = Context->RenderBus->GetCommands(ERenderPass::Translucent);
        const TArray<FRenderCommand>& ParticleCommands = Context->RenderBus->GetCommands(ERenderPass::Particle);

        SortedCommands.reserve(MeshCommands.size() + ParticleCommands.size());
        for (const FRenderCommand& Cmd : MeshCommands)
        {
            SortedCommands.push_back(FTransparentCommandRef{ &Cmd, false });
        }
        for (const FRenderCommand& Cmd : ParticleCommands)
        {
            if (IsParticleCommand(Cmd))
            {
                SortedCommands.push_back(FTransparentCommandRef{ &Cmd, true });
            }
        }

        const FVector CameraPosition = Context->RenderBus->GetCameraPosition();

        std::sort(
            SortedCommands.begin(),
            SortedCommands.end(),
            [&CameraPosition](const FTransparentCommandRef& A, const FTransparentCommandRef& B)
            {
                const float ADistance = FVector::DistSquared(GetTranslucentSortCenter(*A.Command), CameraPosition);
                const float BDistance = FVector::DistSquared(GetTranslucentSortCenter(*B.Command), CameraPosition);
                return ADistance > BDistance;
            });

        return SortedCommands;
    }

    void ApplyTranslucentPassRenderState(ID3D11DeviceContext* DeviceContext)
    {
        ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(EBlendType::AlphaBlend);
        DeviceContext->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);

        ID3D11DepthStencilState* DepthState =
            FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::DepthReadOnly);
        DeviceContext->OMSetDepthStencilState(DepthState, 0);
    }

    uint32 BuildTranslucentPermutationKey(const FRenderPassContext* Context, const UMaterialInterface* Material)
    {
        uint32 PermutationKey = (uint32)ELightingModel::Unlit;
        switch (Context->RenderBus->GetViewMode())
        {
        case EViewMode::Lit_Gouraud:
            PermutationKey = (uint32)ELightingModel::Gouraud;
            break;
        case EViewMode::Lit_Lambert:
            PermutationKey = (uint32)ELightingModel::Lambert;
            break;
        case EViewMode::Lit_BlinnPhong:
            PermutationKey = (uint32)ELightingModel::BlinnPhong;
            break;
        case EViewMode::Heatmap:
            PermutationKey = (uint32)ELightingModel::Heatmap;
            break;
        }

        if (Material)
        {
            if (Material->HasDiffuseMap()) PermutationKey |= (uint32)EShaderFeature::HasDiffuseMap;
            if (Material->HasNormalMap()) PermutationKey |= (uint32)EShaderFeature::HasNormalMap;
            if (Material->HasSpecularMap()) PermutationKey |= (uint32)EShaderFeature::HasSpecularMap;
            if (Material->HasEmissiveMap()) PermutationKey |= (uint32)EShaderFeature::HasEmissiveMap;
            if (Material->HasAlphaMask()) PermutationKey |= (uint32)EShaderFeature::HasAlphaMask;
        }

        return PermutationKey;
    }

    FShaderProgram* GetTranslucentShaderProgram(const FRenderCommand& Cmd, uint32 PermutationKey)
    {
        if (!Cmd.Material)
        {
            return nullptr;
        }

        const FVertexFactoryDesc& VertexFactoryDesc = FVertexFactoryRegistry::Get(Cmd.VertexFactoryType);

        FShaderStageKey VSKey;
        VSKey.FilePath = VertexFactoryDesc.VertexShaderPath;
        VSKey.EntryPoint = VertexFactoryDesc.BasePassVSEntry;
        VSKey.Target = "vs_5_0";
        VSKey.PermutationKey = PermutationKey;

        FShaderStageKey PSKey;
        PSKey.FilePath = Cmd.Material->GetPixelShaderPath();
        PSKey.EntryPoint = Cmd.Material->GetPixelShaderEntryPoint();
        PSKey.Target = "ps_5_0";
        PSKey.PermutationKey = PermutationKey;

        TArray<D3D_SHADER_MACRO> Macros = FShaderHelper::BuildUberLitMacros(PermutationKey);
        return FResourceManager::Get().GetOrCreateShaderProgram(
            VSKey,
            PSKey,
            Macros.data(),
            Macros.data(),
            &VertexFactoryDesc.VertexLayout);
    }

    bool DrawTranslucentMeshCommand(const FRenderPassContext* Context, const FRenderCommand& Cmd)
    {
        Context->RenderResources->PerObjectConstantBuffer.Update(
            Context->DeviceContext,
            &Cmd.PerObjectConstants,
            sizeof(FPerObjectConstants));
        ID3D11Buffer* cb1 = Context->RenderResources->PerObjectConstantBuffer.GetBuffer();
        Context->DeviceContext->VSSetConstantBuffers(1, 1, &cb1);
        Context->DeviceContext->PSSetConstantBuffers(1, 1, &cb1);

        if (Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
        {
            return true;
        }

        uint32 offset = 0;
        ID3D11Buffer* vertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
        if (vertexBuffer == nullptr)
        {
            return true;
        }

        uint32 vertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
        uint32 stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
        if (vertexCount == 0 || stride == 0)
        {
            return true;
        }

        if (Cmd.Material != nullptr)
        {
            const uint32 PermutationKey = BuildTranslucentPermutationKey(Context, Cmd.Material);
            FShaderProgram* Program = GetTranslucentShaderProgram(Cmd, PermutationKey);
            if (!Program)
            {
                return true;
            }

            Program->Bind(Context->DeviceContext);
            Cmd.Material->BindRenderStates(Context->DeviceContext);
            ApplyTranslucentPassRenderState(Context->DeviceContext);
            Cmd.Material->BindParameters(Context->DeviceContext, Program->PS);
            BindVertexFactoryResources(
                Context->DeviceContext,
                Cmd.VertexFactoryType,
                Context->RenderBus->GetBoneMatrixConstants(Cmd),
                Context->RenderResources,
                Cmd.BoneMatrixConstantBuffer);
        }

        Context->DeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

        ID3D11Buffer* indexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
        if (indexBuffer != nullptr)
        {
            Context->DeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            Context->DeviceContext->DrawIndexed(Cmd.SectionIndexCount, Cmd.SectionIndexStart, 0);
        }
        else
        {
            Context->DeviceContext->Draw(vertexCount, 0);
        }

        return true;
    }

    void ReleaseParticleDynamicData(const FRenderPassContext* Context)
    {
        const TArray<FRenderCommand>& ParticleCommands = Context->RenderBus->GetCommands(ERenderPass::Particle);
        for (const FRenderCommand& Cmd : ParticleCommands)
        {
            if (Cmd.DynamicData)
            {
                delete Cmd.DynamicData;
                const_cast<FRenderCommand&>(Cmd).DynamicData = nullptr;
            }
        }
    }

    bool DrawTransparentCommands(const FRenderPassContext* Context, FParticleRenderPass* ParticleRenderPass)
    {
        const TArray<FTransparentCommandRef> SortedCommands = BuildSortedTransparentCommands(Context);
        if (SortedCommands.empty())
        {
            ReleaseParticleDynamicData(Context);
            return true;
        }

        bool bUsedParticleInstanceSlot = false;
        bool bParticleResourcesReady = false;

        if (ParticleRenderPass)
        {
            bParticleResourcesReady = ParticleRenderPass->EnsureGPUResources(Context->Device);
        }

        for (const FTransparentCommandRef& Entry : SortedCommands)
        {
            if (!Entry.Command)
            {
                continue;
            }

            if (Entry.bParticle)
            {
                if (bParticleResourcesReady)
                {
                    ParticleRenderPass->RenderParticleCommand(*Entry.Command, *Context);
                    bUsedParticleInstanceSlot = bUsedParticleInstanceSlot
                        || Entry.Command->VertexFactoryType == EVertexFactoryType::SpriteParticle
                        || Entry.Command->VertexFactoryType == EVertexFactoryType::MeshParticle;
                }
            }
            else
            {
                DrawTranslucentMeshCommand(Context, *Entry.Command);
            }
        }

        if (ParticleRenderPass)
        {
            ParticleRenderPass->EndParticleCommandBatch(Context->DeviceContext, bUsedParticleInstanceSlot);
        }
        ReleaseParticleDynamicData(Context);

        return true;
    }
}

bool FTranslucentRenderPass::Initialize()
{
    return true;
}

bool FTranslucentRenderPass::Release()
{
    return true;
}

bool FTranslucentRenderPass::Begin(const FRenderPassContext* Context)
{
    ID3D11RenderTargetView* RTV = PrevPassRTV;
    ID3D11DepthStencilView* DSV = Context->RenderTargets->DepthStencilView;
    Context->DeviceContext->OMSetRenderTargets(1, &RTV, DSV);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    OutSRV = PrevPassSRV;
    OutRTV = PrevPassRTV;

    return true;
}

bool FTranslucentRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    return DrawTransparentCommands(Context, ParticleRenderPass);
}

bool FTranslucentRenderPass::End(const FRenderPassContext* Context)
{
    return true;
}
