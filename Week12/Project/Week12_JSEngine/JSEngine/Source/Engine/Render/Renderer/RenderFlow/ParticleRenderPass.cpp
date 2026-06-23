#include "ParticleRenderPass.h"

#include "Core/ResourceManager.h"
#include "Particle/ParticleDynamicData.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Resource/ShaderPaths.h"
#include "Render/Resource/VertexFactoryTypes.h"
#include "Render/Resource/VertexTypes.h"
#include "Render/Resource/Texture.h"
#include "Render/SubUVBatcher.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Scene/RenderCommand.h"

#include <algorithm>

// Cycle 15a (D4): Particle 4종 슬롯 + 5 보조 (UTexture*/SubUV grid/count) → 단일 DynamicData* 슬롯 통합.
// 측정 결과: 464 (Cycle 10a baseline) → 384 (Cycle 15a, -80 bytes).
// (제거 멤버 합 ~64B + 정렬 padding 영향 -16B 추가 감소 = -80B.)
static_assert(sizeof(FRenderCommand) == 384, "Cycle 15a baseline: FRenderCommand expected 384 bytes on x64 MSVC after particle slot consolidation (D4)");

namespace
{
    struct FSpriteParticleCB
    {
        uint32 SubUVColumns;
        uint32 SubUVRows;
        float  Padding[2];
    };

    FShaderProgram* GetSpriteParticleProgram()
    {
        const FVertexFactoryDesc& Desc = FVertexFactoryRegistry::Get(EVertexFactoryType::SpriteParticle);

        FShaderStageKey VSKey;
        VSKey.FilePath = Desc.VertexShaderPath;
        VSKey.EntryPoint = Desc.BasePassVSEntry;
        VSKey.Target = "vs_5_0";

        FShaderStageKey PSKey;
        PSKey.FilePath = FShaderPaths::ParticleSprite;
        PSKey.EntryPoint = "SpriteParticlePS";
        PSKey.Target = "ps_5_0";

        return FResourceManager::Get().GetOrCreateShaderProgram(
            VSKey, PSKey, nullptr, nullptr, &Desc.VertexLayout);
    }

    // Cycle 11: Mesh particle 전용 shader program.
    // SpriteParticle 패턴과 동일 — Registry의 Desc로 VS/Layout 받고 PS는 직접 지정.
    FShaderProgram* GetMeshParticleProgram()
    {
        const FVertexFactoryDesc& Desc = FVertexFactoryRegistry::Get(EVertexFactoryType::MeshParticle);

        FShaderStageKey VSKey;
        VSKey.FilePath = Desc.VertexShaderPath;
        VSKey.EntryPoint = Desc.BasePassVSEntry;
        VSKey.Target = "vs_5_0";

        FShaderStageKey PSKey;
        PSKey.FilePath = FShaderPaths::ParticleMesh;
        PSKey.EntryPoint = "MeshParticlePS";
        PSKey.Target = "ps_5_0";

        return FResourceManager::Get().GetOrCreateShaderProgram(
            VSKey, PSKey, nullptr, nullptr, &Desc.VertexLayout);
    }

    // Cycle 12: Ribbon particle 전용 shader program. slot 0 per-vertex only, no instancing.
    FShaderProgram* GetRibbonParticleProgram()
    {
        const FVertexFactoryDesc& Desc = FVertexFactoryRegistry::Get(EVertexFactoryType::RibbonParticle);

        FShaderStageKey VSKey;
        VSKey.FilePath = Desc.VertexShaderPath;
        VSKey.EntryPoint = Desc.BasePassVSEntry;
        VSKey.Target = "vs_5_0";

        FShaderStageKey PSKey;
        PSKey.FilePath = FShaderPaths::ParticleRibbon;
        PSKey.EntryPoint = "RibbonParticlePS";
        PSKey.Target = "ps_5_0";

        return FResourceManager::Get().GetOrCreateShaderProgram(
            VSKey, PSKey, nullptr, nullptr, &Desc.VertexLayout);
    }

    // Cycle 13a: Beam particle 전용 shader program. Ribbon 와 동일 카테고리 — slot 0 per-vertex only, no instancing.
    FShaderProgram* GetBeamParticleProgram()
    {
        const FVertexFactoryDesc& Desc = FVertexFactoryRegistry::Get(EVertexFactoryType::BeamParticle);

        FShaderStageKey VSKey;
        VSKey.FilePath = Desc.VertexShaderPath;
        VSKey.EntryPoint = Desc.BasePassVSEntry;
        VSKey.Target = "vs_5_0";

        FShaderStageKey PSKey;
        PSKey.FilePath = FShaderPaths::ParticleBeam;
        PSKey.EntryPoint = "BeamParticlePS";
        PSKey.Target = "ps_5_0";

        return FResourceManager::Get().GetOrCreateShaderProgram(
            VSKey, PSKey, nullptr, nullptr, &Desc.VertexLayout);
    }

    const UMaterial* ResolveBaseMaterial(const UMaterialInterface* MaterialInterface)
    {
        UMaterialInterface* MutableMaterial = const_cast<UMaterialInterface*>(MaterialInterface);
        if (const UMaterial* Material = Cast<UMaterial>(MutableMaterial))
        {
            return Material;
        }
        if (const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MutableMaterial))
        {
            return MaterialInstance->Parent;
        }
        return nullptr;
    }
}

bool FParticleRenderPass::Initialize()
{
    return true;
}

bool FParticleRenderPass::Release()
{
    QuadVertexBuffer.Release();
    QuadIndexBuffer.Release();
    InstanceBuffer.Release();
    SpriteParticleCB.Release();
    MeshInstanceBuffer.Release();
    RibbonVertexBuffer.Release();
    BeamVertexBuffer.Release();
    bGPUResourcesReady = false;
    return true;
}

bool FParticleRenderPass::EnsureGPUResources(ID3D11Device* Device)
{
    if (bGPUResourcesReady || !Device)
    {
        return bGPUResourcesReady;
    }

    // Quad: 4 vertices, 6 indices (2 triangles). XY in [-0.5, 0.5], UV [0,1] standard.
    const FSpriteParticleVertex Vertices[4] =
    {
        { FVector(-0.5f, -0.5f, 0.0f), FVector2(0.0f, 1.0f) },
        { FVector( 0.5f, -0.5f, 0.0f), FVector2(1.0f, 1.0f) },
        { FVector(-0.5f,  0.5f, 0.0f), FVector2(0.0f, 0.0f) },
        { FVector( 0.5f,  0.5f, 0.0f), FVector2(1.0f, 0.0f) },
    };
    QuadVertexBuffer.CreateRaw(Device, Vertices, 4, sizeof(FSpriteParticleVertex), false);

    TArray<uint32> Indices = { 0, 2, 1, 1, 2, 3 };
    QuadIndexBuffer.Create(Device, Indices, static_cast<uint32>(sizeof(uint32) * Indices.size()));

    InstanceBuffer.Create(Device, sizeof(FSpriteParticleInstanceData), 256);
    SpriteParticleCB.Create(Device, sizeof(FSpriteParticleCB));

    // Cycle 11: Mesh emitter용 per-instance VB. Sprite와 동일 grow-by-2x 패턴.
    MeshInstanceBuffer.Create(Device, sizeof(FMeshParticleInstanceData), 256);

    // Cycle 12: Ribbon emitter용 slot 0 dynamic VB. instancing 없음 — sizeof(FRibbonParticleVertex) stride.
    RibbonVertexBuffer.Create(Device, sizeof(FRibbonParticleVertex), 256);

    // Cycle 13a: Beam emitter용 slot 0 dynamic VB. Ribbon 와 동일 패턴 — sizeof(FBeamParticleVertex) stride.
    BeamVertexBuffer.Create(Device, sizeof(FBeamParticleVertex), 256);

    bGPUResourcesReady = QuadVertexBuffer.GetBuffer() != nullptr
        && QuadIndexBuffer.GetBuffer() != nullptr
        && InstanceBuffer.IsValid()
        && SpriteParticleCB.GetBuffer() != nullptr
        && MeshInstanceBuffer.IsValid()
        && RibbonVertexBuffer.IsValid()
        && BeamVertexBuffer.IsValid();
    return bGPUResourcesReady;
}

bool FParticleRenderPass::Begin(const FRenderPassContext* Context)
{
    ID3D11RenderTargetView* RTV = PrevPassRTV;
    ID3D11DepthStencilView* DSV = Context->RenderTargets->DepthStencilView;
    Context->DeviceContext->OMSetRenderTargets(1, &RTV, DSV);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    OutSRV = PrevPassSRV;
    OutRTV = PrevPassRTV;
    return true;
}

bool FParticleRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    if (!Context || !Context->RenderBus || !Context->DeviceContext || !Context->Device)
    {
        return true;
    }

    if (bExternalDispatch)
    {
        return true;
    }

    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::Particle);
    if (Commands.empty())
    {
        return true;
    }

    if (!EnsureGPUResources(Context->Device))
    {
        return false;
    }

    // Cycle 10a: type-agnostic dispatch. Cmd.VertexFactoryType으로 4-way switch → 각 helper.
    // Cycle 11: Mesh helper도 본문 보유. Ribbon/Beam은 Cycle 12b/13b에서 본문 채움.
    // 단일 Pass + procedural switch 구조 (사용자 결정 3).
    bool bAnySpriteRendered = false;
    bool bAnyMeshRendered = false;
    for (const FRenderCommand& Cmd : Commands)
    {
        RenderParticleCommand(Cmd, *Context);
        bAnySpriteRendered = bAnySpriteRendered || Cmd.VertexFactoryType == EVertexFactoryType::SpriteParticle;
        bAnyMeshRendered = bAnyMeshRendered || Cmd.VertexFactoryType == EVertexFactoryType::MeshParticle;
    }

    // Cycle 15a (D2 매 frame new): frame-scope life-cycle. RenderPass 가 frame 끝에 delete.
    // Bus.GetCommands 는 const ref — Cmd.DynamicData 포인터 값만 변경 (소유권 해제) 위해 const_cast.
    // 단일 스레드 + Bus 가 frame 마다 Clear 되므로 안전.
    for (const FRenderCommand& Cmd : Commands)
    {
        if (Cmd.DynamicData)
        {
            delete Cmd.DynamicData;
            const_cast<FRenderCommand&>(Cmd).DynamicData = nullptr;
        }
    }

    //TODO
    //Slot 해제는 End logic에서 담당하도록 수정하는게 가독성에 유리함. 추후 진행
    // Slot 1을 다른 패스가 자동으로 미사용한다고 가정해도 위험. instance VB는 binding 해제.
    // Sprite/Mesh Cmd가 한 번이라도 처리됐을 때만 unbind 필요 (Ribbon/Beam은 slot 1 사용 안 함).
    EndParticleCommandBatch(Context->DeviceContext, bAnySpriteRendered || bAnyMeshRendered);

    return true;
}

void FParticleRenderPass::RenderParticleCommand(const FRenderCommand& Cmd, const FRenderPassContext& Context)
{
    switch (Cmd.VertexFactoryType)
    {
    case EVertexFactoryType::SpriteParticle:
        RenderSpriteEmitter(Cmd, Context);
        break;
    case EVertexFactoryType::MeshParticle:
        RenderMeshEmitter(Cmd, Context);
        break;
    case EVertexFactoryType::RibbonParticle:
        RenderRibbonEmitter(Cmd, Context);
        break;
    case EVertexFactoryType::BeamParticle:
        RenderBeamEmitter(Cmd, Context);
        break;
    default:
        break;
    }
}

void FParticleRenderPass::EndParticleCommandBatch(ID3D11DeviceContext* DeviceContext, bool bUsedInstanceSlot)
{
    if (!DeviceContext || !bUsedInstanceSlot)
    {
        return;
    }

    ID3D11Buffer* NullBuffer = nullptr;
    UINT NullStride = 0;
    UINT NullOffset = 0;
    DeviceContext->IASetVertexBuffers(1, 1, &NullBuffer, &NullStride, &NullOffset);
}

// Function : Render single Sprite emitter command — Cycle 15a (DynamicData path)
// input : Cmd, Context. Cmd.DynamicData : FDynamicSpriteEmitterData* (Builder 가 set).
// output : One DrawIndexedInstanced (또는 SubUVBatcher path) when valid.
void FParticleRenderPass::RenderSpriteEmitter(const FRenderCommand& Cmd, const FRenderPassContext& Context)
{
    FDynamicSpriteEmitterData* SpriteData = static_cast<FDynamicSpriteEmitterData*>(Cmd.DynamicData);
    if (!SpriteData || SpriteData->SpriteInstanceDataBuffer.empty())
    {
        return;
    }
    const FDynamicSpriteEmitterReplayData& Replay = SpriteData->Source;

    if (Context.SubUVBatcher && Context.RenderBus)
    {
        Context.SubUVBatcher->Clear();

        UTexture* Texture = Replay.ParticleTexture ? Replay.ParticleTexture : FResourceManager::Get().GetTexture("DefaultWhite");
        const uint32 Columns = (Replay.SubUVColumns > 0) ? Replay.SubUVColumns : 1;
        const uint32 Rows = (Replay.SubUVRows > 0) ? Replay.SubUVRows : 1;
        const uint32 FrameCount = std::max(Columns * Rows, 1u);
        const FVector UnitScale(1.0f, 1.0f, 1.0f);

        for (const FSpriteParticleInstanceData& Particle : SpriteData->SpriteInstanceDataBuffer)
        {
            Context.SubUVBatcher->AddSprite(
                Texture,
                Particle.Position,
                Context.RenderBus->GetCameraRight(),
                Context.RenderBus->GetCameraUp(),
                UnitScale,
                Particle.SubUVIndex % FrameCount,
                Columns,
                Rows,
                Particle.Size.X * 2.0f,
                Particle.Size.Y * 2.0f,
                Particle.Color,
                Particle.Rotation);
        }

        const bool bWireframe = Context.RenderBus->GetViewMode() == EViewMode::Wireframe;
        Context.SubUVBatcher->Flush(Context.DeviceContext, bWireframe);
        return;
    }

    FShaderProgram* Program = GetSpriteParticleProgram();
    if (!Program || !Program->VS || !Program->PS)
    {
        return;
    }

    ID3D11DeviceContext* DeviceContext = Context.DeviceContext;

    Program->Bind(DeviceContext);

    const EBlendType SpriteBlendType = Replay.BlendType;
    const EDepthStencilType SpriteDepthType = (SpriteBlendType == EBlendType::Opaque)
        ? EDepthStencilType::Default
        : EDepthStencilType::DepthReadOnly;
    ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(SpriteBlendType);
    DeviceContext->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);
    ID3D11DepthStencilState* DepthState = FResourceManager::Get().GetOrCreateDepthStencilState(SpriteDepthType);
    DeviceContext->OMSetDepthStencilState(DepthState, 0);
    ID3D11RasterizerState* RasterState = FResourceManager::Get().GetOrCreateRasterizerState(ERasterizerType::SolidNoCull);
    DeviceContext->RSSetState(RasterState);
    ID3D11SamplerState* Sampler = FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear);
    DeviceContext->PSSetSamplers(0, 1, &Sampler);

    ID3D11Buffer* IndexBuffer = QuadIndexBuffer.GetBuffer();
    DeviceContext->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);

    // Cycle 15a: DynamicData->FillVertexBuffer 가 InstanceBuffer.Update 수행 (helper 는 fetch 안 함).
    SpriteData->FillVertexBuffer(Context.Device, DeviceContext, InstanceBuffer);

    if (!InstanceBuffer.IsValid() || InstanceBuffer.GetInstanceCount() == 0)
    {
        return;
    }

    ID3D11Buffer* FrameBuf = Context.RenderResources->FrameBuffer.GetBuffer();
    if (FrameBuf)
    {
        DeviceContext->VSSetConstantBuffers(0, 1, &FrameBuf);
    }

    FSpriteParticleCB CBData = {};
    CBData.SubUVColumns = (Replay.SubUVColumns > 0) ? Replay.SubUVColumns : 1;
    CBData.SubUVRows    = (Replay.SubUVRows    > 0) ? Replay.SubUVRows    : 1;
    SpriteParticleCB.Update(DeviceContext, &CBData, sizeof(FSpriteParticleCB));
    ID3D11Buffer* CBBuf = SpriteParticleCB.GetBuffer();
    DeviceContext->VSSetConstantBuffers(8, 1, &CBBuf);

    Context.RenderResources->PerObjectConstantBuffer.Update(
        DeviceContext, &Cmd.PerObjectConstants, sizeof(FPerObjectConstants));
    ID3D11Buffer* PerObjBuf = Context.RenderResources->PerObjectConstantBuffer.GetBuffer();
    DeviceContext->VSSetConstantBuffers(1, 1, &PerObjBuf);
    DeviceContext->PSSetConstantBuffers(1, 1, &PerObjBuf);

    ID3D11ShaderResourceView* TextureSRV = nullptr;
    if (Replay.ParticleTexture)
    {
        TextureSRV = Replay.ParticleTexture->GetSRV();
    }
    if (!TextureSRV)
    {
        TextureSRV = FResourceManager::Get().GetDefaultWhiteSRV();
    }
    DeviceContext->PSSetShaderResources(0, 1, &TextureSRV);

    ID3D11Buffer* VBs[2] = { QuadVertexBuffer.GetBuffer(), InstanceBuffer.GetBuffer() };
    UINT Strides[2] = { QuadVertexBuffer.GetStride(), InstanceBuffer.GetStride() };
    UINT Offsets[2] = { 0, 0 };
    DeviceContext->IASetVertexBuffers(0, 2, VBs, Strides, Offsets);

    DeviceContext->DrawIndexedInstanced(6, InstanceBuffer.GetInstanceCount(), 0, 0, 0);
}

// Function : Render single Mesh particle emitter command — Cycle 15a (DynamicData path)
// input : Cmd, Context. Cmd.DynamicData : FDynamicMeshEmitterData*. Cmd.MeshBuffer : Builder 가 set.
// output : One DrawIndexedInstanced call issued when MeshBuffer + instance data valid.
// D3D state follows material blend policy: opaque writes depth, translucent reads depth only.
void FParticleRenderPass::RenderMeshEmitter(const FRenderCommand& Cmd, const FRenderPassContext& Context)
{
    FDynamicMeshEmitterData* MeshData = static_cast<FDynamicMeshEmitterData*>(Cmd.DynamicData);
    if (!MeshData || MeshData->MeshInstanceDataBuffer.empty() || Cmd.MeshBuffer == nullptr)
    {
        return;
    }
    if (!Cmd.MeshBuffer->IsValid())
    {
        return;
    }
    const FDynamicMeshEmitterReplayData& Replay = MeshData->Source;

    FShaderProgram* Program = GetMeshParticleProgram();
    if (!Program || !Program->VS || !Program->PS)
    {
        return;
    }

    ID3D11DeviceContext* DeviceContext = Context.DeviceContext;
    Program->Bind(DeviceContext);

    const UMaterial* MeshMaterial = ResolveBaseMaterial(Cmd.Material);
    const EBlendType MeshBlendType = MeshMaterial ? MeshMaterial->BlendType : EBlendType::AlphaBlend;
    const EDepthStencilType MeshDepthType = (MeshBlendType == EBlendType::Opaque)
        ? EDepthStencilType::Default
        : EDepthStencilType::DepthReadOnly;

    // Mesh particles follow their material blend policy. Translucent materials depth-test but do not write depth,
    // matching the sprite/ribbon/beam particle pass behavior.
    ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(MeshBlendType);
    DeviceContext->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);
    ID3D11DepthStencilState* DepthState = FResourceManager::Get().GetOrCreateDepthStencilState(MeshDepthType);
    DeviceContext->OMSetDepthStencilState(DepthState, 0);
    ID3D11RasterizerState* RasterState = FResourceManager::Get().GetOrCreateRasterizerState(ERasterizerType::SolidBackCull);
    DeviceContext->RSSetState(RasterState);
    ID3D11SamplerState* Sampler = FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear);
    DeviceContext->PSSetSamplers(0, 1, &Sampler);

    ID3D11Buffer* FrameBuf = Context.RenderResources->FrameBuffer.GetBuffer();
    if (FrameBuf)
    {
        DeviceContext->VSSetConstantBuffers(0, 1, &FrameBuf);
    }

    Context.RenderResources->PerObjectConstantBuffer.Update(
        DeviceContext, &Cmd.PerObjectConstants, sizeof(FPerObjectConstants));
    ID3D11Buffer* PerObjBuf = Context.RenderResources->PerObjectConstantBuffer.GetBuffer();
    DeviceContext->VSSetConstantBuffers(1, 1, &PerObjBuf);
    DeviceContext->PSSetConstantBuffers(1, 1, &PerObjBuf);

    ID3D11ShaderResourceView* TextureSRV = nullptr;
    if (Replay.ParticleTexture)
    {
        TextureSRV = Replay.ParticleTexture->GetSRV();
    }
    if (!TextureSRV)
    {
        TextureSRV = FResourceManager::Get().GetDefaultWhiteSRV();
    }
    DeviceContext->PSSetShaderResources(0, 1, &TextureSRV);

    // Cycle 15a: DynamicData->FillVertexBuffer 가 MeshInstanceBuffer.Update 수행.
    MeshData->FillVertexBuffer(Context.Device, DeviceContext, MeshInstanceBuffer);
    if (!MeshInstanceBuffer.IsValid() || MeshInstanceBuffer.GetInstanceCount() == 0)
    {
        return;
    }

    // VB 바인딩: Slot 0 mesh per-vertex, Slot 1 per-instance.
    ID3D11Buffer* VBs[2] = {
        Cmd.MeshBuffer->GetVertexBuffer().GetBuffer(),
        MeshInstanceBuffer.GetBuffer()
    };
    UINT Strides[2] = {
        Cmd.MeshBuffer->GetVertexBuffer().GetStride(),
        MeshInstanceBuffer.GetStride()
    };
    UINT Offsets[2] = { 0, 0 };
    DeviceContext->IASetVertexBuffers(0, 2, VBs, Strides, Offsets);

    ID3D11Buffer* IndexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
    DeviceContext->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);

    DeviceContext->DrawIndexedInstanced(Cmd.SectionIndexCount, MeshInstanceBuffer.GetInstanceCount(), Cmd.SectionIndexStart, 0, 0);
}

// Function : Render single Ribbon particle emitter command (Cycle 12)
// input : Cmd, Context
// Cmd : render command produced by Builder Ribbon case (RibbonVertices + Material 세팅됨)
// Context : render pass context (Device, DeviceContext, RenderResources, ...)
// output : One Draw call issued when RibbonVertexBuffer valid (DrawIndexed 아님 — strip 은 index 불필요)
//
// D3D state: BlendAlpha + DepthReadOnly + SolidNoCull (사용자 결정 lock-in — ribbon trail 알파 마스킹).
// PerObject CB: Model 은 Identity (vertex 가 이미 world space — instance 도 없음).
// Slot 0: RibbonVertexBuffer (FRibbonParticleVertex), Slot 1: binding 없음.
// topology: TRIANGLESTRIP.
void FParticleRenderPass::RenderRibbonEmitter(const FRenderCommand& Cmd, const FRenderPassContext& Context)
{
    FDynamicRibbonEmitterData* RibbonData = static_cast<FDynamicRibbonEmitterData*>(Cmd.DynamicData);
    if (!RibbonData || RibbonData->RibbonVertexBuffer.empty())
    {
        return;
    }
    const FDynamicRibbonEmitterReplayData& Replay = RibbonData->Source;

    FShaderProgram* Program = GetRibbonParticleProgram();
    if (!Program || !Program->VS || !Program->PS)
    {
        return;
    }

    ID3D11DeviceContext* DeviceContext = Context.DeviceContext;
    Program->Bind(DeviceContext);

    const EBlendType RibbonBlendType = Replay.BlendType;
    const EDepthStencilType RibbonDepthType = (RibbonBlendType == EBlendType::Opaque)
        ? EDepthStencilType::Default
        : EDepthStencilType::DepthReadOnly;
    ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(RibbonBlendType);
    DeviceContext->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);
    ID3D11DepthStencilState* DepthState = FResourceManager::Get().GetOrCreateDepthStencilState(RibbonDepthType);
    DeviceContext->OMSetDepthStencilState(DepthState, 0);
    ID3D11RasterizerState* RasterState = FResourceManager::Get().GetOrCreateRasterizerState(ERasterizerType::SolidNoCull);
    DeviceContext->RSSetState(RasterState);
    ID3D11SamplerState* Sampler = FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear);
    DeviceContext->PSSetSamplers(0, 1, &Sampler);

    ID3D11Buffer* FrameBuf = Context.RenderResources->FrameBuffer.GetBuffer();
    if (FrameBuf)
    {
        DeviceContext->VSSetConstantBuffers(0, 1, &FrameBuf);
    }

    Context.RenderResources->PerObjectConstantBuffer.Update(
        DeviceContext, &Cmd.PerObjectConstants, sizeof(FPerObjectConstants));
    ID3D11Buffer* PerObjBuf = Context.RenderResources->PerObjectConstantBuffer.GetBuffer();
    DeviceContext->VSSetConstantBuffers(1, 1, &PerObjBuf);
    DeviceContext->PSSetConstantBuffers(1, 1, &PerObjBuf);

    ID3D11ShaderResourceView* TextureSRV = nullptr;
    if (Replay.ParticleTexture)
    {
        TextureSRV = Replay.ParticleTexture->GetSRV();
    }
    if (!TextureSRV)
    {
        TextureSRV = FResourceManager::Get().GetDefaultWhiteSRV();
    }
    DeviceContext->PSSetShaderResources(0, 1, &TextureSRV);

    // Cycle 15a: DynamicData->FillVertexBuffer 가 RibbonVertexBuffer.Update 수행.
    RibbonData->FillVertexBuffer(Context.Device, DeviceContext, RibbonVertexBuffer);
    if (!RibbonVertexBuffer.IsValid() || RibbonVertexBuffer.GetInstanceCount() == 0)
    {
        return;
    }

    ID3D11Buffer* VBs[1] = { RibbonVertexBuffer.GetBuffer() };
    UINT Strides[1] = { RibbonVertexBuffer.GetStride() };
    UINT Offsets[1] = { 0 };
    DeviceContext->IASetVertexBuffers(0, 1, VBs, Strides, Offsets);

    DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    DeviceContext->Draw(RibbonVertexBuffer.GetInstanceCount(), 0);
    DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// Function : Render single Beam particle emitter command (Cycle 13a)
// input : Cmd, Context
// Cmd : render command produced by Builder Beam case (BeamVertices + Material 세팅됨)
// Context : render pass context (Device, DeviceContext, RenderResources, ...)
// output : One Draw call issued when BeamVertexBuffer valid (indexless — strip 은 index 불필요)
//
// D3D state: BlendAlpha + DepthReadOnly + SolidNoCull (Ribbon 와 동일 — Additive 본 cycle 외).
// PerObject CB: Model 은 Identity (vertex 가 이미 world space — instance 도 없음).
// Slot 0: BeamVertexBuffer (FBeamParticleVertex), Slot 1: binding 없음.
// topology: TRIANGLESTRIP. helper 끝에서 TRIANGLELIST 복원 (다음 Sprite/Mesh helper 보호).
void FParticleRenderPass::RenderBeamEmitter(const FRenderCommand& Cmd, const FRenderPassContext& Context)
{
    FDynamicBeamEmitterData* BeamData = static_cast<FDynamicBeamEmitterData*>(Cmd.DynamicData);
    if (!BeamData || BeamData->BeamVertexBuffer.empty())
    {
        return;
    }
    const FDynamicBeamEmitterReplayData& Replay = BeamData->Source;

    FShaderProgram* Program = GetBeamParticleProgram();
    if (!Program || !Program->VS || !Program->PS)
    {
        return;
    }

    ID3D11DeviceContext* DeviceContext = Context.DeviceContext;
    Program->Bind(DeviceContext);

    const EBlendType BeamBlendType = Replay.BlendType;
    const EDepthStencilType BeamDepthType = (BeamBlendType == EBlendType::Opaque)
        ? EDepthStencilType::Default
        : EDepthStencilType::DepthReadOnly;
    ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(BeamBlendType);
    DeviceContext->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);
    ID3D11DepthStencilState* DepthState = FResourceManager::Get().GetOrCreateDepthStencilState(BeamDepthType);
    DeviceContext->OMSetDepthStencilState(DepthState, 0);
    ID3D11RasterizerState* RasterState = FResourceManager::Get().GetOrCreateRasterizerState(ERasterizerType::SolidNoCull);
    DeviceContext->RSSetState(RasterState);
    ID3D11SamplerState* Sampler = FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear);
    DeviceContext->PSSetSamplers(0, 1, &Sampler);

    ID3D11Buffer* FrameBuf = Context.RenderResources->FrameBuffer.GetBuffer();
    if (FrameBuf)
    {
        DeviceContext->VSSetConstantBuffers(0, 1, &FrameBuf);
    }

    Context.RenderResources->PerObjectConstantBuffer.Update(
        DeviceContext, &Cmd.PerObjectConstants, sizeof(FPerObjectConstants));
    ID3D11Buffer* PerObjBuf = Context.RenderResources->PerObjectConstantBuffer.GetBuffer();
    DeviceContext->VSSetConstantBuffers(1, 1, &PerObjBuf);
    DeviceContext->PSSetConstantBuffers(1, 1, &PerObjBuf);

    ID3D11ShaderResourceView* TextureSRV = nullptr;
    if (Replay.ParticleTexture)
    {
        TextureSRV = Replay.ParticleTexture->GetSRV();
    }
    if (!TextureSRV)
    {
        TextureSRV = FResourceManager::Get().GetDefaultWhiteSRV();
    }
    DeviceContext->PSSetShaderResources(0, 1, &TextureSRV);

    // Cycle 15a: DynamicData->FillVertexBuffer 가 BeamVertexBuffer.Update 수행.
    BeamData->FillVertexBuffer(Context.Device, DeviceContext, BeamVertexBuffer);
    if (!BeamVertexBuffer.IsValid() || BeamVertexBuffer.GetInstanceCount() == 0)
    {
        return;
    }

    ID3D11Buffer* VBs[1] = { BeamVertexBuffer.GetBuffer() };
    UINT Strides[1] = { BeamVertexBuffer.GetStride() };
    UINT Offsets[1] = { 0 };
    DeviceContext->IASetVertexBuffers(0, 1, VBs, Strides, Offsets);

    DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    DeviceContext->Draw(BeamVertexBuffer.GetInstanceCount(), 0);
    DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

bool FParticleRenderPass::End(const FRenderPassContext* Context)
{

    return true;
}
