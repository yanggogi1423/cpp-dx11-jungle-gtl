#pragma once

#include "Core/CoreMinimal.h"
#include "Asset/CurveFloatAsset.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Editor/Packaging/GameBuildSettings.h"
#include "Object/FName.h"
#include "Render/Resource/Material.h"
#include "SimpleJSON/json.hpp"

#include <cstddef>
#include <memory>

class UEditorEngine;
class UObject;
class AActor;
class UActorComponent;
class USceneComponent;
class UMovementComponent;
class UPrimitiveComponent;
class UMaterialInterface;
class USkeletalMeshComponent;
class UCurveFloatAsset;
class USkeletalMesh;
class UWorld;

struct FEditorObjectRef
{
	FName WorldHandle = FName::None;
	FGuid ActorGuid;
	FGuid ComponentGuid;
	FString ObjectPath;

	bool HasActor() const { return ActorGuid.IsValid(); }
	bool HasComponent() const { return ComponentGuid.IsValid(); }
	bool IsValid() const { return WorldHandle != FName::None && ActorGuid.IsValid(); }
};

struct FEditorAssetRef
{
	FString AssetPath;
	FString AssetType;
	FGuid ObjectGuid;

	bool IsValid() const { return !AssetPath.empty(); }
};

struct FActorSequenceObjectRef
{
	FEditorObjectRef OwnerComponentRef;
	FGuid SequenceGuid;
	FGuid BindingGuid;
	FGuid TrackGuid;
	FGuid SectionGuid;
	FString ChannelName;
	FGuid KeyGuid;

	bool IsValid() const { return OwnerComponentRef.IsValid(); }
};

struct FEditorActorTransformState
{
	FEditorObjectRef ActorRef;
	FVector Location = FVector::ZeroVector;
	FVector Rotation = FVector::ZeroVector;
	FVector Scale = FVector(1.0f, 1.0f, 1.0f);

	bool IsValid() const { return ActorRef.IsValid(); }
};

struct FEditorObjectTagsState
{
	FEditorObjectRef ObjectRef;
	TArray<FString> Tags;

	bool IsValid() const { return ObjectRef.IsValid(); }
};

struct FEditorObjectState
{
	FEditorObjectRef ObjectRef;
	json::JSON Data;
	FString Label;

	bool IsValid() const { return ObjectRef.IsValid(); }
};

struct FEditorSkeletalBonePoseState
{
	FEditorObjectRef ComponentRef;
	TArray<int32> BoneIndices;
	TArray<FMatrix> LocalTransforms;

	bool IsValid() const
	{
		return ComponentRef.IsValid()
			&& ComponentRef.ComponentGuid.IsValid()
			&& BoneIndices.size() == LocalTransforms.size();
	}
};

struct FEditorCurveAssetState
{
	FEditorAssetRef AssetRef;
	FFloatCurve Curve;
	FString Label;

	bool IsValid() const { return AssetRef.IsValid(); }
};

struct FEditorMaterialState
{
	FEditorAssetRef AssetRef;
	TMap<FString, FMaterialParamValue> Params;
	FString Label;

	bool IsValid() const { return AssetRef.IsValid(); }
};

struct FEditorSkeletalMeshSocketState
{
	FEditorAssetRef AssetRef;
	TArray<FSkeletalMeshSocket> Sockets;
	FString Label;

	bool IsValid() const { return AssetRef.IsValid(); }
};

struct FEditorProjectSettingsState
{
	FString SettingsPath;
	FGameBuildSettings BuildSettings;
	FString LastScenePath;
	FString Label;

	bool IsValid() const { return !SettingsPath.empty(); }
};

struct FEditorWorldGameModeSettingsState
{
	FName WorldHandle = FName::None;
	bool bOverrideGameMode = false;
	FString GameModeClass = "AGameModeBase";
	FString PlayerControllerClass = "APlayerController";
	FString DefaultPawnClass = "ADefaultPawn";
	FString DefaultPawnPrefabPath;
	FString Label;

	bool IsValid() const { return WorldHandle != FName::None; }
};

struct FEditorFileSystemEntryState
{
	FString Path;
	bool bDirectory = false;
	TArray<uint8> Data;
};

struct FEditorFileSystemState
{
	FString RootPath;
	TArray<FEditorFileSystemEntryState> Entries;
	FString Label;

	bool IsValid() const { return !RootPath.empty(); }
};

class FEditorUndoObjectResolver
{
public:
	static FEditorObjectRef MakeActorRef(UEditorEngine* Editor, AActor* Actor);
	static FEditorObjectRef MakeComponentRef(UEditorEngine* Editor, UActorComponent* Component);
	static AActor* ResolveActor(UEditorEngine* Editor, const FEditorObjectRef& Ref);
	static UActorComponent* ResolveComponent(UEditorEngine* Editor, const FEditorObjectRef& Ref);
	static UObject* ResolveObject(UEditorEngine* Editor, const FEditorObjectRef& Ref);
};

struct FEditorUndoContext
{
	UEditorEngine* Editor = nullptr;

	AActor* ResolveActor(const FEditorObjectRef& Ref) const;
	UActorComponent* ResolveComponent(const FEditorObjectRef& Ref) const;
	UObject* ResolveObject(const FEditorObjectRef& Ref) const;
};

class IEditorUndoCommand
{
public:
	virtual ~IEditorUndoCommand() = default;

	virtual FString GetLabel() const = 0;
	virtual bool Undo(FEditorUndoContext& Context) = 0;
	virtual bool Redo(FEditorUndoContext& Context) = 0;
	virtual size_t GetMemoryUsage() const = 0;
};

struct FEditorTransaction
{
	FString Label;
	TArray<std::unique_ptr<IEditorUndoCommand>> Commands;
	size_t CachedMemoryUsage = 0;

	bool Undo(FEditorUndoContext& Context);
	bool Redo(FEditorUndoContext& Context);
	bool IsEmpty() const { return Commands.empty(); }
	size_t GetMemoryUsage() const;
	int32 GetCommandCount() const { return static_cast<int32>(Commands.size()); }
};

struct FUndoHistoryStats
{
	int32 UndoCount = 0;
	int32 RedoCount = 0;
	int32 MaxEntries = 0;
	size_t LogicalBytes = 0;
	size_t ReservedBytes = 0;
	size_t EntryOverheadBytes = 0;
	size_t ApproxTotalBytes = 0;
};

class FEditorUndoSystem
{
public:
	void BeginTransaction(const FString& Label);
	bool AddCommand(std::unique_ptr<IEditorUndoCommand> Command);
	bool EndTransaction();
	void CancelTransaction();

	bool Undo();
	bool Redo();
	bool RestoreHistoryIndex(int32 Index);
	TArray<FEditorActorTransformState> CaptureActorTransforms(const TArray<AActor*>& Actors) const;
	bool RecordActorTransforms(
		const TArray<FEditorActorTransformState>& BeforeStates,
		const TArray<FEditorActorTransformState>& AfterStates);
	TArray<FEditorObjectTagsState> CaptureObjectTags(const TArray<UObject*>& Objects) const;
	bool RecordObjectTags(
		const TArray<FEditorObjectTagsState>& BeforeStates,
		const TArray<FEditorObjectTagsState>& AfterStates);
	FEditorObjectState CaptureObjectState(UObject* Object, const FString& Label = FString()) const;
	TArray<FEditorObjectState> CaptureObjectStates(const TArray<UObject*>& Objects, const FString& Label = FString()) const;
	bool RecordObjectState(
		const FEditorObjectState& BeforeState,
		const FEditorObjectState& AfterState,
		const FString& Label = "Edit Property");
	bool RecordObjectStates(
		const TArray<FEditorObjectState>& BeforeStates,
		const TArray<FEditorObjectState>& AfterStates,
		const FString& Label = "Edit Property");
	FEditorSkeletalBonePoseState CaptureSkeletalBonePose(
		USkeletalMeshComponent* Component,
		int32 BoneIndex = -1) const;
	bool RecordSkeletalBonePose(
		const FEditorSkeletalBonePoseState& BeforeState,
		const FEditorSkeletalBonePoseState& AfterState,
		const FString& Label = "Edit Bone Pose");
	FEditorCurveAssetState CaptureCurveAssetState(
		UCurveFloatAsset* Curve,
		const FString& AssetPath,
		const FString& Label = FString()) const;
	bool RecordCurveAssetState(
		const FEditorCurveAssetState& BeforeState,
		const FEditorCurveAssetState& AfterState,
		const FString& Label = "Edit Curve");
	FEditorMaterialState CaptureMaterialState(
		UMaterialInterface* Material,
		const FString& Label = FString()) const;
	bool RecordMaterialState(
		const FEditorMaterialState& BeforeState,
		const FEditorMaterialState& AfterState,
		const FString& Label = "Edit Material");
	FEditorSkeletalMeshSocketState CaptureSkeletalMeshSocketState(
		USkeletalMesh* Mesh,
		const FString& Label = FString()) const;
	bool RecordSkeletalMeshSocketState(
		const FEditorSkeletalMeshSocketState& BeforeState,
		const FEditorSkeletalMeshSocketState& AfterState,
		const FString& Label = "Edit Skeletal Mesh Socket");
	FEditorProjectSettingsState CaptureProjectSettings(const FString& Label = FString()) const;
	bool RecordProjectSettings(
		const FEditorProjectSettingsState& BeforeState,
		const FEditorProjectSettingsState& AfterState,
		const FString& Label = "Edit Project Settings");
	FEditorWorldGameModeSettingsState CaptureWorldGameModeSettings(
		UWorld* World,
		const FString& Label = FString()) const;
	bool RecordWorldGameModeSettings(
		const FEditorWorldGameModeSettingsState& BeforeState,
		const FEditorWorldGameModeSettingsState& AfterState,
		const FString& Label = "Edit World Settings");
	FEditorFileSystemState CaptureFileSystemState(
		const FString& Path,
		const FString& Label = FString()) const;
	bool RecordCreateFileSystemPath(
		const FEditorFileSystemState& AfterState,
		const FString& Label = "Create Asset");
	bool RecordDeleteFileSystemPath(
		const FEditorFileSystemState& BeforeState,
		const FString& Label = "Delete Asset");
	bool RecordRenameFileSystemPath(
		const FString& OldPath,
		const FString& NewPath,
		const FString& Label = "Rename Asset");
	bool RecordObjectRename(UObject* Object, const FName& OldName, const FName& NewName);
	bool RecordCreateActors(const TArray<AActor*>& Actors);
	bool RecordDeleteActors(const TArray<AActor*>& Actors);
	bool RecordCreateComponents(const TArray<UActorComponent*>& Components);
	bool RecordDeleteComponents(const TArray<UActorComponent*>& Components);
	bool RecordSceneComponentAttachment(
		USceneComponent* Component,
		USceneComponent* OldParent,
		USceneComponent* NewParent,
		const FName& OldSocketName,
		const FName& NewSocketName);
	bool RecordMovementUpdatedComponent(
		UMovementComponent* Component,
		USceneComponent* OldUpdatedComponent,
		USceneComponent* NewUpdatedComponent);
	bool RecordMaterialSlot(
		UPrimitiveComponent* Component,
		int32 SlotIndex,
		UMaterialInterface* OldMaterial,
		UMaterialInterface* NewMaterial);
	void ClearHistory();
	void ClearHistory(const FName& WorldHandle);
	void ClearAllHistory();

	bool CanUndo() const { return !UndoStack.empty(); }
	bool CanRedo() const { return !RedoStack.empty(); }
	bool IsApplyingUndoRedo() const { return bApplyingUndoRedo; }
	bool HasActiveTransaction() const { return ActiveTransaction != nullptr; }
	bool HasPendingMutationCapture() const { return bPendingMutationCapture; }
	bool IsMutationTrackingActive() const { return bApplyingUndoRedo || ActiveTransaction != nullptr || bPendingMutationCapture; }
	uint64 GetTransactionRevision() const { return TransactionRevision; }

	bool IsRestoring() const { return bApplyingUndoRedo; }
	void BeginRestore() { bApplyingUndoRedo = true; }
	void EndRestore() { bApplyingUndoRedo = false; }

	const TArray<FEditorTransaction>& GetUndoHistory() const { return UndoStack; }
	const TArray<FEditorTransaction>& GetRedoHistory() const { return RedoStack; }
	FUndoHistoryStats GetStats() const;

private:
	friend class UEditorEngine;

	void SetOwner(UEditorEngine* InOwner) { Owner = InOwner; }
	FEditorUndoContext MakeContext() const;
	void NoteMutationCapture() const { bPendingMutationCapture = true; }
	void PushUndoTransaction(FEditorTransaction Transaction);
	void PushRedoTransaction(FEditorTransaction Transaction);
	void TrimHistoryToLimits();
	size_t GetUndoRedoMemoryUsage() const;

private:
	UEditorEngine* Owner = nullptr;
	TArray<FEditorTransaction> UndoStack;
	TArray<FEditorTransaction> RedoStack;
	std::unique_ptr<FEditorTransaction> ActiveTransaction;
	bool bApplyingUndoRedo = false;
	mutable bool bPendingMutationCapture = false;
	uint64 TransactionRevision = 0;

	static constexpr int32 MaxUndoHistory = 200;
	static constexpr size_t MaxUndoMemoryBytes = 128ull * 1024ull * 1024ull;
};

class FScopedEditorTransaction
{
public:
	FScopedEditorTransaction(FEditorUndoSystem& InUndoSystem, const FString& Label);
	~FScopedEditorTransaction();

	void Cancel();

private:
	FEditorUndoSystem& UndoSystem;
	bool bCancelled = false;
};
