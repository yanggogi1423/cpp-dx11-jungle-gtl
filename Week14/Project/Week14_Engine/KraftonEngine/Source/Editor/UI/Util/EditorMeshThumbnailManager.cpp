#include "Editor/UI/Util/EditorMeshThumbnailManager.h"

#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/World.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Render/Scene/FScene.h"
#include "Runtime/Engine.h"
#include "Viewport/Asset/StaticMeshEditorViewportClient.h"
#include "Viewport/Viewport.h"

void FEditorMeshThumbnailManager::Initialize(ID3D11Device* InDevice)
{
	Device = InDevice;
}

void FEditorMeshThumbnailManager::Shutdown()
{
	ClearThumbnails();
	Device = nullptr;
}

ID3D11ShaderResourceView* FEditorMeshThumbnailManager::GetOrRequestThumbnail(
	const FString& AssetPath,
	EMeshThumbnailType Type)
{
	FMeshThumbnailEntry* Entry = FindOrCreateEntry(AssetPath, Type);
	if (!Entry || Entry->bFailed)
	{
		return nullptr;
	}

	if (!Entry->bRequested)
	{
		Entry->bRequested = true;
		PendingRequests.push_back(MakeKey(AssetPath, Type));
	}

	FViewport* Viewport = Entry->Viewport ? Entry->Viewport->GetViewport() : nullptr;
	return Entry->bReady && Viewport ? Viewport->GetSRV() : nullptr;
}

void FEditorMeshThumbnailManager::Tick(float DeltaTime)
{
	constexpr int32 MaxBuildPerFrame = 1;

	int32 BuiltCount = 0;
	while (!PendingRequests.empty() && BuiltCount < MaxBuildPerFrame)
	{
		const FString Key = PendingRequests.front();
		PendingRequests.erase(PendingRequests.begin());

		auto It = Entries.find(Key);
		if (It == Entries.end())
		{
			continue;
		}

		FMeshThumbnailEntry& Entry = *It->second;
		if (!Entry.PreviewWorld && !Entry.bFailed)
		{
			BuildPreviewScene(Entry);
			++BuiltCount;
		}
	}

	for (auto& Pair : Entries)
	{
		FMeshThumbnailEntry& Entry = *Pair.second;
		if (Entry.bFailed || !Entry.PreviewWorld || Entry.bReady)
		{
			continue;
		}

		if (Entry.Viewport && Entry.Viewport->IsRenderable())
		{
			Entry.Viewport->Tick(DeltaTime);
		}

		if (Entry.WarmupFrames > 0)
		{
			--Entry.WarmupFrames;
		}
		else
		{
			Entry.bReady = true;
		}
	}
}

void FEditorMeshThumbnailManager::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	for (const auto& Pair : Entries)
	{
		const FMeshThumbnailEntry& Entry = *Pair.second;
		if (Entry.bReady || Entry.bFailed)
		{
			continue;
		}

		if (Entry.Viewport && Entry.Viewport->IsRenderable())
		{
			OutClients.push_back(Entry.Viewport.get());
		}
	}
}

void FEditorMeshThumbnailManager::ClearThumbnails()
{
	for (auto& Pair : Entries)
	{
		ReleaseEntry(*Pair.second);
	}

	Entries.clear();
	PendingRequests.clear();
}

FMeshThumbnailEntry* FEditorMeshThumbnailManager::FindOrCreateEntry(const FString& AssetPath, EMeshThumbnailType Type)
{
	const FString Key = MakeKey(AssetPath, Type);
	if (auto It = Entries.find(Key); It != Entries.end())
	{
		return It->second.get();
	}

	auto NewEntry = std::make_unique<FMeshThumbnailEntry>();
	NewEntry->AssetPath = AssetPath;
	NewEntry->Type = Type;
	NewEntry->PreviewWorldHandle = FName("MeshThumbnail_" + std::to_string(NextPreviewWorldSerial++));

	FMeshThumbnailEntry* RawEntry = NewEntry.get();
	Entries[Key] = std::move(NewEntry);
	return RawEntry;
}

void FEditorMeshThumbnailManager::BuildPreviewScene(FMeshThumbnailEntry& Entry)
{
	if (!Device || !GEngine)
	{
		Entry.bFailed = true;
		return;
	}

	if (Entry.Type != EMeshThumbnailType::StaticMesh)
	{
		Entry.bFailed = true;
		return;
	}

	UStaticMesh* Mesh = FMeshManager::LoadStaticMesh(Entry.AssetPath, Device);
	if (!Mesh)
	{
		Entry.bFailed = true;
		return;
	}

	FWorldContext& WorldContext =
		GEngine->CreateWorldContext(EWorldType::EditorPreview, Entry.PreviewWorldHandle);

	Entry.PreviewWorld = WorldContext.World;
	if (!Entry.PreviewWorld)
	{
		Entry.bFailed = true;
		return;
	}

	Entry.PreviewWorld->SetWorldType(EWorldType::EditorPreview);
	Entry.PreviewWorld->InitWorld();

	Entry.PreviewActor = Entry.PreviewWorld->SpawnActor<AActor>();
	if (!Entry.PreviewActor)
	{
		Entry.bFailed = true;
		return;
	}

	UStaticMeshComponent* MeshComponent = Entry.PreviewActor->AddComponent<UStaticMeshComponent>();
	if (!MeshComponent)
	{
		Entry.bFailed = true;
		return;
	}

	MeshComponent->SetStaticMesh(Mesh);
	Entry.PreviewActor->SetRootComponent(MeshComponent);
	Entry.PreviewActor->SetActorLocation(FVector::ZeroVector);
	Entry.PreviewMeshComponent = MeshComponent;

	ADirectionalLightActor* LightActor = Entry.PreviewWorld->SpawnActor<ADirectionalLightActor>();
	if (LightActor)
	{
		LightActor->InitDefaultComponents();
		LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));

		if (UDirectionalLightComponent* LightComponent = LightActor->GetComponentByClass<UDirectionalLightComponent>())
		{
			LightComponent->SetShadowBias(0.0f);
			LightComponent->PushToScene();
		}
	}

	Entry.Viewport = std::make_unique<FStaticMeshEditorViewportClient>();
	Entry.Viewport->Initialize(Device, 128, 128);
	Entry.Viewport->SetPreviewWorld(Entry.PreviewWorld);
	Entry.Viewport->SetPreviewActor(Entry.PreviewActor);
	Entry.Viewport->SetPreviewMeshComponent(MeshComponent);
	Entry.Viewport->ResetCameraToPreviewBounds();

	Entry.PreviewWorld->SetEditorPOVProvider(Entry.Viewport.get());
	Entry.WarmupFrames = 2;
	Entry.bReady = false;
}

void FEditorMeshThumbnailManager::ReleaseEntry(FMeshThumbnailEntry& Entry)
{
	if (Entry.Viewport)
	{
		Entry.Viewport->Release();
		Entry.Viewport.reset();
	}

	if (Entry.PreviewWorld && Entry.PreviewWorldHandle.IsValid() && GEngine)
	{
		FScene& PreviewScene = Entry.PreviewWorld->GetScene();
		GEngine->GetRenderer().GetResources().ReleaseShadowResourcesForScene(&PreviewScene);
		GEngine->DestroyWorldContext(Entry.PreviewWorldHandle);
	}

	Entry.PreviewWorld = nullptr;
	Entry.PreviewActor = nullptr;
	Entry.PreviewMeshComponent = nullptr;
	Entry.bReady = false;
}

FString FEditorMeshThumbnailManager::MakeKey(const FString& AssetPath, EMeshThumbnailType Type)
{
	return AssetPath + (Type == EMeshThumbnailType::StaticMesh ? "#StaticMesh" : "#Unknown");
}
