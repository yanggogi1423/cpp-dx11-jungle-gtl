#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Undo/EditorUndoSystem.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Editor/Viewport/FSceneViewport.h"
#include "Core/ResourceManager.h"
#include "GameFramework/AActor.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "Math/Utils.h"
#include "Particle/ParticleDynamicData.h"
#include "Particle/ParticleSystemComponent.h"
#include "Particle/ParticleEvent.h"
#include "Render/Renderer/Renderer.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Render/Scene/PrimitiveDrawCommandBuilder.h"
#include "Render/Scene/RenderBus.h"

#include "ImGui/imgui.h"

#include <algorithm>
#include <cstdio>

namespace
{
constexpr float MaxDebugCameraSpeedMultiplier = 20.0f;

FString FormatHistoryBytes(size_t Bytes)
{
    char Buffer[64];
    const double Value = static_cast<double>(Bytes);
    if (Bytes >= 1024ull * 1024ull)
    {
        snprintf(Buffer, sizeof(Buffer), "%.2f MB", Value / (1024.0 * 1024.0));
    }
    else if (Bytes >= 1024ull)
    {
        snprintf(Buffer, sizeof(Buffer), "%.2f KB", Value / 1024.0);
    }
    else
    {
        snprintf(Buffer, sizeof(Buffer), "%zu B", Bytes);
    }
    return Buffer;
}

float GetDebugCameraBaseSpeed()
{
    return std::max(0.1f, FEditorSettings::Get().CameraSpeed);
}

float GetDebugCameraSpeedMultiplier(FEditorViewportClient* Client)
{
    if (!Client)
    {
        return 1.0f;
    }
    return MathUtil::Clamp(Client->GetMoveSpeed() / GetDebugCameraBaseSpeed(), 0.01f, MaxDebugCameraSpeedMultiplier);
}

void SetDebugCameraSpeedMultiplier(FEditorViewportClient* Client, float Multiplier)
{
    if (!Client)
    {
        return;
    }

    Client->SetMoveSpeed(MathUtil::Clamp(
        GetDebugCameraBaseSpeed() * Multiplier,
        0.1f,
        GetDebugCameraBaseSpeed() * MaxDebugCameraSpeedMultiplier));
}

UParticleSystemComponent* FindSelectedParticleSystemComponent(UEditorEngine* InEditorEngine)
{
    if (!InEditorEngine)
    {
        return nullptr;
    }

    FWorldContext* Context = InEditorEngine->GetFocusedWorldContext();
    if (!Context || !Context->SelectionManager)
    {
        return nullptr;
    }

    if (UParticleSystemComponent* SelectedComponent = Cast<UParticleSystemComponent>(
        Context->SelectionManager->GetSelectedComponent()))
    {
        return SelectedComponent;
    }

    AActor* SelectedActor = Context->SelectionManager->GetPrimarySelection();
    return SelectedActor ? SelectedActor->FindComponent<UParticleSystemComponent>() : nullptr;
}

UParticleSystemComponent* FindFirstParticleSystemComponentInWorld(UEditorEngine* InEditorEngine)
{
    UWorld* World = InEditorEngine ? InEditorEngine->GetFocusedWorld() : nullptr;
    if (!World)
    {
        return nullptr;
    }

    for (AActor* Actor : World->GetActors())
    {
        if (!Actor)
        {
            continue;
        }

        if (UParticleSystemComponent* ParticleComponent = Actor->FindComponent<UParticleSystemComponent>())
        {
            return ParticleComponent;
        }
    }

    return nullptr;
}

struct FParticleRenderSmokeCounts
{
    int32 ParticleCommandCount = 0;
    uint32 SpriteInstanceCount = 0;
    uint32 MeshInstanceCount = 0;
    uint32 RibbonVertexCount = 0;
    uint32 BeamVertexCount = 0;
};

void WarmupParticleComponent(UParticleSystemComponent* ParticleComponent, int32 FrameCount = 10)
{
    if (!ParticleComponent)
    {
        return;
    }

    constexpr float FixedDeltaTime = 1.0f / 60.0f;
    for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
    {
        ParticleComponent->TickPreview(FixedDeltaTime, true);
    }
}

bool CollectParticleRenderCommandData(
    UEditorEngine* InEditorEngine,
    UParticleSystemComponent* ParticleComponent,
    FParticleRenderSmokeCounts& OutCounts,
    FString& OutError)
{
    OutCounts = FParticleRenderSmokeCounts();

    if (!ParticleComponent)
    {
        OutError = "particle component is null";
        return false;
    }
    if (!ParticleComponent->GetTemplate())
    {
        OutError = "particle component has no template";
        return false;
    }

    ParticleComponent->RefreshTemplateRuntime(false);
    WarmupParticleComponent(ParticleComponent, 12);

    if (ParticleComponent->GetTotalActiveParticleCount() <= 0)
    {
        OutError = "no active particles available for render command collection";
        return false;
    }

    ID3D11Device* Device = InEditorEngine
        ? InEditorEngine->GetRenderer().GetFD3DDevice().GetDevice()
        : nullptr;
    if (!Device)
    {
        OutError = "renderer D3D device is not available";
        return false;
    }

    FRenderBus RenderBus;
    FMeshBufferManager MeshBufferManager;
    MeshBufferManager.Create(Device);

    FShowFlags ShowFlags;
    ShowFlags.bPrimitives = true;

    FPrimitiveDrawCommandBuilder Builder;
    const bool bCollected = Builder.CollectPrimitive(
        ParticleComponent,
        ShowFlags,
        EViewMode::Lit_BlinnPhong,
        RenderBus,
        MeshBufferManager);

    const TArray<FRenderCommand>& ParticleCommands = RenderBus.GetCommands(ERenderPass::Particle);
    bool bFoundParticleDrawData = false;
    for (const FRenderCommand& Command : ParticleCommands)
    {
        if (Command.SourcePrimitive != ParticleComponent)
        {
            continue;
        }

        FDynamicEmitterDataBase* DynamicData = Command.DynamicData;
        if (!DynamicData)
        {
            continue;
        }

        switch (Command.VertexFactoryType)
        {
        case EVertexFactoryType::SpriteParticle:
            if (FDynamicSpriteEmitterData* SpriteData = static_cast<FDynamicSpriteEmitterData*>(DynamicData))
            {
                const uint32 Count = static_cast<uint32>(SpriteData->SpriteInstanceDataBuffer.size());
                if (Count > 0)
                {
                    bFoundParticleDrawData = true;
                    OutCounts.SpriteInstanceCount += Count;
                }
            }
            break;
        case EVertexFactoryType::MeshParticle:
            if (FDynamicMeshEmitterData* MeshData = static_cast<FDynamicMeshEmitterData*>(DynamicData))
            {
                const uint32 Count = static_cast<uint32>(MeshData->MeshInstanceDataBuffer.size());
                if (Count > 0)
                {
                    bFoundParticleDrawData = true;
                    OutCounts.MeshInstanceCount += Count;
                }
            }
            break;
        case EVertexFactoryType::RibbonParticle:
            if (FDynamicRibbonEmitterData* RibbonData = static_cast<FDynamicRibbonEmitterData*>(DynamicData))
            {
                const uint32 Count = static_cast<uint32>(RibbonData->RibbonVertexBuffer.size());
                if (Count > 0)
                {
                    bFoundParticleDrawData = true;
                    OutCounts.RibbonVertexCount += Count;
                }
            }
            break;
        case EVertexFactoryType::BeamParticle:
            if (FDynamicBeamEmitterData* BeamData = static_cast<FDynamicBeamEmitterData*>(DynamicData))
            {
                const uint32 Count = static_cast<uint32>(BeamData->BeamVertexBuffer.size());
                if (Count > 0)
                {
                    bFoundParticleDrawData = true;
                    OutCounts.BeamVertexCount += Count;
                }
            }
            break;
        default:
            break;
        }

        delete DynamicData;
    }

    OutCounts.ParticleCommandCount = static_cast<int32>(ParticleCommands.size());
    MeshBufferManager.Release();

    if (!bCollected)
    {
        OutError = "primitive draw command builder rejected the particle component";
        return false;
    }

    if (!bFoundParticleDrawData)
    {
        char Buffer[192];
        snprintf(
            Buffer,
            sizeof(Buffer),
            "no particle render data, particleCommands=%d",
            OutCounts.ParticleCommandCount);
        OutError = Buffer;
        return false;
    }

    return true;
}

bool RunSelectedParticleRuntimeSmoke(UEditorEngine* InEditorEngine, FString& OutSummary)
{
    UParticleSystemComponent* ParticleComponent = FindSelectedParticleSystemComponent(InEditorEngine);
    bool bUsedSelection = true;
    if (!ParticleComponent)
    {
        ParticleComponent = FindFirstParticleSystemComponentInWorld(InEditorEngine);
        bUsedSelection = false;
    }

    if (!ParticleComponent)
    {
        OutSummary = "no particle system component in selection or focused world";
        return false;
    }

    if (!ParticleComponent->GetTemplate())
    {
        OutSummary = "selected particle component has no template";
        return false;
    }

    const int32 EmitterInstanceCount = ParticleComponent->GetEmitterInstanceCount();
    if (EmitterInstanceCount <= 0)
    {
        OutSummary = "template did not create emitter instances";
        return false;
    }

    constexpr int32 WarmupFrameCount = 10;
    constexpr float FixedDeltaTime = 1.0f / 60.0f;
    for (int32 FrameIndex = 0; FrameIndex < WarmupFrameCount; ++FrameIndex)
    {
        ParticleComponent->TickPreview(FixedDeltaTime, true);
    }

    int32 ActiveParticleCount = ParticleComponent->GetTotalActiveParticleCount();
    if (ActiveParticleCount <= 0)
    {
        char Buffer[160];
        snprintf(
            Buffer,
            sizeof(Buffer),
            "no active particles after %d warmup frames, emitters=%d",
            WarmupFrameCount,
            EmitterInstanceCount);
        OutSummary = Buffer;
        return false;
    }

    ParticleComponent->RefreshTemplateRuntime(false);
    ActiveParticleCount = ParticleComponent->GetTotalActiveParticleCount();
    if (ActiveParticleCount <= 0)
    {
        OutSummary = "runtime refresh dropped active particles";
        return false;
    }
    uint32 SpriteInstanceCount = 0;
    uint32 MeshInstanceCount = 0;
    uint32 RibbonVertexCount = 0;
    int32 CompiledLODCount = 0;
    bool bFoundRenderableData = false;
    for (int32 EmitterIndex = 0; EmitterIndex < EmitterInstanceCount; ++EmitterIndex)
    {
        FParticleEmitterInstance* Instance = ParticleComponent->GetEmitterInstance(EmitterIndex);
        if (!Instance)
        {
            char Buffer[128];
            snprintf(Buffer, sizeof(Buffer), "emitter instance %d is null", EmitterIndex);
            OutSummary = Buffer;
            return false;
        }

        const FCompiledParticleLODData* CompiledLOD = Instance->GetCurrentCompiledLODData();

        if (!CompiledLOD)
        {
            char Buffer[160];
            snprintf(Buffer, sizeof(Buffer), "emitter instance %d has no compiled LOD data", EmitterIndex);
            OutSummary = Buffer;
            return false;
        }
        ++CompiledLODCount;

        if (Instance->GetActiveParticleCount() <= 0)
        {
            continue;
        }

        FDynamicEmitterDataBase* DynData = Instance->CreateDynamicData();
        if (!DynData)
        {
            continue;
        }

        if (DynData->GetSource().eEmitterType != EDynamicEmitterType::None)
        {
            switch (DynData->GetVertexFactoryType())
            {
            case EVertexFactoryType::SpriteParticle:
            {
                FDynamicSpriteEmitterData* SpriteDyn = static_cast<FDynamicSpriteEmitterData*>(DynData);
                const uint32 Count = static_cast<uint32>(SpriteDyn->SpriteInstanceDataBuffer.size());
                SpriteInstanceCount += Count;
                bFoundRenderableData |= Count > 0;
                break;
            }
            case EVertexFactoryType::MeshParticle:
            {
                FDynamicMeshEmitterData* MeshDyn = static_cast<FDynamicMeshEmitterData*>(DynData);
                const uint32 Count = static_cast<uint32>(MeshDyn->MeshInstanceDataBuffer.size());
                MeshInstanceCount += Count;
                bFoundRenderableData |= Count > 0;
                break;
            }
            case EVertexFactoryType::RibbonParticle:
            {
                FDynamicRibbonEmitterData* RibbonDyn = static_cast<FDynamicRibbonEmitterData*>(DynData);
                const uint32 Count = static_cast<uint32>(RibbonDyn->RibbonVertexBuffer.size());
                RibbonVertexCount += Count;
                bFoundRenderableData |= Count > 0;
                break;
            }
            case EVertexFactoryType::BeamParticle:
            {
                FDynamicBeamEmitterData* BeamDyn = static_cast<FDynamicBeamEmitterData*>(DynData);
                bFoundRenderableData |= !BeamDyn->BeamVertexBuffer.empty();
                break;
            }
            default:
                break;
            }
        }
        delete DynData;
    }

    if (!bFoundRenderableData)
    {
        char Buffer[192];
        snprintf(
            Buffer,
            sizeof(Buffer),
            "active particles exist but no render data was built, active=%d, emitters=%d",
            ActiveParticleCount,
            EmitterInstanceCount);
        OutSummary = Buffer;
        return false;
    }

    char Buffer[256];
    snprintf(
        Buffer,
        sizeof(Buffer),
        "%s, emitters=%d, compiledLODs=%d, active=%d, spriteInstances=%u, meshInstances=%u, ribbonVertices=%u",
        bUsedSelection ? "selected" : "firstInWorld",
        EmitterInstanceCount,
        CompiledLODCount,
        ActiveParticleCount,
        SpriteInstanceCount,
        MeshInstanceCount,
        RibbonVertexCount);
    OutSummary = Buffer;
    return true;
}

bool RunParticleRenderCommandSmoke(UEditorEngine* InEditorEngine, FString& OutSummary)
{
    UParticleSystemComponent* ParticleComponent = FindSelectedParticleSystemComponent(InEditorEngine);
    bool bUsedSelection = true;
    if (!ParticleComponent)
    {
        ParticleComponent = FindFirstParticleSystemComponentInWorld(InEditorEngine);
        bUsedSelection = false;
    }

    if (!ParticleComponent)
    {
        OutSummary = "no particle system component in selection or focused world";
        return false;
    }

    if (!ParticleComponent->GetTemplate())
    {
        OutSummary = "particle component has no template";
        return false;
    }

    FParticleRenderSmokeCounts Counts;
    FString Error;
    if (!CollectParticleRenderCommandData(InEditorEngine, ParticleComponent, Counts, Error))
    {
        OutSummary = Error;
        return false;
    }

    char Buffer[256];
    snprintf(
        Buffer,
        sizeof(Buffer),
        "%s, commands=%d, spriteInstances=%u, meshInstances=%u, ribbonVertices=%u, beamVertices=%u",
        bUsedSelection ? "selected" : "firstInWorld",
        Counts.ParticleCommandCount,
        Counts.SpriteInstanceCount,
        Counts.MeshInstanceCount,
        Counts.RibbonVertexCount,
        Counts.BeamVertexCount);
    OutSummary = Buffer;
    return true;
}

bool RunParticleRenderCoverageSmoke(UEditorEngine* InEditorEngine, FString& OutSummary)
{
    struct FCaseResult
    {
        const char* Label = "";
        FParticleRenderSmokeCounts Counts;
    };

    FCaseResult Results[4];
    int32 ResultCount = 0;

    auto RunCase = [&](const char* Label, EParticleEmitterRenderMode ExpectedMode, UParticleSystem* ParticleSystem) -> bool
    {
        UParticleSystemComponent* Component = UObjectManager::Get().CreateObject<UParticleSystemComponent>();
        auto Cleanup = [&]()
        {
            if (Component)
            {
                UObjectManager::Get().DestroyObject(Component);
                Component = nullptr;
            }
            if (ParticleSystem)
            {
                UObjectManager::Get().DestroyObject(ParticleSystem);
                ParticleSystem = nullptr;
            }
        };

        if (!ParticleSystem || !Component)
        {
            OutSummary = FString(Label) + " case failed to create test objects";
            Cleanup();
            return false;
        }

        Component->SetTemplate(ParticleSystem);

        FParticleRenderSmokeCounts Counts;
        FString Error;
        if (!CollectParticleRenderCommandData(InEditorEngine, Component, Counts, Error))
        {
            OutSummary = FString(Label) + " render command collection failed: " + Error;
            Cleanup();
            return false;
        }

        bool bHasExpectedData = false;
        switch (ExpectedMode)
        {
        case EParticleEmitterRenderMode::Sprite:
            bHasExpectedData = Counts.SpriteInstanceCount > 0;
            break;
        case EParticleEmitterRenderMode::Mesh:
            bHasExpectedData = Counts.MeshInstanceCount > 0;
            break;
        case EParticleEmitterRenderMode::Ribbon:
            bHasExpectedData = Counts.RibbonVertexCount > 0;
            break;
        case EParticleEmitterRenderMode::Beam:
            bHasExpectedData = Counts.BeamVertexCount > 0;
            break;
        default:
            break;
        }

        if (!bHasExpectedData)
        {
            char Buffer[256];
            snprintf(
                Buffer,
                sizeof(Buffer),
                "%s expected render data missing, commands=%d, sprite=%u, mesh=%u, ribbon=%u, beam=%u",
                Label,
                Counts.ParticleCommandCount,
                Counts.SpriteInstanceCount,
                Counts.MeshInstanceCount,
                Counts.RibbonVertexCount,
                Counts.BeamVertexCount);
            OutSummary = Buffer;
            Cleanup();
            return false;
        }

        Results[ResultCount++] = { Label, Counts };
        Cleanup();
        return true;
    };

    if (!RunCase("sprite", EParticleEmitterRenderMode::Sprite, UParticleSystem::CreateDefaultSpriteSystem()))
    {
        return false;
    }
    if (!RunCase("mesh", EParticleEmitterRenderMode::Mesh, UParticleSystem::CreateDefaultMeshSystem()))
    {
        return false;
    }
    if (!RunCase("ribbon", EParticleEmitterRenderMode::Ribbon, UParticleSystem::CreateDefaultRibbonSystem()))
    {
        return false;
    }
    if (!RunCase("beam", EParticleEmitterRenderMode::Beam, UParticleSystem::CreateDefaultBeamSystem()))
    {
        return false;
    }

    uint32 SpriteInstances = 0;
    uint32 MeshInstances = 0;
    uint32 RibbonVertices = 0;
    uint32 BeamVertices = 0;
    int32 Commands = 0;
    for (int32 Index = 0; Index < ResultCount; ++Index)
    {
        Commands += Results[Index].Counts.ParticleCommandCount;
        SpriteInstances += Results[Index].Counts.SpriteInstanceCount;
        MeshInstances += Results[Index].Counts.MeshInstanceCount;
        RibbonVertices += Results[Index].Counts.RibbonVertexCount;
        BeamVertices += Results[Index].Counts.BeamVertexCount;
    }

    char Buffer[256];
    snprintf(
        Buffer,
        sizeof(Buffer),
        "sprite/mesh/ribbon/beam commands ok: commands=%d, sprite=%u, mesh=%u, ribbon=%u, beam=%u",
        Commands,
        SpriteInstances,
        MeshInstances,
        RibbonVertices,
        BeamVertices);
    OutSummary = Buffer;
    return true;
}

bool RunParticleLODSmoke(FString& OutSummary)
{
    UParticleSystem* ParticleSystem = UParticleSystem::CreateDefaultSpriteSystem();
    auto Fail = [&](const char* Message) -> bool
    {
        OutSummary = Message;
        if (ParticleSystem)
        {
            UObjectManager::Get().DestroyObject(ParticleSystem);
            ParticleSystem = nullptr;
        }
        return false;
    };

    if (!ParticleSystem || ParticleSystem->GetEmitters().empty())
    {
        return Fail("failed to create default particle system");
    }

    UParticleEmitter* Emitter = ParticleSystem->GetEmitters()[0];
    UParticleLODLevel* LOD0 = Emitter ? Emitter->GetLODLevel(0) : nullptr;
    if (!Emitter || !LOD0)
    {
        return Fail("default particle system has no emitter or LOD0");
    }

    LOD0->Level = 0;
    LOD0->bEnabled = true;
    LOD0->DistanceThreshold = 100.0f;

    UParticleLODLevel* LOD1 = Cast<UParticleLODLevel>(LOD0->Duplicate());
    if (!LOD1)
    {
        return Fail("failed to duplicate LOD0");
    }

    LOD1->Level = 1;
    LOD1->bEnabled = true;
    LOD1->DistanceThreshold = 1000.0f;
    Emitter->LODLevels.push_back(LOD1);
    Emitter->CacheEmitterModuleInfo();

    FParticleEmitterInstance Instance;
    Instance.Init(Emitter, nullptr, 0);
    Instance.SelectLODLevel(50.0f);
    if (Instance.GetCurrentLODLevelIndex() != 0)
    {
        return Fail("near distance did not select LOD0");
    }

    Instance.SpawnParticles(1, 0.0f, 0.0f, FVector::ZeroVector, FVector::ZeroVector);
    const int32 ActiveBeforeSwitch = Instance.GetActiveParticleCount();
    if (ActiveBeforeSwitch <= 0)
    {
        return Fail("failed to spawn baseline particle before LOD switch");
    }

    Instance.SelectLODLevel(500.0f);
    if (Instance.GetCurrentLODLevelIndex() != 1)
    {
        return Fail("far distance did not select LOD1");
    }
    if (Instance.GetActiveParticleCount() != ActiveBeforeSwitch)
    {
        return Fail("LOD switch did not preserve existing particles");
    }

    LOD0->bEnabled = false;
    Emitter->CacheEmitterModuleInfo();
    Instance.RebindCompiledLOD(50.0f);
    if (Instance.GetCurrentLODLevelIndex() != 1)
    {
        return Fail("disabled LOD0 was not skipped");
    }

    const FCompiledParticleLODData* CompiledLOD = Instance.GetCurrentCompiledLODData();
    if (!CompiledLOD || CompiledLOD->SourceLODLevel != LOD1)
    {
        return Fail("compiled LOD did not rebind to LOD1");
    }
    if (CompiledLOD->SpawnModule != LOD1->GetSpawnModule())
    {
        return Fail("rebound compiled LOD does not use LOD1 spawn module");
    }

    Instance.SpawnParticles(1, 0.0f, 0.0f, FVector::ZeroVector, FVector::ZeroVector);
    if (Instance.GetActiveParticleCount() <= ActiveBeforeSwitch)
    {
        return Fail("new spawn after LOD switch did not use active compiled LOD");
    }

    char Buffer[192];
    snprintf(
        Buffer,
        sizeof(Buffer),
        "nearLOD=0, farLOD=1, disabledSkipLOD=%d, activeBefore=%d, activeAfter=%d",
        Instance.GetCurrentLODLevelIndex(),
        ActiveBeforeSwitch,
        Instance.GetActiveParticleCount());
    OutSummary = Buffer;

    UObjectManager::Get().DestroyObject(ParticleSystem);
    ParticleSystem = nullptr;
    return true;
}

bool RunParticleEditorRefreshRegressionSmoke(UEditorEngine* InEditorEngine, FString& OutSummary)
{
    UParticleSystem* ParticleSystem = UParticleSystem::CreateDefaultSpriteSystem();
    UParticleSystemComponent* PreviewComponent = UObjectManager::Get().CreateObject<UParticleSystemComponent>();
    UWorld* World = InEditorEngine ? InEditorEngine->GetFocusedWorld() : nullptr;
    AParticleSystemActor* PlacedActor = World ? World->SpawnActor<AParticleSystemActor>() : nullptr;

    auto Cleanup = [&]()
    {
        if (World && PlacedActor)
        {
            World->DestroyActor(PlacedActor);
            PlacedActor = nullptr;
            World->SyncSpatialIndex();
        }
        if (PreviewComponent)
        {
            UObjectManager::Get().DestroyObject(PreviewComponent);
            PreviewComponent = nullptr;
        }
        if (ParticleSystem)
        {
            UObjectManager::Get().DestroyObject(ParticleSystem);
            ParticleSystem = nullptr;
        }
    };

    auto Fail = [&](const char* Message) -> bool
    {
        OutSummary = Message;
        Cleanup();
        return false;
    };

    if (!ParticleSystem || !PreviewComponent || !World || !PlacedActor)
    {
        return Fail("failed to create particle refresh regression objects");
    }

    PlacedActor->InitDefaultComponents();
    PlacedActor->SetFName(FName("Particle Refresh Regression Actor"));
    PlacedActor->SetTemplate(ParticleSystem);
    PlacedActor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
    World->SyncSpatialIndex();

    UParticleSystemComponent* PlacedComponent = PlacedActor->GetParticleSystemComponent();
    PreviewComponent->SetTemplate(ParticleSystem);
    if (!PlacedComponent || !PreviewComponent->GetTemplate() || !PlacedComponent->GetTemplate())
    {
        return Fail("preview or placed particle component did not receive template");
    }

    WarmupParticleComponent(PreviewComponent, 12);
    WarmupParticleComponent(PlacedComponent, 12);
    const int32 PreviewActiveBefore = PreviewComponent->GetTotalActiveParticleCount();
    const int32 PlacedActiveBefore = PlacedComponent->GetTotalActiveParticleCount();
    if (PreviewActiveBefore <= 0 || PlacedActiveBefore <= 0)
    {
        return Fail("baseline preview or placed component did not spawn particles");
    }

    UParticleEmitter* Emitter = ParticleSystem->GetEmitters().empty() ? nullptr : ParticleSystem->GetEmitters()[0];
    UParticleLODLevel* LOD0 = Emitter ? Emitter->GetLODLevel(0) : nullptr;
    if (!Emitter || !LOD0)
    {
        return Fail("default particle system has no emitter or LOD0");
    }

    auto ValidateRebound = [&](const char* Label, bool bRequireActivePreserved) -> bool
    {
        PreviewComponent->RefreshTemplateRuntime(false);
        PlacedComponent->RefreshTemplateRuntime(false);

        FParticleEmitterInstance* PreviewInstance = PreviewComponent->GetEmitterInstance(0);
        FParticleEmitterInstance* PlacedInstance = PlacedComponent->GetEmitterInstance(0);
        if (!PreviewInstance || !PlacedInstance)
        {
            OutSummary = FString(Label) + " refresh lost emitter instance";
            return false;
        }
        if (!PreviewInstance->GetCurrentCompiledLODData() || !PlacedInstance->GetCurrentCompiledLODData())
        {
            OutSummary = FString(Label) + " refresh left a null compiled LOD";
            return false;
        }
        if (bRequireActivePreserved &&
            (PreviewComponent->GetTotalActiveParticleCount() <= 0 || PlacedComponent->GetTotalActiveParticleCount() <= 0))
        {
            OutSummary = FString(Label) + " refresh dropped live particles";
            return false;
        }
        return true;
    };

    if (UParticleModule* Module = !LOD0->GetModules().empty() ? LOD0->GetModules()[0] : nullptr)
    {
        Module->SetEnabled(!Module->IsEnabled());
        Emitter->CacheEmitterModuleInfo();
        if (!ValidateRebound("module property edit", true))
        {
            Cleanup();
            return false;
        }
        Module->SetEnabled(!Module->IsEnabled());
    }

    LOD0->DistanceThreshold += 500.0f;
    Emitter->CacheEmitterModuleInfo();
    if (!ValidateRebound("LOD threshold edit", true))
    {
        Cleanup();
        return false;
    }

    UParticleLODLevel* AddedLOD = Cast<UParticleLODLevel>(LOD0->Duplicate());
    if (!AddedLOD)
    {
        return Fail("failed to duplicate LOD for refresh regression");
    }
    AddedLOD->Level = static_cast<int32>(Emitter->GetLODLevels().size());
    AddedLOD->bEnabled = true;
    AddedLOD->DistanceThreshold = LOD0->GetDistanceThreshold() + 1000.0f;
    Emitter->LODLevels.push_back(AddedLOD);
    Emitter->CacheEmitterModuleInfo();
    if (!ValidateRebound("LOD add edit", true))
    {
        Cleanup();
        return false;
    }

    Emitter->RemoveLODLevel(static_cast<int32>(Emitter->GetLODLevels().size()) - 1);
    if (!ValidateRebound("LOD delete edit", true))
    {
        Cleanup();
        return false;
    }

    UParticleMeshRendererProperties* MeshRenderer = UObjectManager::Get().CreateObject<UParticleMeshRendererProperties>();
    if (!MeshRenderer)
    {
        return Fail("failed to create mesh renderer properties");
    }
    MeshRenderer->SetMesh(FResourceManager::Get().LoadStaticMesh("Asset/Mesh/Dice/Dice.obj"));
    LOD0->SetRendererProperties(MeshRenderer);
    Emitter->CacheEmitterModuleInfo();

    PreviewComponent->RefreshTemplateRuntime(false);
    PlacedComponent->RefreshTemplateRuntime(false);
    FParticleEmitterInstance* PreviewInstanceAfterRenderer = PreviewComponent->GetEmitterInstance(0);
    FParticleEmitterInstance* PlacedInstanceAfterRenderer = PlacedComponent->GetEmitterInstance(0);
    const FCompiledParticleLODData* PreviewCompiledAfterRenderer =
        PreviewInstanceAfterRenderer ? PreviewInstanceAfterRenderer->GetCurrentCompiledLODData() : nullptr;
    const FCompiledParticleLODData* PlacedCompiledAfterRenderer =
        PlacedInstanceAfterRenderer ? PlacedInstanceAfterRenderer->GetCurrentCompiledLODData() : nullptr;
    if (!PreviewCompiledAfterRenderer || !PlacedCompiledAfterRenderer ||
        PreviewCompiledAfterRenderer->RenderMode != EParticleEmitterRenderMode::Mesh ||
        PlacedCompiledAfterRenderer->RenderMode != EParticleEmitterRenderMode::Mesh)
    {
        return Fail("renderer property edit did not recreate/rebind components to mesh compiled LOD");
    }

    WarmupParticleComponent(PreviewComponent, 12);
    WarmupParticleComponent(PlacedComponent, 12);
    if (PreviewComponent->GetTotalActiveParticleCount() <= 0 || PlacedComponent->GetTotalActiveParticleCount() <= 0)
    {
        return Fail("renderer property refresh did not restart live simulation");
    }

    char Buffer[256];
    snprintf(
        Buffer,
        sizeof(Buffer),
        "preview and placed PSC refreshed: activeBefore=%d/%d, finalMode=Mesh, finalActive=%d/%d",
        PreviewActiveBefore,
        PlacedActiveBefore,
        PreviewComponent->GetTotalActiveParticleCount(),
        PlacedComponent->GetTotalActiveParticleCount());
    OutSummary = Buffer;
    Cleanup();
    return true;
}

bool RunParticleEventDispatchSmoke(FString& OutSummary)
{
    UParticleSystemComponent* Component = UObjectManager::Get().CreateObject<UParticleSystemComponent>();
    AParticleEventManager* Dispatcher = UObjectManager::Get().CreateObject<AParticleEventManager>();

    auto Cleanup = [&]()
    {
        if (Dispatcher)
        {
            UObjectManager::Get().DestroyObject(Dispatcher);
            Dispatcher = nullptr;
        }
        if (Component)
        {
            UObjectManager::Get().DestroyObject(Component);
            Component = nullptr;
        }
    };

    auto Fail = [&](const char* Message) -> bool
    {
        OutSummary = Message;
        Cleanup();
        return false;
    };

    if (!Component || !Dispatcher)
    {
        return Fail("failed to create particle event test objects");
    }

    int32 ComponentBroadcastCount = 0;
    int32 DispatcherBroadcastCount = 0;
    uint32 ComponentParticleId = 0;
    uint32 DispatcherParticleId = 0;

    Dispatcher->BindToParticleSystemComponent(Component);
    Component->OnParticleCollide.Add(
        [&](const FParticleEventCollideData& EventData)
        {
            ++ComponentBroadcastCount;
            ComponentParticleId = EventData.ParticleId;
        });
    Dispatcher->OnParticleCollide.Add(
        [&](const FParticleEventCollideData& EventData)
        {
            ++DispatcherBroadcastCount;
            DispatcherParticleId = EventData.ParticleId;
        });

    FParticleEventCollideData EventData;
    EventData.Component = Component;
    EventData.EmitterIndex = 3;
    EventData.ParticleId = 77;
    EventData.Location = FVector(1.0f, 2.0f, 3.0f);
    EventData.Normal = FVector::UpVector;

    Component->QueueCollisionEvent(EventData);
    if (!Component->HasPendingCollisionEvents() || Component->GetPendingCollisionEvents().size() != 1)
    {
        return Fail("collision event was not queued on the component");
    }
    if (ComponentBroadcastCount != 0 || DispatcherBroadcastCount != 0)
    {
        return Fail("collision event was broadcast before dispatch");
    }

    Component->DispatchQueuedParticleEvents();
    if (Component->HasPendingCollisionEvents())
    {
        return Fail("component collision queue was not cleared after dispatch");
    }
    if (!Dispatcher->GetCollisionEvents().empty())
    {
        return Fail("dispatcher listener queue was not cleared after broadcast");
    }
    if (ComponentBroadcastCount != 1 || ComponentParticleId != 77)
    {
        return Fail("component collision delegate did not receive the queued event");
    }
    if (DispatcherBroadcastCount != 1 || DispatcherParticleId != 77)
    {
        return Fail("event dispatcher delegate did not receive the queued event");
    }

    char Buffer[160];
    snprintf(
        Buffer,
        sizeof(Buffer),
        "queued=1, componentBroadcasts=%d, dispatcherBroadcasts=%d, particleId=%u",
        ComponentBroadcastCount,
        DispatcherBroadcastCount,
        ComponentParticleId);
    OutSummary = Buffer;
    Cleanup();
    return true;
}

} // namespace

void FEditorMainPanel::RenderUndoHistoryPanel(float DeltaTime)
{
    (void)DeltaTime;
    if (!PanelVisibility.bShowUndoHistory || !EditorEngine)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Undo History", &PanelVisibility.bShowUndoHistory))
    {
        ImGui::End();
        return;
    }

    const FEditorUndoSystem& UndoSystem = EditorEngine->GetUndoSystem();
    const TArray<FUndoSnapshotEntry>& UndoEntries = UndoSystem.GetUndoHistory();
    const TArray<FUndoSnapshotEntry>& RedoEntries = UndoSystem.GetRedoHistory();
    const FUndoHistoryStats HistoryStats = UndoSystem.GetStats();

    const bool bCanUndo = !UndoEntries.empty();
    const bool bCanRedo = !RedoEntries.empty();
    ImGui::BeginDisabled(!bCanUndo);
    if (ImGui::Button("Undo", ImVec2(86.0f, 0.0f)))
    {
        EditorEngine->GetCommandSystem().Execute(EEditorCommand::Undo);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!bCanRedo);
    if (ImGui::Button("Redo", ImVec2(86.0f, 0.0f)))
    {
        EditorEngine->GetCommandSystem().Execute(EEditorCommand::Redo);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!bCanUndo && !bCanRedo);
    if (ImGui::Button("Clear", ImVec2(86.0f, 0.0f)))
    {
        EditorEngine->GetCommandSystem().Execute(EEditorCommand::ClearUndoHistory);
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Stat History", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Entries: %d / %d", HistoryStats.UndoCount + HistoryStats.RedoCount, HistoryStats.MaxEntries);
        ImGui::TextDisabled("Undo %d, Redo %d", HistoryStats.UndoCount, HistoryStats.RedoCount);
        ImGui::Text("Snapshot Data: %s", FormatHistoryBytes(HistoryStats.LogicalBytes).c_str());
        ImGui::Text("Reserved Memory: %s", FormatHistoryBytes(HistoryStats.ReservedBytes).c_str());
        ImGui::TextDisabled("Approx Total: %s", FormatHistoryBytes(HistoryStats.ApproxTotalBytes).c_str());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Approx Total = string reserved capacity + entry storage. Scene restore also creates a temporary world only while undo/redo is executing.");
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Undo");
    ImGui::BeginChild("##UndoHistoryList", ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.62f), true);
    if (UndoEntries.empty())
    {
        ImGui::TextDisabled("No undo history.");
    }
    else
    {
        for (int32 Index = static_cast<int32>(UndoEntries.size()) - 1; Index >= 0; --Index)
        {
            ImGui::PushID(Index);
            const FString Label = UndoEntries[Index].Label.empty() ? FString("Scene Edit") : UndoEntries[Index].Label;
            if (ImGui::Selectable(Label.c_str()))
            {
                FEditorCommandArgs Args;
                Args.HistoryIndex = Index;
                EditorEngine->GetCommandSystem().Execute(EEditorCommand::RestoreUndoHistoryIndex, Args);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::TextDisabled("Redo");
    ImGui::BeginChild("##RedoHistoryList", ImVec2(0.0f, 0.0f), true);
    if (RedoEntries.empty())
    {
        ImGui::TextDisabled("No redo history.");
    }
    else
    {
        for (int32 Index = static_cast<int32>(RedoEntries.size()) - 1; Index >= 0; --Index)
        {
            const FString Label = RedoEntries[Index].Label.empty() ? FString("Scene Edit") : RedoEntries[Index].Label;
            ImGui::TextUnformatted(Label.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void FEditorMainPanel::RenderEditorDebugPanel(float DeltaTime)
{
    (void)DeltaTime;
    if (!PanelVisibility.bShowEditorDebug || !EditorEngine)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(500.0f, 430.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Editor Debug", &PanelVisibility.bShowEditorDebug))
    {
        ImGui::End();
        return;
    }

    FEditorSettings& Settings = FEditorSettings::Get();
    if (ImGui::CollapsingHeader("Viewport", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat("Camera Base Speed", &Settings.CameraSpeed, 0.1f, 0.1f, 100.0f, "%.1f");
        ImGui::DragFloat("Camera Rotate Speed", &Settings.CameraRotationSpeed, 1.0f, 1.0f, 720.0f, "%.0f");
        ImGui::DragFloat("Camera Zoom Speed", &Settings.CameraZoomSpeed, 1.0f, 10.0f, 5000.0f, "%.0f");
        ImGui::DragFloat("Dolly Speed Scale", &Settings.CameraDollySpeedScale, 0.01f, 0.05f, 5.0f, "%.2fx");
        ImGui::DragFloat("Pan Speed Scale", &Settings.CameraPanSpeedScale, 0.05f, 0.05f, 10.0f, "%.2fx");
        const char* PickingModeItems[] = { "ID Buffer", "Ray-Triangle" };
        int32 PickingModeIndex = static_cast<int32>(Settings.PickingMode);
        if (ImGui::Combo("Picking Mode", &PickingModeIndex, PickingModeItems, IM_ARRAYSIZE(PickingModeItems)))
        {
            if (PickingModeIndex >= 0 && PickingModeIndex < static_cast<int32>(EEditorPickingMode::Count))
            {
                Settings.PickingMode = static_cast<EEditorPickingMode>(PickingModeIndex);
            }
        }
        ImGui::Checkbox("Camera Smoothing", &Settings.bEnableCameraSmoothing);
        ImGui::BeginDisabled(!Settings.bEnableCameraSmoothing);
        ImGui::DragFloat("Move Smooth Speed", &Settings.CameraMoveSmoothSpeed, 0.05f, 0.1f, 40.0f, "%.2f");
        ImGui::DragFloat("Rotate Smooth Speed", &Settings.CameraRotateSmoothSpeed, 0.05f, 0.1f, 40.0f, "%.2f");
        ImGui::EndDisabled();

        FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
        if (FEditorViewportClient* FocusedClient = Layout.GetViewportClient(Layout.GetLastFocusedViewportIndex()))
        {
            float SpeedMultiplier = GetDebugCameraSpeedMultiplier(FocusedClient);
            if (ImGui::DragFloat(
                "Focused Speed Multiplier",
                &SpeedMultiplier,
                0.05f,
                0.01f,
                MaxDebugCameraSpeedMultiplier,
                "%.2fx"))
            {
                SetDebugCameraSpeedMultiplier(FocusedClient, SpeedMultiplier);
            }
        }
    }

    if (ImGui::CollapsingHeader("Show Flags", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Primitives", &Settings.ShowFlags.bPrimitives);
        ImGui::Checkbox("Skeletal Mesh", &Settings.ShowFlags.bSkeletalMesh);
        ImGui::Checkbox("Particle System", &Settings.ShowFlags.bParticleSystem);
        ImGui::Checkbox("BillboardText", &Settings.ShowFlags.bBillboardText);
        ImGui::Checkbox("Axis", &Settings.ShowFlags.bAxis);
        ImGui::Checkbox("Grid", &Settings.ShowFlags.bGrid);
        ImGui::Checkbox("Gizmo", &Settings.ShowFlags.bGizmo);
        ImGui::Checkbox("Bounding Volume", &Settings.ShowFlags.bBoundingVolume);
        ImGui::Checkbox("Collision", &Settings.ShowFlags.bCollision);
        if (Settings.ShowFlags.bBoundingVolume)
        {
            ImGui::Indent();
            ImGui::Checkbox("BVH Bounding Volume", &Settings.ShowFlags.bBVHBoundingVolume);
            ImGui::Unindent();
        }
        ImGui::Checkbox("Enable LOD", &Settings.ShowFlags.bEnableLOD);
        ImGui::Checkbox("Decals", &Settings.ShowFlags.bDecals);
        ImGui::Checkbox("Fog", &Settings.ShowFlags.bFog);
        ImGui::Checkbox("Shadow", &Settings.ShowFlags.bShadow);
        ImGui::Checkbox("Bloom", &Settings.ShowFlags.bBloom);
        if (Settings.ShowFlags.bBloom)
        {
            ImGui::Indent();
            ImGui::DragFloat("Bloom Threshold", &Settings.ShowFlags.BloomThreshold, 0.01f, 0.0f, 10.0f, "%.2f");
            ImGui::DragFloat("Bloom Knee", &Settings.ShowFlags.BloomKnee, 0.01f, 0.0f, 2.0f, "%.2f");
            ImGui::DragFloat("Bloom Intensity", &Settings.ShowFlags.BloomIntensity, 0.01f, 0.0f, 5.0f, "%.2f");
            ImGui::DragInt("Bloom Blur Iterations", &Settings.ShowFlags.BloomBlurIterations, 0.05f, 0, 8);
            ImGui::Unindent();
        }
        ImGui::Checkbox("Tone Mapping", &Settings.ShowFlags.bToneMapping);
        if (Settings.ShowFlags.bToneMapping)
        {
            static const char* ToneMappingNames[] = { "Linear", "Reinhard", "ACES", "Hable" };
            int32 ToneMappingIndex = static_cast<int32>(Settings.ShowFlags.ToneMappingMode);
            ToneMappingIndex = std::clamp<int32>(ToneMappingIndex, 0, static_cast<int32>(EToneMappingMode::Count) - 1);
            ImGui::Indent();
            if (ImGui::Combo("Tone Mapping Mode", &ToneMappingIndex, ToneMappingNames, IM_ARRAYSIZE(ToneMappingNames)))
            {
                Settings.ShowFlags.ToneMappingMode = static_cast<EToneMappingMode>(ToneMappingIndex);
            }
            ImGui::DragFloat("Exposure", &Settings.ShowFlags.Exposure, 0.01f, 0.0f, 5.0f, "%.2f");
            if (Settings.ShowFlags.ToneMappingMode == EToneMappingMode::Hable)
            {
                ImGui::DragFloat("Hable White Point", &Settings.ShowFlags.HableWhitePoint, 0.05f, 1.0f, 20.0f, "%.2f");
            }
            ImGui::Unindent();
        }
        ImGui::Checkbox("Gamma Correction", &Settings.ShowFlags.bGammaCorrection);
        if (Settings.ShowFlags.bGammaCorrection)
        {
            ImGui::Indent();
            ImGui::SliderFloat("Gamma", &Settings.ShowFlags.GammaValue, 1.0f, 3.0f, "%.2f");
            ImGui::Unindent();
        }
        ImGui::Checkbox("FXAA", &Settings.bEnableFXAA);
    }

    if (ImGui::CollapsingHeader("Particle", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("Uses selected particle component, or first particle component in the focused world.");
        if (ImGui::Button("Place Default Particle System"))
        {
            const FString DefaultParticlePath = "Asset/Particle System/New Particle System 2.uasset";
            bool bPlaced = false;
            FString Summary = "editor engine is not available";

            if (EditorEngine)
            {
                FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
                const int32 ViewportIndex = Layout.GetLastFocusedViewportIndex();
                FEditorViewportClient* Client = Layout.GetViewportClient(ViewportIndex);
                const FSceneViewport* Viewport = Client ? Client->GetViewport() : nullptr;
                const FViewportRect Rect = Viewport ? Viewport->GetRect() : FViewportRect();
                const float LocalX = Rect.Width > 0 ? static_cast<float>(Rect.Width) * 0.5f : 0.0f;
                const float LocalY = Rect.Height > 0 ? static_cast<float>(Rect.Height) * 0.5f : 0.0f;

                bPlaced = SpawnParticleSystemFromContentPath(DefaultParticlePath, ViewportIndex, LocalX, LocalY);
                Summary = bPlaced ? DefaultParticlePath : "failed to place default particle system";
            }

            FEditorConsoleWidget::AddLog(
                "Default particle placement %s: %s\n",
                bPlaced ? "passed" : "failed",
                Summary.c_str());

            if (EditorEngine)
            {
                if (bPlaced)
                {
                    EditorEngine->GetNotificationService().Info("Default particle system placed in viewport");
                }
                else
                {
                    EditorEngine->GetNotificationService().Warning("Default particle system placement failed");
                }
            }
        }

        if (ImGui::Button("Runtime Smoke"))
        {
            FString Summary;
            const bool bPassed = RunSelectedParticleRuntimeSmoke(EditorEngine, Summary);
            FEditorConsoleWidget::AddLog(
                "Selected particle runtime smoke test %s: %s\n",
                bPassed ? "passed" : "failed",
                Summary.c_str());

            if (EditorEngine)
            {
                if (bPassed)
                {
                    EditorEngine->GetNotificationService().Info("Selected particle runtime smoke test passed");
                }
                else
                {
                    EditorEngine->GetNotificationService().Warning("Selected particle runtime smoke test failed");
                }
            }
        }

        if (ImGui::Button("Render Command Smoke"))
        {
            FString Summary;
            const bool bPassed = RunParticleRenderCommandSmoke(EditorEngine, Summary);
            FEditorConsoleWidget::AddLog(
                "Particle render command smoke test %s: %s\n",
                bPassed ? "passed" : "failed",
                Summary.c_str());

            if (EditorEngine)
            {
                if (bPassed)
                {
                    EditorEngine->GetNotificationService().Info("Particle render command smoke test passed");
                }
                else
                {
                    EditorEngine->GetNotificationService().Warning("Particle render command smoke test failed");
                }
            }
        }

        if (ImGui::Button("Render Coverage Smoke"))
        {
            FString Summary;
            const bool bPassed = RunParticleRenderCoverageSmoke(EditorEngine, Summary);
            FEditorConsoleWidget::AddLog(
                "Particle render coverage smoke test %s: %s\n",
                bPassed ? "passed" : "failed",
                Summary.c_str());

            if (EditorEngine)
            {
                if (bPassed)
                {
                    EditorEngine->GetNotificationService().Info("Particle render coverage smoke test passed");
                }
                else
                {
                    EditorEngine->GetNotificationService().Warning("Particle render coverage smoke test failed");
                }
            }
        }

        if (ImGui::Button("Run Particle Serialization Smoke Test"))
        {
            const FString SmokeTestPath = "Asset/Particle/SmokeTest.uasset";
            const bool bPassed = FResourceManager::Get().RunParticleSystemSerializationSmokeTest(SmokeTestPath);
            FEditorConsoleWidget::AddLog(
                "Particle serialization smoke test %s: %s\n",
                bPassed ? "passed" : "failed",
                SmokeTestPath.c_str());
        }

        if (ImGui::Button("Run Particle LOD Smoke Test"))
        {
            FString Summary;
            const bool bPassed = RunParticleLODSmoke(Summary);
            FEditorConsoleWidget::AddLog(
                "Particle LOD smoke test %s: %s\n",
                bPassed ? "passed" : "failed",
                Summary.c_str());

            if (EditorEngine)
            {
                if (bPassed)
                {
                    EditorEngine->GetNotificationService().Info("Particle LOD smoke test passed");
                }
                else
                {
                    EditorEngine->GetNotificationService().Warning("Particle LOD smoke test failed");
                }
            }
        }

        if (ImGui::Button("Run Particle Editor Refresh Regression Smoke Test"))
        {
            FString Summary;
            const bool bPassed = RunParticleEditorRefreshRegressionSmoke(EditorEngine, Summary);
            FEditorConsoleWidget::AddLog(
                "Particle editor refresh regression smoke test %s: %s\n",
                bPassed ? "passed" : "failed",
                Summary.c_str());

            if (EditorEngine)
            {
                if (bPassed)
                {
                    EditorEngine->GetNotificationService().Info("Particle editor refresh regression smoke test passed");
                }
                else
                {
                    EditorEngine->GetNotificationService().Warning("Particle editor refresh regression smoke test failed");
                }
            }
        }

        if (ImGui::Button("Run Particle Event Dispatch Smoke Test"))
        {
            FString Summary;
            const bool bPassed = RunParticleEventDispatchSmoke(Summary);
            FEditorConsoleWidget::AddLog(
                "Particle event dispatch smoke test %s: %s\n",
                bPassed ? "passed" : "failed",
                Summary.c_str());

            if (EditorEngine)
            {
                if (bPassed)
                {
                    EditorEngine->GetNotificationService().Info("Particle event dispatch smoke test passed");
                }
                else
                {
                    EditorEngine->GetNotificationService().Warning("Particle event dispatch smoke test failed");
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Place Actors (Grid)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const int32 PrimitiveCount = Widgets.ControlWidget.GetPrimitiveTypeCount();
        DebugGridState.PrimitiveType = MathUtil::Clamp(DebugGridState.PrimitiveType, 0, PrimitiveCount - 1);

        if (ImGui::BeginCombo("Actor Type", Widgets.ControlWidget.GetPrimitiveTypeLabel(DebugGridState.PrimitiveType)))
        {
            for (int32 i = 0; i < PrimitiveCount; ++i)
            {
                const bool bSelected = (DebugGridState.PrimitiveType == i);
                if (ImGui::Selectable(Widgets.ControlWidget.GetPrimitiveTypeLabel(i), bSelected))
                {
                    DebugGridState.PrimitiveType = i;
                }
                if (bSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::DragInt("Rows", &DebugGridState.Rows, 1.0f, 1, 128, "%d");
        ImGui::DragInt("Cols", &DebugGridState.Cols, 1.0f, 1, 128, "%d");
        ImGui::DragInt("Layers", &DebugGridState.Layers, 1.0f, 1, 32, "%d");
        ImGui::DragFloat("Grid Spacing", &DebugGridState.Spacing, 0.1f, 0.1f, 1000.0f, "%.2f");
        ImGui::Checkbox("Center Grid Around Origin", &DebugGridState.bCenter);
        ImGui::DragFloat3("Origin", &DebugGridState.Origin.X, 0.1f, -100000.0f, 100000.0f, "%.2f");

        DebugGridState.Rows = MathUtil::Clamp(DebugGridState.Rows, 1, 128);
        DebugGridState.Cols = MathUtil::Clamp(DebugGridState.Cols, 1, 128);
        DebugGridState.Layers = MathUtil::Clamp(DebugGridState.Layers, 1, 32);
        DebugGridState.Spacing = std::max(0.1f, DebugGridState.Spacing);

        const int32 TotalActors = DebugGridState.Rows * DebugGridState.Cols * DebugGridState.Layers;
        ImGui::Text("Total Actors: %d", TotalActors);
        if (TotalActors > 2048)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Large grid; spawn is capped at 2048 per click.");
        }

        if (ImGui::Button("Spawn Grid Actors"))
        {
            const float RowOffset = DebugGridState.bCenter ? static_cast<float>(DebugGridState.Rows - 1) * 0.5f : 0.0f;
            const float ColOffset = DebugGridState.bCenter ? static_cast<float>(DebugGridState.Cols - 1) * 0.5f : 0.0f;
            const float LayerOffset = DebugGridState.bCenter ? static_cast<float>(DebugGridState.Layers - 1) * 0.5f : 0.0f;
            const int32 SpawnLimit = std::min(TotalActors, 2048);
            int32 SpawnedCount = 0;

            for (int32 Layer = 0; Layer < DebugGridState.Layers && SpawnedCount < SpawnLimit; ++Layer)
            {
                for (int32 Row = 0; Row < DebugGridState.Rows && SpawnedCount < SpawnLimit; ++Row)
                {
                    for (int32 Col = 0; Col < DebugGridState.Cols && SpawnedCount < SpawnLimit; ++Col)
                    {
                        const FVector Location(
                            DebugGridState.Origin.X + (static_cast<float>(Col) - ColOffset) * DebugGridState.Spacing,
                            DebugGridState.Origin.Y + (static_cast<float>(Row) - RowOffset) * DebugGridState.Spacing,
                            DebugGridState.Origin.Z + (static_cast<float>(Layer) - LayerOffset) * DebugGridState.Spacing);
                        if (Widgets.ControlWidget.SpawnPrimitive(DebugGridState.PrimitiveType, Location, 1))
                        {
                            ++SpawnedCount;
                        }
                    }
                }
            }

            if (UWorld* World = EditorEngine->GetFocusedWorld())
            {
                World->RebuildSpatialIndex();
            }
            FEditorConsoleWidget::AddLog(
                "Editor Debug grid spawned %d %s actors\n",
                SpawnedCount,
                Widgets.ControlWidget.GetPrimitiveTypeLabel(DebugGridState.PrimitiveType));
        }
    }

    ImGui::End();
}
