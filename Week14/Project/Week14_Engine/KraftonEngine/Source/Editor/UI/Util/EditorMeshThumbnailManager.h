#pragma once

#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"
#include "Object/FName.h"
#include "Viewport/Asset/StaticMeshEditorViewportClient.h"

#include <d3d11.h>
#include <memory>

class AActor;
class IEditorPreviewViewportClient;
class UStaticMeshComponent;
class UWorld;

enum class EMeshThumbnailType
{
	StaticMesh,
};

struct FMeshThumbnailEntry
{
	FString AssetPath;
	EMeshThumbnailType Type = EMeshThumbnailType::StaticMesh;

	FName PreviewWorldHandle;
	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;
	UStaticMeshComponent* PreviewMeshComponent = nullptr;
	std::unique_ptr<FStaticMeshEditorViewportClient> Viewport;

	bool bRequested = false;
	bool bReady = false;
	bool bFailed = false;
	int32 WarmupFrames = 2;
};

class FEditorMeshThumbnailManager : public TSingleton<FEditorMeshThumbnailManager>
{
public:
	void Initialize(ID3D11Device* InDevice);
	void Shutdown();

	ID3D11ShaderResourceView* GetOrRequestThumbnail(const FString& AssetPath, EMeshThumbnailType Type);
	void Tick(float DeltaTime);

	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const;
	void ClearThumbnails();

private:
	FMeshThumbnailEntry* FindOrCreateEntry(const FString& AssetPath, EMeshThumbnailType Type);
	void BuildPreviewScene(FMeshThumbnailEntry& Entry);
	void ReleaseEntry(FMeshThumbnailEntry& Entry);

	static FString MakeKey(const FString& AssetPath, EMeshThumbnailType Type);

private:
	ID3D11Device* Device = nullptr;
	TMap<FString, std::unique_ptr<FMeshThumbnailEntry>> Entries;
	TArray<FString> PendingRequests;
	uint32 NextPreviewWorldSerial = 0;
};
