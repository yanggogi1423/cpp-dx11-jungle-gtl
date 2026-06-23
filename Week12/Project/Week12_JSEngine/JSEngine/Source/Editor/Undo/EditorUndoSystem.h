#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Guid.h"
#include "Editor/Packaging/GameBuildSettings.h"
#include "Object/FName.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Asset/CurveFloatAsset.h"
#include "Render/Resource/Material.h"

#include <cstddef>
#include <memory>

class UEditorEngine;
class AActor;
class UActorComponent;
class USceneComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UObject;
class UCurveFloatAsset;
class UMaterialInterface;
class UWorld;
struct FProperty;

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

struct FEditorActorTransformState
{
	FEditorObjectRef ActorRef;
	FVector Location = FVector::ZeroVector;
	FVector Rotation = FVector::ZeroVector;
	FVector Scale = FVector(1.0f, 1.0f, 1.0f);

	bool IsValid() const { return ActorRef.IsValid(); }
};

struct FEditorSerializedActorState
{
	FEditorObjectRef ActorRef;
	FString ActorJson;

	bool IsValid() const { return ActorRef.IsValid() && !ActorJson.empty(); }
};

struct FEditorSceneComponentTransformState
{
	FEditorObjectRef ComponentRef;
	FVector RelativeLocation = FVector::ZeroVector;
	FVector RelativeRotation = FVector::ZeroVector;
	FVector RelativeScale = FVector(1.0f, 1.0f, 1.0f);

	bool IsValid() const { return ComponentRef.IsValid() && ComponentRef.HasComponent(); }
};

struct FEditorSkeletalBonePoseState
{
	FEditorObjectRef ComponentRef;
	TArray<int32> BoneIndices;
	TArray<FMatrix> LocalTransforms;

	bool IsValid() const
	{
		return ComponentRef.IsValid()
			&& ComponentRef.HasComponent()
			&& BoneIndices.size() == LocalTransforms.size()
			&& !BoneIndices.empty();
	}
};

struct FEditorSkeletalMeshSocketState
{
	FString MeshPath;
	TArray<FSkeletalMeshSocket> Sockets;
	FString Label;

	bool IsValid() const { return !MeshPath.empty(); }
};

struct FEditorProjectSettingsState
{
	FString SettingsPath;
	FGameBuildSettings BuildSettings;
	FString LastScenePath;
	FString Label;

	bool IsValid() const { return !SettingsPath.empty(); }
};

struct FEditorAssetRef
{
	FString AssetPath;
	FString AssetType;
	FGuid ObjectGuid;

	bool IsValid() const { return !AssetPath.empty(); }
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

struct FEditorWorldGameModeSettingsState
{
	FName WorldHandle = FName::None;
	bool bOverrideGameMode = false;
	FString GameModeClass;
	FString PlayerControllerClass;
	FString DefaultPawnClass;
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

struct FEditorReflectedPropertyState
{
	FEditorObjectRef ObjectRef;
	FString PropertyName;
	FString ValueJson;

	bool IsValid() const { return ObjectRef.IsValid() && !PropertyName.empty() && !ValueJson.empty(); }
};

class FEditorUndoObjectResolver
{
public:
	static FEditorObjectRef MakeActorRef(UEditorEngine* Editor, AActor* Actor);
	static AActor* ResolveActor(UEditorEngine* Editor, const FEditorObjectRef& Ref);
	static FEditorObjectRef MakeComponentRef(UEditorEngine* Editor, UActorComponent* Component);
	static UActorComponent* ResolveComponent(UEditorEngine* Editor, const FEditorObjectRef& Ref);
};

struct FEditorUndoContext
{
	UEditorEngine* Editor = nullptr;

	AActor* ResolveActor(const FEditorObjectRef& Ref) const;
	UActorComponent* ResolveComponent(const FEditorObjectRef& Ref) const;
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

struct FUndoSnapshotEntry
{
	FName WorldHandle;
	FString Label;
	FString Snapshot;
};

struct FWorldUndoHistory
{
	TArray<FUndoSnapshotEntry> UndoHistory;
	TArray<FUndoSnapshotEntry> RedoHistory;
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

	bool CaptureSnapshot(const char* Reason = nullptr);
	bool Undo();
	bool Redo();
	bool RestoreHistoryIndex(int32 Index);
	TArray<FEditorActorTransformState> CaptureActorTransforms(const TArray<AActor*>& Actors) const;
	bool RecordActorTransforms(
		const TArray<FEditorActorTransformState>& BeforeStates,
		const TArray<FEditorActorTransformState>& AfterStates);
	TArray<FEditorSerializedActorState> CaptureActorStates(const TArray<AActor*>& Actors) const;
	bool RecordActorCreation(
		const TArray<FEditorSerializedActorState>& CreatedStates,
		const FString& Label = "Create Actors");
	bool RecordActorDeletion(
		const TArray<FEditorSerializedActorState>& DeletedStates,
		const FString& Label = "Delete Actors");
	bool RecordActorStateChange(
		const TArray<FEditorSerializedActorState>& BeforeStates,
		const TArray<FEditorSerializedActorState>& AfterStates,
		const FString& Label = "Edit Actor");
	FEditorSceneComponentTransformState CaptureSceneComponentTransform(USceneComponent* Component) const;
	bool RecordSceneComponentTransform(
		const FEditorSceneComponentTransformState& BeforeState,
		const FEditorSceneComponentTransformState& AfterState,
		const FString& Label = "Transform Component");
	FEditorSkeletalBonePoseState CaptureSkeletalBonePose(
		USkeletalMeshComponent* Component,
		int32 BoneIndex = -1) const;
	bool RecordSkeletalBonePose(
		const FEditorSkeletalBonePoseState& BeforeState,
		const FEditorSkeletalBonePoseState& AfterState,
		const FString& Label = "Edit Bone Pose");
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
	FEditorCurveAssetState CaptureCurveAssetState(
		UCurveFloatAsset* Curve,
		const FString& AssetPath = FString(),
		const FString& Label = FString()) const;
	bool RecordCurveAssetState(
		const FEditorCurveAssetState& BeforeState,
		const FEditorCurveAssetState& AfterState,
		const FString& Label = "Edit Curve");
	FEditorMaterialState CaptureMaterialState(
		UMaterialInterface* Material,
		const FString& AssetPath = FString(),
		const FString& Label = FString()) const;
	bool RecordMaterialState(
		const FEditorMaterialState& BeforeState,
		const FEditorMaterialState& AfterState,
		const FString& Label = "Edit Material");
	FEditorWorldGameModeSettingsState CaptureWorldGameModeSettings(
		UWorld* World,
		const FString& Label = FString()) const;
	bool RecordWorldGameModeSettings(
		const FEditorWorldGameModeSettingsState& BeforeState,
		const FEditorWorldGameModeSettingsState& AfterState,
		const FString& Label = "Edit World GameMode Settings");
	FEditorFileSystemState CaptureFileSystemState(
		const FString& RootPath,
		const FString& Label = FString()) const;
	bool RecordCreateFileSystemPath(
		const FEditorFileSystemState& CreatedState,
		const FString& Label = "Create Content");
	bool RecordDeleteFileSystemPath(
		const FEditorFileSystemState& DeletedState,
		const FString& Label = "Delete Content");
	bool RecordRenameFileSystemPath(
		const FEditorFileSystemState& BeforeState,
		const FEditorFileSystemState& AfterState,
		const FString& Label = "Rename Content");
	FEditorReflectedPropertyState CaptureReflectedProperty(UObject* Object, const FProperty& Property) const;
	bool RecordReflectedProperty(
		const FEditorReflectedPropertyState& BeforeState,
		const FEditorReflectedPropertyState& AfterState,
		const FString& Label = "Edit Property");
	void ClearHistory();
	void ClearHistory(const FName& WorldHandle);
	void ClearAllHistory();

	bool CanUndo() const;
	bool CanRedo() const;
	bool IsApplyingUndoRedo() const { return bApplyingUndoRedo; }
	bool HasActiveTransaction() const { return ActiveTransaction != nullptr; }
	uint64 GetTransactionRevision() const { return TransactionRevision; }

	bool IsRestoring() const { return bRestoring || bApplyingUndoRedo; }
	void BeginRestore() { bRestoring = true; }
	void EndRestore() { bRestoring = false; }

	const TArray<FUndoSnapshotEntry>& GetUndoHistory() const;
	const TArray<FUndoSnapshotEntry>& GetRedoHistory() const;
	FUndoHistoryStats GetStats() const;

private:
	friend class UEditorEngine;

	void SetOwner(UEditorEngine* InOwner) { Owner = InOwner; }
	FEditorUndoContext MakeContext() const;
	FName GetActiveWorldHandle() const;
	FWorldUndoHistory* FindHistory(const FName& WorldHandle);
	const FWorldUndoHistory* FindHistory(const FName& WorldHandle) const;
	FWorldUndoHistory& GetOrCreateHistory(const FName& WorldHandle);
	bool PushSnapshot(const FName& WorldHandle, FString Snapshot, const char* Reason, bool& bOutClearedRedo);
	bool PopUndoSnapshot(const FName& WorldHandle, FString CurrentSnapshot, FUndoSnapshotEntry& OutEntry);
	bool PopRedoSnapshot(const FName& WorldHandle, FString CurrentSnapshot, FUndoSnapshotEntry& OutEntry);
	bool RestoreHistorySnapshotIndex(const FName& WorldHandle, int32 Index, FString CurrentSnapshot, FUndoSnapshotEntry& OutEntry);
	bool ClearStorage();
	bool ClearStorage(const FName& WorldHandle);
	void PushWithLimit(TArray<FUndoSnapshotEntry>& History, FUndoSnapshotEntry Entry);
	void PushTransactionWithLimit(TArray<FEditorTransaction>& History, FEditorTransaction Transaction);
	size_t GetTransactionMemoryUsage() const;

private:
	UEditorEngine* Owner = nullptr;
	TMap<FName, FWorldUndoHistory, FName::Hash> HistoriesByWorld;
	TArray<FEditorTransaction> UndoTransactions;
	TArray<FEditorTransaction> RedoTransactions;
	std::unique_ptr<FEditorTransaction> ActiveTransaction;
	mutable TArray<FUndoSnapshotEntry> EmptyHistory;
	bool bRestoring = false;
	bool bApplyingUndoRedo = false;
	uint64 TransactionRevision = 0;

	static constexpr int32 MaxUndoHistory = 50;
	static constexpr int32 MaxTransactionHistory = 200;
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
