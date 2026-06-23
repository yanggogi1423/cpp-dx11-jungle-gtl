#include "Editor/UI/EditorMainPanel.h"
#include "Editor/UI/EditorMainPanelPlacementHelpers.h"

#include "Editor/EditorEngine.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Editor/Viewport/FSceneViewport.h"
#include "Editor/Viewport/ViewportLayout.h"
#include "Engine/Component/SkeletalMeshComponent.h"
#include "Engine/Component/StaticMeshComponent.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Core/Paths.h"
#include "Engine/Asset/AssetFile.h"
#include "Engine/Asset/AssetMetaData.h"
#include "Engine/Asset/SkeletalMesh.h"
#include "Engine/Asset/StaticMesh.h"
#include "Core/ResourceManager.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "Serialization/PrefabManager.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemComponent.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace
{
void RecordPlacedActor(UEditorEngine* EditorEngine, AActor* Actor, const FString& Label)
{
    if (!EditorEngine || !Actor)
    {
        return;
    }

    TArray<AActor*> Actors;
    Actors.push_back(Actor);
    EditorEngine->GetUndoSystem().RecordActorCreation(
        EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
        Label);
}

bool HasParentDirectoryReference(const std::filesystem::path& Path)
{
    for (const std::filesystem::path& Part : Path)
    {
        if (Part == L"..")
        {
            return true;
        }
    }
    return false;
}

std::wstring GetLowerExtension(const std::filesystem::path& Path)
{
    std::wstring Extension = Path.extension().wstring();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
    return Extension;
}

bool ResolveProjectDropPath(
    const FString& PayloadPath,
    std::filesystem::path& OutAbsolutePath,
    std::filesystem::path& OutRelativePath)
{
    std::filesystem::path Path(FPaths::ToWide(PayloadPath));
    const std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir()).lexically_normal();
    if (!Path.is_absolute())
    {
        Path = RootPath / Path;
    }
    Path = Path.lexically_normal();

    const std::filesystem::path RelativePath = Path.lexically_relative(RootPath);
    if (RelativePath.empty() || HasParentDirectoryReference(RelativePath))
    {
        return false;
    }

    OutAbsolutePath = Path;
    OutRelativePath = RelativePath;
    return true;
}

bool IsUAssetClass(const std::filesystem::path& Path, const FString& ExpectedClassName)
{
    if (GetLowerExtension(Path) != L".uasset")
    {
        return false;
    }

    FAssetMetaData MetaData;
    const FString AssetPath = FPaths::Normalize(FPaths::ToUtf8(Path.generic_wstring()));
    return FAssetFile::LoadMetadataOnly(AssetPath, MetaData) &&
        MetaData.ClassName == ExpectedClassName;
}

FString ResolveStaticMeshDropLoadPath(const FString& PayloadPath)
{
    std::filesystem::path Path;
    std::filesystem::path RelativePath;
    if (!ResolveProjectDropPath(PayloadPath, Path, RelativePath))
    {
        return {};
    }

    const std::wstring Extension = GetLowerExtension(Path);
    if (Extension == L".obj" || Extension == L".fbx")
    {
        return FPaths::Normalize(FPaths::ToUtf8(RelativePath.generic_wstring()));
    }
    if (Extension == L".bin")
    {
        return FPaths::Normalize(FPaths::ToUtf8(Path.generic_wstring()));
    }
    if (IsUAssetClass(Path, UStaticMesh::StaticClass()->GetName()))
    {
        return FPaths::Normalize(FPaths::ToUtf8(RelativePath.generic_wstring()));
    }
    return {};
}
FString ResolveParticleSystemDropLoadPath(const FString& PayloadPath)
{
    std::filesystem::path Path;
    std::filesystem::path RelativePath;
    if (!ResolveProjectDropPath(PayloadPath, Path, RelativePath))
    {
        return {};
    }

    if (GetLowerExtension(Path) != L".uasset")
    {
        return {};
    }

    return FPaths::Normalize(FPaths::ToUtf8(RelativePath.generic_wstring()));
}
FString ResolveSkeletalMeshDropLoadPath(const FString& PayloadPath)
{
    std::filesystem::path Path;
    std::filesystem::path RelativePath;
    if (!ResolveProjectDropPath(PayloadPath, Path, RelativePath))
    {
        return {};
    }

    const std::wstring Extension = GetLowerExtension(Path);
    if (Extension == L".fbx" || IsUAssetClass(Path, USkeletalMesh::StaticClass()->GetName()))
    {
        return FPaths::Normalize(FPaths::ToUtf8(RelativePath.generic_wstring()));
    }

    return {};
}

FString ResolveFbxDropInspectPath(const FString& PayloadPath)
{
    std::filesystem::path Path;
    std::filesystem::path RelativePath;
    if (!ResolveProjectDropPath(PayloadPath, Path, RelativePath))
    {
        return {};
    }

    if (GetLowerExtension(Path) != L".fbx")
    {
        return {};
    }

    return FPaths::Normalize(FPaths::ToUtf8(Path.generic_wstring()));
}
}

bool FEditorMainPanel::SpawnStaticMeshFromContentPath(
    const FString& PayloadPath,
    int32 ViewportIndex,
    float LocalX,
    float LocalY
)
{
    if (!EditorEngine)
    {
        return false;
    }

    FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
    FEditorViewportClient* Client = Layout.GetViewportClient(ViewportIndex);
    if (!Client || !Client->AllowsEditorWorldControl())
    {
        return false;
    }

    const FString MeshLoadPath = ResolveStaticMeshDropLoadPath(PayloadPath);
    if (MeshLoadPath.empty())
    {
        PushFooterLog("Unsupported static mesh drop");
        return false;
    }

    UStaticMesh* Mesh = FResourceManager::Get().LoadStaticMesh(MeshLoadPath);
    if (!Mesh || !Mesh->HasValidMeshData())
    {
        PushFooterLog("Failed to load dropped static mesh");
        return false;
    }

    UWorld* World = EditorEngine->GetFocusedWorld();
    if (!World)
    {
        return false;
    }

    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
    if (!Actor)
    {
        return false;
    }

    Actor->InitDefaultComponents();
    Actor->SetActorLocation(FEditorMainPanelPlacementHelpers::ComputePlacementLocation(Client, LocalX, LocalY));
    if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Actor->GetRootComponent()))
    {
        StaticMeshComp->SetStaticMesh(Mesh);
    }
    RecordPlacedActor(EditorEngine, Actor, "Place Static Mesh");

	const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(Client->GetFocusedWorld());
    Layout.SetLastFocusedViewportIndex(ViewportIndex);
    Ctx->SelectionManager->Select(Actor);
    EditorEngine->GetSceneService().MarkDirty();
    PushFooterLog("StaticMesh actor placed from Content Browser");
    return true;
}

bool FEditorMainPanel::SpawnSkeletalMeshFromContentPath(
    const FString& PayloadPath,
    int32 ViewportIndex,
    float LocalX,
    float LocalY
)
{
    if (!EditorEngine)
    {
        return false;
    }

    FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
    FEditorViewportClient* Client = Layout.GetViewportClient(ViewportIndex);
    if (!Client || !Client->AllowsEditorWorldControl())
    {
        return false;
    }

    const FString MeshLoadPath = ResolveSkeletalMeshDropLoadPath(PayloadPath);
    if (MeshLoadPath.empty())
    {
        return false;
    }

    FResourceManager& ResourceManager = FResourceManager::Get();
    USkeletalMesh* Mesh = nullptr;
    const FString InspectPath = ResolveFbxDropInspectPath(PayloadPath);
    if (InspectPath.empty())
    {
        Mesh = ResourceManager.LoadSkeletalMesh(MeshLoadPath);
    }
    else
    {
        const FFbxMeshContentInfo ContentInfo = ResourceManager.InspectFbxMeshContent(InspectPath);
        if (!ContentInfo.bHasSkeletalMesh)
        {
            Mesh = ResourceManager.LoadSkeletalMesh(MeshLoadPath);
            if (!Mesh || !Mesh->HasValidMeshData())
            {
                return false;
            }
        }
        else
        {
            const TArray<FString> ImportedAnimSequencePaths = ResourceManager.ImportAnimationStacksFromFbx(InspectPath);
            if (!ImportedAnimSequencePaths.empty())
            {
                Widgets.ContentBrowserWidget.Refresh();
            }
            Mesh = ResourceManager.LoadSkeletalMesh(MeshLoadPath);
        }
    }

	if (!Mesh || !Mesh->HasValidMeshData())
    {
        PushFooterLog("Failed to load dropped skeletal mesh");
        return false;
    }

    UWorld* World = EditorEngine->GetFocusedWorld();
    if (!World)
    {
        return false;
    }

    ASkeletalMeshActor* Actor = World->SpawnActor<ASkeletalMeshActor>();
    if (!Actor)
    {
        return false;
    }

    Actor->InitDefaultComponents();
    Actor->SetActorLocation(FEditorMainPanelPlacementHelpers::ComputePlacementLocation(Client, LocalX, LocalY));
    if (USkeletalMeshComponent* SkeletalMeshComp = Actor->GetSkeletalMeshComponent())
    {
        SkeletalMeshComp->SetSkeletalMesh(Mesh);
    }
    RecordPlacedActor(EditorEngine, Actor, "Place Skeletal Mesh");

    const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(Client->GetFocusedWorld());
    Layout.SetLastFocusedViewportIndex(ViewportIndex);
    if (Ctx && Ctx->SelectionManager)
    {
        Ctx->SelectionManager->Select(Actor);
    }
    EditorEngine->GetSceneService().MarkDirty();
    PushFooterLog("SkeletalMesh actor placed from Content Browser");
    return true;
}

bool FEditorMainPanel::SpawnPrefabFromContentPath(
    const FString& PayloadPath,
    int32 ViewportIndex,
    float LocalX,
    float LocalY
)
{
    if (!EditorEngine)
    {
        return false;
    }

    FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
    FEditorViewportClient* Client = Layout.GetViewportClient(ViewportIndex);
    if (!Client || !Client->AllowsEditorWorldControl())
    {
        return false;
    }

    UWorld* World = EditorEngine->GetFocusedWorld();
    if (!World)
    {
        return false;
    }

    std::filesystem::path PrefabPath;
    std::error_code PrefabEc;
    if (!FPrefabManager::ResolvePrefabPath(PayloadPath, PrefabPath, false) ||
        !std::filesystem::is_regular_file(PrefabPath, PrefabEc))
    {
        PushFooterLog("Failed to find prefab");
        return false;
    }

    AActor* Actor = FPrefabManager::SpawnActorFromPrefab(World, PayloadPath);
    if (!Actor)
    {
        PushFooterLog("Failed to spawn prefab");
        return false;
    }

    Actor->SetActorLocation(FEditorMainPanelPlacementHelpers::ComputePlacementLocation(Client, LocalX, LocalY));
    World->SyncSpatialIndex();
    RecordPlacedActor(EditorEngine, Actor, "Place Prefab");
    Layout.SetLastFocusedViewportIndex(ViewportIndex);
    const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(Client->GetFocusedWorld());
    Ctx->SelectionManager->Select(Actor);
    EditorEngine->GetSceneService().MarkDirty();
    PushFooterLog("Prefab actor placed from Content Browser");
    return true;
}

bool FEditorMainPanel::SpawnParticleSystemFromContentPath(const FString& PayloadPath, int32 ViewportIndex, float LocalX, float LocalY)
{
    if (!EditorEngine)
    {
        return false;
    }

    FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
    FEditorViewportClient* Client = Layout.GetViewportClient(ViewportIndex);
    if (!Client || !Client->AllowsEditorWorldControl())
    {
        return false;
    }

    const FString ParticleSystemPath = ResolveParticleSystemDropLoadPath(PayloadPath);
    if (ParticleSystemPath.empty())
    {
        PushFooterLog("Unsupported particle system drop");
        return false;
    }

    UParticleSystem* ParticleSystem = FResourceManager::Get().LoadParticleSystem(ParticleSystemPath);
    if (!ParticleSystem)
    {
        PushFooterLog("Failed to load dropped particle system");
        return false;
    }

    TArray<FString> Errors;
    if (!ParticleSystem->Validate(&Errors))
    {
        PushFooterLog("Dropped particle system is invalid");
        return false;
    }

    UWorld* World = EditorEngine->GetFocusedWorld();
    if (!World)
    {
        return false;
    }

    AParticleSystemActor* Actor = World->SpawnActor<AParticleSystemActor>();
    if (!Actor)
    {
        return false;
    }

    Actor->InitDefaultComponents();
    Actor->SetFName(FName("ParticleSystemActor"));
    Actor->SetTemplate(ParticleSystem);

    Actor->SetActorLocation(
        FEditorMainPanelPlacementHelpers::ComputePlacementLocation(Client, LocalX, LocalY));

    World->SyncSpatialIndex();
    RecordPlacedActor(EditorEngine, Actor, "Place Particle System");
    Layout.SetLastFocusedViewportIndex(ViewportIndex);

    const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(Client->GetFocusedWorld());
    if (Ctx && Ctx->SelectionManager)
    {
        Ctx->SelectionManager->Select(Actor);
    }

    EditorEngine->GetSceneService().MarkDirty();
    PushFooterLog("Particle system actor placed from Content Browser");
    return true;
}

void FEditorMainPanel::StartPendingAssetDrop(const FString& PayloadPath, int32 ViewportIndex, float LocalX, float LocalY)
{
    if (!EditorEngine)
    {
        return;
    }
    if (PendingAssetDropState.bActive)
    {
        EditorEngine->GetNotificationService().Warning("Another asset import is already running.");
        return;
    }

    PendingAssetDropState.Reset();
    PendingAssetDropState.bActive = true;
    PendingAssetDropState.PayloadPath = PayloadPath;
    PendingAssetDropState.ViewportIndex = ViewportIndex;
    PendingAssetDropState.LocalX = LocalX;
    PendingAssetDropState.LocalY = LocalY;
    PendingAssetDropState.ToastHandle = EditorEngine->GetNotificationService().BeginTask(
        "Loading Asset",
        "Queued asset import...",
        0.05f);
}

void FEditorMainPanel::TickPendingAssetDrop()
{
    if (!EditorEngine || !PendingAssetDropState.bActive)
    {
        return;
    }

    auto& Notifications = EditorEngine->GetNotificationService();
    if (!PendingAssetDropState.bStepPrepared)
    {
        Notifications.UpdateTask(
            PendingAssetDropState.ToastHandle,
            "Importing dropped asset... Editor may pause briefly.",
            0.35f);
        PendingAssetDropState.bStepPrepared = true;
        return;
    }

    const FString PayloadPath = PendingAssetDropState.PayloadPath;
    const int32 ViewportIndex = PendingAssetDropState.ViewportIndex;
    const float LocalX = PendingAssetDropState.LocalX;
    const float LocalY = PendingAssetDropState.LocalY;
    const FEditorNotificationHandle ToastHandle = PendingAssetDropState.ToastHandle;
    PendingAssetDropState.Reset();

    bool bPlaced = SpawnSkeletalMeshFromContentPath(PayloadPath, ViewportIndex, LocalX, LocalY);
    if (!bPlaced)
    {
        bPlaced = SpawnStaticMeshFromContentPath(PayloadPath, ViewportIndex, LocalX, LocalY);
    }

    Notifications.FinishTask(
        ToastHandle,
        bPlaced ? EEditorNotificationType::Info : EEditorNotificationType::Error,
        bPlaced ? "Asset imported and placed." : "Failed to import dropped asset.");
}

bool FEditorMainPanel::SpawnPrefabAtOrigin(const FString& PayloadPath)
{
    if (!EditorEngine)
    {
        return false;
    }

    UWorld* World = EditorEngine->GetFocusedWorld();
    if (!World)
    {
        return false;
    }

    std::filesystem::path PrefabPath;
    std::error_code PrefabEc;
    if (!FPrefabManager::ResolvePrefabPath(PayloadPath, PrefabPath, false) ||
        !std::filesystem::is_regular_file(PrefabPath, PrefabEc))
    {
        PushFooterLog("Failed to find prefab");
        return false;
    }

    AActor* Actor = FPrefabManager::SpawnActorFromPrefab(World, PayloadPath);
    if (!Actor)
    {
        PushFooterLog("Failed to spawn prefab");
        return false;
    }

    Actor->SetActorLocation(FVector::ZeroVector);
    World->SyncSpatialIndex();
    RecordPlacedActor(EditorEngine, Actor, "Place Prefab");
    const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(World);
    Ctx->SelectionManager->Select(Actor);
    EditorEngine->GetSceneService().MarkDirty();
    PushFooterLog("Prefab actor placed at origin");
    return true;
}

void FEditorMainPanel::HandleContentBrowserViewportDrop()
{
    FString PayloadType;
    FString PayloadPath;
    if (!Widgets.ContentBrowserWidget.ConsumeReleasedDragPayload(PayloadType, PayloadPath))
    {
        return;
    }
    if ((PayloadType != "ObjectContentItem" &&
         PayloadType != "PrefabContentItem" &&
         PayloadType != "ParticleSystemContentItem") ||
        Widgets.ContentBrowserWidget.IsMouseOverBrowser())
    {
        return;
    }

    POINT MouseClientPos = {};
    if (Window)
    {
        MouseClientPos = Window->ScreenToClientPoint(InputSystem::Get().GetMousePos());
    }
    else
    {
        const ImVec2 MousePos = ImGui::GetIO().MousePos;
        MouseClientPos = POINT{
            static_cast<LONG>(MousePos.x),
            static_cast<LONG>(MousePos.y)
        };
    }
    FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
    const int32 FocusedViewportIndex = Layout.GetLastFocusedViewportIndex();
    auto TryDropOnViewport = [&](int32 ViewportIndex) -> bool
    {
        FEditorViewportClient* Client = Layout.GetViewportClient(ViewportIndex);
        if (!Client || !Client->AllowsEditorWorldControl())
        {
            return false;
        }

        const FViewportRect& Rect = Layout.GetSceneViewport(ViewportIndex).GetRect();
        if (Rect.Width <= 0 || Rect.Height <= 0)
        {
            return false;
        }
        if (MouseClientPos.x < Rect.X ||
            MouseClientPos.x >= Rect.X + Rect.Width ||
            MouseClientPos.y < Rect.Y ||
            MouseClientPos.y >= Rect.Y + Rect.Height)
        {
            return false;
        }

        const float LocalX = MathUtil::Clamp(
            static_cast<float>(MouseClientPos.x - Rect.X),
            0.0f,
            std::max(0.0f, static_cast<float>(Rect.Width - 1))
        );
        const float LocalY = MathUtil::Clamp(
            static_cast<float>(MouseClientPos.y - Rect.Y),
            0.0f,
            std::max(0.0f, static_cast<float>(Rect.Height - 1))
        );
        if (PayloadType == "PrefabContentItem")
        {
            return SpawnPrefabFromContentPath(PayloadPath, ViewportIndex, LocalX, LocalY);
        }
        if (PayloadType == "ParticleSystemContentItem")
        {
            return SpawnParticleSystemFromContentPath(PayloadPath, ViewportIndex, LocalX, LocalY);
        }
        if (PendingAssetDropState.bActive)
        {
            EditorEngine->GetNotificationService().Warning("Another asset import is already running.");
            return false;
        }
        StartPendingAssetDrop(PayloadPath, ViewportIndex, LocalX, LocalY);
        return true;
    };

    if (FocusedViewportIndex >= 0 &&
        FocusedViewportIndex < FEditorViewportLayout::MaxViewports &&
        TryDropOnViewport(FocusedViewportIndex))
    {
        return;
    }
    for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
    {
        if (i != FocusedViewportIndex && TryDropOnViewport(i))
        {
            return;
        }
    }
}
