#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"
#include "Object/FName.h"

#include <cstddef>
#include <functional>
#include <memory>

class UEditorEngine;
class UObject;
class AActor;
class UActorComponent;
class USceneComponent;
struct FProperty;

struct FEditorObjectRef
{
	FName WorldHandle = FName::None;
	uint32 ActorUUID = 0;
	uint32 ComponentUUID = 0;
	FString ActorName;
	FString ComponentName;
	FString ComponentGuid;

	bool HasActor() const { return ActorUUID != 0 || !ActorName.empty(); }
	bool HasComponent() const { return ComponentUUID != 0 || !ComponentGuid.empty() || !ComponentName.empty(); }
	bool IsValid() const { return WorldHandle != FName::None && HasActor(); }
};

struct FEditorActorTransformState
{
	FEditorObjectRef ActorRef;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
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
	FRotator RelativeRotation = FRotator::ZeroRotator;
	FVector RelativeScale = FVector(1.0f, 1.0f, 1.0f);

	bool IsValid() const { return ComponentRef.IsValid() && ComponentRef.HasComponent(); }
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
	TArray<uint8> ValueBytes;

	bool IsValid() const { return ObjectRef.IsValid() && !PropertyName.empty() && !ValueBytes.empty(); }
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

class FLambdaEditorUndoCommand : public IEditorUndoCommand
{
public:
	using FApplySnapshotFunc = std::function<bool(FEditorUndoContext&, const TArray<uint8>&)>;

	FLambdaEditorUndoCommand(
		FString InLabel,
		TArray<uint8> InBeforeSnapshot,
		TArray<uint8> InAfterSnapshot,
		FApplySnapshotFunc InApplySnapshot);

	FString GetLabel() const override;
	bool Undo(FEditorUndoContext& Context) override;
	bool Redo(FEditorUndoContext& Context) override;
	size_t GetMemoryUsage() const override;

private:
	FString Label;
	TArray<uint8> BeforeSnapshot;
	TArray<uint8> AfterSnapshot;
	FApplySnapshotFunc ApplySnapshot;
};

struct FEditorTransaction
{
	FString Label;
	TArray<std::unique_ptr<IEditorUndoCommand>> Commands;

	bool Undo(FEditorUndoContext& Context);
	bool Redo(FEditorUndoContext& Context);
	bool IsEmpty() const { return Commands.empty(); }
	size_t GetMemoryUsage() const;
};

struct FUndoSnapshotEntry
{
	FName WorldHandle = FName::None;
	FString Label;
	FString Snapshot;
};

class FEditorUndoSystem
{
public:
	void SetOwner(UEditorEngine* InOwner) { Owner = InOwner; }

	void BeginTransaction(const FString& Label);
	bool AddCommand(std::unique_ptr<IEditorUndoCommand> Command);
	bool EndTransaction();
	void CancelTransaction();

	bool CaptureSnapshot(const char* Reason = nullptr);
	bool Undo();
	bool Redo();

	TArray<FEditorActorTransformState> CaptureActorTransforms(const TArray<AActor*>& Actors) const;
	bool RecordActorTransforms(
		const TArray<FEditorActorTransformState>& BeforeStates,
		const TArray<FEditorActorTransformState>& AfterStates,
		const FString& Label = "Transform Actors");

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

	FEditorReflectedPropertyState CaptureReflectedProperty(UObject* Object, const FProperty& Property) const;
	bool RecordReflectedProperty(
		const FEditorReflectedPropertyState& BeforeState,
		const FEditorReflectedPropertyState& AfterState,
		const FString& Label = "Edit Property");

	FEditorFileSystemState CaptureFileSystemState(
		const FString& RootPath,
		const FString& Label = FString()) const;
	bool RecordCreateFileSystemPath(
		const FEditorFileSystemState& CreatedState,
		const FString& Label = "Create Content");
	bool RecordCreateFileSystemPaths(
		const TArray<FEditorFileSystemState>& CreatedStates,
		const FString& Label = "Create Content");
	bool RecordDeleteFileSystemPath(
		const FEditorFileSystemState& DeletedState,
		const FString& Label = "Delete Content");
	bool RecordModifyFileSystemPath(
		const FEditorFileSystemState& BeforeState,
		const FEditorFileSystemState& AfterState,
		const FString& Label = "Modify Content");
	bool RecordRenameFileSystemPath(
		const FEditorFileSystemState& BeforeState,
		const FEditorFileSystemState& AfterState,
		const FString& Label = "Rename Content");

	void ClearHistory();
	void ClearHistory(const FName& WorldHandle);
	void ClearAllHistory();

	bool CanUndo() const;
	bool CanRedo() const;

	bool IsApplyingUndoRedo() const { return bApplyingUndoRedo; }
	bool HasActiveTransaction() const { return ActiveTransaction != nullptr; }
	bool IsRestoring() const { return bRestoring || bApplyingUndoRedo; }
	void BeginRestore() { bRestoring = true; }
	void EndRestore() { bRestoring = false; }

	const TArray<FUndoSnapshotEntry>& GetUndoHistory() const;
	const TArray<FUndoSnapshotEntry>& GetRedoHistory() const;

private:
	FEditorUndoContext MakeContext() const;
	FName GetActiveWorldHandle() const;
	void PushTransactionWithLimit(TArray<FEditorTransaction>& History, FEditorTransaction Transaction);
	void RefreshHistoryLabels() const;

private:
	UEditorEngine* Owner = nullptr;
	TArray<FEditorTransaction> UndoTransactions;
	TArray<FEditorTransaction> RedoTransactions;
	std::unique_ptr<FEditorTransaction> ActiveTransaction;
	mutable TArray<FUndoSnapshotEntry> UndoHistoryLabels;
	mutable TArray<FUndoSnapshotEntry> RedoHistoryLabels;
	bool bRestoring = false;
	bool bApplyingUndoRedo = false;

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
