#include "Editor/Undo/EditorUndoSystem.h"

#include "Component/ActorComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logging/Log.h"
#include "Core/Logging/Notification.h"
#include "Core/Types/PropertyTypes.h"
#include "Editor/EditorEngine.h"
#include "Editor/Selection/SelectionManager.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "GameFramework/WorldContext.h"
#include "Object/Object.h"
#include "Object/Reflection/UClass.h"
#include "Platform/Paths.h"
#include "Serialization/MemoryArchive.h"
#include "Serialization/SceneSaveManager.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace
{
FString GetTransactionLabel(const FEditorTransaction& Transaction)
{
	return Transaction.Label.empty() ? FString("Editor Edit") : Transaction.Label;
}

void ShowUndoRedoToast(const char* Action, const FString& Label, ENotificationType Type)
{
	FNotificationManager::Get().AddNotification(FString(Action) + ": " + Label, Type, 1.8f);
}

bool IsUndoRecordingAllowed(UEditorEngine* Editor)
{
	return Editor && !Editor->IsPlayingInEditor();
}

bool SameActorTarget(const FEditorObjectRef& A, const FEditorObjectRef& B)
{
	if (A.WorldHandle != B.WorldHandle)
	{
		return false;
	}
	if (A.ActorUUID != 0 && B.ActorUUID != 0)
	{
		return A.ActorUUID == B.ActorUUID;
	}
	return !A.ActorName.empty() && A.ActorName == B.ActorName;
}

bool SameComponentTarget(const FEditorObjectRef& A, const FEditorObjectRef& B)
{
	if (!SameActorTarget(A, B))
	{
		return false;
	}
	if (!A.HasComponent() && !B.HasComponent())
	{
		return true;
	}
	if (A.ComponentUUID != 0 && B.ComponentUUID != 0)
	{
		return A.ComponentUUID == B.ComponentUUID;
	}
	if (!A.ComponentGuid.empty() && !B.ComponentGuid.empty())
	{
		return A.ComponentGuid == B.ComponentGuid;
	}
	return !A.ComponentName.empty() && A.ComponentName == B.ComponentName;
}

bool SameVector(const FVector& A, const FVector& B)
{
	return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
}

bool SameTransform(const FEditorActorTransformState& A, const FEditorActorTransformState& B)
{
	return SameVector(A.Location, B.Location) && A.Rotation == B.Rotation && SameVector(A.Scale, B.Scale);
}

bool SameComponentTransform(
	const FEditorSceneComponentTransformState& A,
	const FEditorSceneComponentTransformState& B)
{
	return SameVector(A.RelativeLocation, B.RelativeLocation)
		&& A.RelativeRotation == B.RelativeRotation
		&& SameVector(A.RelativeScale, B.RelativeScale);
}

const FProperty* FindPropertyByName(UObject* Object, const FString& PropertyName)
{
	if (!Object || !Object->GetClass() || PropertyName.empty())
	{
		return nullptr;
	}

	TArray<const FProperty*> Properties;
	Object->GetClass()->GetPropertyRefs(Properties);
	for (const FProperty* Property : Properties)
	{
		if (Property && Property->Name && std::strcmp(Property->Name, PropertyName.c_str()) == 0)
		{
			return Property;
		}
	}
	return nullptr;
}

bool SerializePropertyValue(UObject* Object, const FProperty& Property, TArray<uint8>& OutBytes)
{
	OutBytes.clear();
	if (!Object || Property.Name == nullptr || (Property.Flags & PF_Transient) != 0)
	{
		return false;
	}

	void* ValuePtr = Property.GetValuePtrFor(Object);
	if (!ValuePtr)
	{
		return false;
	}

	FMemoryArchive Ar(true);
	FPropertySerializeContext Context;
	Context.Owner = Object;
	Context.RequiredFlags = PF_Save;
	Property.SerializeValue(ValuePtr, Ar, Context);
	OutBytes = Ar.GetBuffer();
	return !OutBytes.empty();
}

bool DeserializePropertyValue(UObject* Object, const FProperty& Property, const TArray<uint8>& Bytes)
{
	if (!Object || !Property.Name || Bytes.empty())
	{
		return false;
	}

	void* ValuePtr = Property.GetValuePtrFor(Object);
	if (!ValuePtr)
	{
		return false;
	}

	FMemoryArchive Ar(Bytes, false);
	FPropertySerializeContext Context;
	Context.Owner = Object;
	Context.RequiredFlags = PF_Save;
	Property.SerializeValue(ValuePtr, Ar, Context);

	FPropertyChangedEvent Event;
	Event.Object = Object;
	Event.Property = &Property;
	Event.PropertyName = Property.Name;
	Event.DisplayName = Property.DisplayName ? Property.DisplayName : Property.Name;
	Event.Type = Property.GetType();
	Event.ChangeType = EPropertyChangeType::ValueSet;
	Object->PostEditChangeProperty(Event);
	return true;
}

std::filesystem::path ResolveUndoFileSystemPath(const FString& Path)
{
	std::filesystem::path Result(FPaths::ToWide(Path));
	if (Result.is_relative())
	{
		Result = std::filesystem::path(FPaths::RootDir()) / Result;
	}
	return Result.lexically_normal();
}

std::filesystem::path GetUndoRootPath()
{
	return std::filesystem::path(FPaths::RootDir()).lexically_normal();
}

bool IsPathInsideUndoRoot(const std::filesystem::path& Path)
{
	const std::filesystem::path Root = GetUndoRootPath();
	const std::filesystem::path Normalized = Path.lexically_normal();
	const std::wstring RootText = Root.wstring();
	const std::wstring PathText = Normalized.wstring();
	if (PathText.size() < RootText.size())
	{
		return false;
	}
	if (_wcsnicmp(PathText.c_str(), RootText.c_str(), RootText.size()) != 0)
	{
		return false;
	}
	return PathText.size() == RootText.size()
		|| PathText[RootText.size()] == L'\\'
		|| PathText[RootText.size()] == L'/';
}

FString NormalizeUndoFileSystemPath(const std::filesystem::path& Path)
{
	return FPaths::ToUtf8(Path.lexically_normal().generic_wstring());
}

bool ReadFileBytesForUndo(const std::filesystem::path& Path, TArray<uint8>& OutBytes)
{
	OutBytes.clear();
	std::ifstream File(Path, std::ios::binary);
	if (!File.is_open())
	{
		return false;
	}

	File.seekg(0, std::ios::end);
	const std::streamoff Size = File.tellg();
	File.seekg(0, std::ios::beg);
	if (Size <= 0)
	{
		return true;
	}

	OutBytes.resize(static_cast<size_t>(Size));
	File.read(reinterpret_cast<char*>(OutBytes.data()), Size);
	return true;
}

bool RemoveFileSystemRoot(const FString& RootPath)
{
	const std::filesystem::path Root = ResolveUndoFileSystemPath(RootPath);
	if (!IsPathInsideUndoRoot(Root))
	{
		return false;
	}

	std::error_code Ec;
	if (!std::filesystem::exists(Root, Ec) || Ec)
	{
		return true;
	}
	std::filesystem::remove_all(Root, Ec);
	return !Ec;
}

bool RestoreFileSystemEntries(const TArray<FEditorFileSystemEntryState>& Entries)
{
	bool bRestoredAny = false;
	for (const FEditorFileSystemEntryState& Entry : Entries)
	{
		if (Entry.Path.empty())
		{
			continue;
		}

		const std::filesystem::path EntryPath = ResolveUndoFileSystemPath(Entry.Path);
		if (!IsPathInsideUndoRoot(EntryPath))
		{
			continue;
		}

		std::error_code Ec;
		if (Entry.bDirectory)
		{
			std::filesystem::create_directories(EntryPath, Ec);
			bRestoredAny |= !Ec;
			continue;
		}

		std::filesystem::create_directories(EntryPath.parent_path(), Ec);
		if (Ec)
		{
			continue;
		}

		std::ofstream File(EntryPath, std::ios::binary | std::ios::trunc);
		if (!File.is_open())
		{
			continue;
		}
		if (!Entry.Data.empty())
		{
			File.write(reinterpret_cast<const char*>(Entry.Data.data()), static_cast<std::streamsize>(Entry.Data.size()));
		}
		bRestoredAny = true;
	}
	return bRestoredAny || Entries.empty();
}

void RefreshAfterContentUndo(FEditorUndoContext& Context)
{
	if (Context.Editor)
	{
		Context.Editor->RefreshContentBrowser();
	}
}

void MarkWorldEdited(UEditorEngine* Editor, const FName& WorldHandle)
{
	if (!Editor)
	{
		return;
	}

	if (FWorldContext* WorldContext = Editor->GetWorldContextFromHandle(WorldHandle))
	{
		if (WorldContext->World)
		{
			WorldContext->World->MarkWorldPrimitivePickingBVHDirty();
		}
	}
	Editor->InvalidateOcclusionResults();
}

class FSetActorTransformsCommand final : public IEditorUndoCommand
{
public:
	FSetActorTransformsCommand(
		TArray<FEditorActorTransformState> InBeforeStates,
		TArray<FEditorActorTransformState> InAfterStates,
		FString InLabel)
		: BeforeStates(std::move(InBeforeStates))
		, AfterStates(std::move(InAfterStates))
		, Label(std::move(InLabel))
	{
		if (Label.empty())
		{
			Label = "Transform Actors";
		}
	}

	FString GetLabel() const override { return Label; }
	bool Undo(FEditorUndoContext& Context) override { return Apply(Context, BeforeStates); }
	bool Redo(FEditorUndoContext& Context) override { return Apply(Context, AfterStates); }

	size_t GetMemoryUsage() const override
	{
		return sizeof(*this)
			+ Label.capacity()
			+ BeforeStates.capacity() * sizeof(FEditorActorTransformState)
			+ AfterStates.capacity() * sizeof(FEditorActorTransformState);
	}

private:
	bool Apply(FEditorUndoContext& Context, const TArray<FEditorActorTransformState>& States)
	{
		bool bAppliedAny = false;
		FName EditedWorld = FName::None;
		for (const FEditorActorTransformState& State : States)
		{
			AActor* Actor = Context.ResolveActor(State.ActorRef);
			if (!Actor)
			{
				continue;
			}
			Actor->SetActorLocation(State.Location);
			Actor->SetActorRotation(State.Rotation);
			Actor->SetActorScale(State.Scale);
			EditedWorld = State.ActorRef.WorldHandle;
			bAppliedAny = true;
		}
		if (bAppliedAny)
		{
			MarkWorldEdited(Context.Editor, EditedWorld);
		}
		return bAppliedAny;
	}

	TArray<FEditorActorTransformState> BeforeStates;
	TArray<FEditorActorTransformState> AfterStates;
	FString Label;
};

enum class EActorLifecycleUndoMode
{
	Created,
	Deleted
};

class FSerializedActorLifecycleCommand final : public IEditorUndoCommand
{
public:
	FSerializedActorLifecycleCommand(
		TArray<FEditorSerializedActorState> InStates,
		FString InLabel,
		EActorLifecycleUndoMode InMode)
		: States(std::move(InStates))
		, Label(std::move(InLabel))
		, Mode(InMode)
	{
		if (Label.empty())
		{
			Label = Mode == EActorLifecycleUndoMode::Created ? "Create Actors" : "Delete Actors";
		}
	}

	FString GetLabel() const override { return Label; }

	bool Undo(FEditorUndoContext& Context) override
	{
		return Mode == EActorLifecycleUndoMode::Created ? DestroyActors(Context) : SpawnActors(Context);
	}

	bool Redo(FEditorUndoContext& Context) override
	{
		return Mode == EActorLifecycleUndoMode::Created ? SpawnActors(Context) : DestroyActors(Context);
	}

	size_t GetMemoryUsage() const override
	{
		size_t Total = sizeof(*this) + Label.capacity() + States.capacity() * sizeof(FEditorSerializedActorState);
		for (const FEditorSerializedActorState& State : States)
		{
			Total += State.ActorJson.capacity() + State.ActorRef.ActorName.capacity();
		}
		return Total;
	}

private:
	bool SpawnActors(FEditorUndoContext& Context)
	{
		if (!Context.Editor)
		{
			return false;
		}

		TArray<AActor*> SpawnedActors;
		bool bAppliedAny = false;
		FName EditedWorld = FName::None;
		for (const FEditorSerializedActorState& State : States)
		{
			if (!State.IsValid() || Context.ResolveActor(State.ActorRef))
			{
				continue;
			}

			FWorldContext* WorldContext = Context.Editor->GetWorldContextFromHandle(State.ActorRef.WorldHandle);
			UWorld* World = WorldContext ? WorldContext->World : nullptr;
			if (!World)
			{
				continue;
			}

			json::JSON ActorJson = json::JSON::Load(State.ActorJson);
			AActor* SpawnedActor = FSceneSaveManager::SpawnActorFromSerializedActor(World, ActorJson, true);
			if (!SpawnedActor)
			{
				continue;
			}

			SpawnedActors.push_back(SpawnedActor);
			EditedWorld = State.ActorRef.WorldHandle;
			bAppliedAny = true;
		}

		if (bAppliedAny)
		{
			FSelectionManager& Selection = Context.Editor->GetSelectionManager();
			Selection.ClearSelection();
			for (AActor* Actor : SpawnedActors)
			{
				Selection.ToggleSelect(Actor);
			}
			MarkWorldEdited(Context.Editor, EditedWorld);
		}
		return bAppliedAny;
	}

	bool DestroyActors(FEditorUndoContext& Context)
	{
		if (!Context.Editor)
		{
			return false;
		}

		bool bAppliedAny = false;
		FName EditedWorld = FName::None;
		for (const FEditorSerializedActorState& State : States)
		{
			AActor* Actor = Context.ResolveActor(State.ActorRef);
			UWorld* World = Actor ? Actor->GetWorld() : nullptr;
			if (!Actor || !World)
			{
				continue;
			}

			Context.Editor->GetSelectionManager().Deselect(Actor);
			World->DestroyActor(Actor);
			EditedWorld = State.ActorRef.WorldHandle;
			bAppliedAny = true;
		}

		if (bAppliedAny)
		{
			MarkWorldEdited(Context.Editor, EditedWorld);
		}
		return bAppliedAny;
	}

	TArray<FEditorSerializedActorState> States;
	FString Label;
	EActorLifecycleUndoMode Mode = EActorLifecycleUndoMode::Deleted;
};

class FSetSerializedActorStatesCommand final : public IEditorUndoCommand
{
public:
	FSetSerializedActorStatesCommand(
		TArray<FEditorSerializedActorState> InBeforeStates,
		TArray<FEditorSerializedActorState> InAfterStates,
		FString InLabel)
		: BeforeStates(std::move(InBeforeStates))
		, AfterStates(std::move(InAfterStates))
		, Label(std::move(InLabel))
	{
		if (Label.empty())
		{
			Label = "Edit Actor";
		}
	}

	FString GetLabel() const override { return Label; }
	bool Undo(FEditorUndoContext& Context) override { return Apply(Context, BeforeStates); }
	bool Redo(FEditorUndoContext& Context) override { return Apply(Context, AfterStates); }

	size_t GetMemoryUsage() const override
	{
		return sizeof(*this)
			+ Label.capacity()
			+ BeforeStates.capacity() * sizeof(FEditorSerializedActorState)
			+ AfterStates.capacity() * sizeof(FEditorSerializedActorState);
	}

private:
	bool Apply(FEditorUndoContext& Context, const TArray<FEditorSerializedActorState>& TargetStates)
	{
		if (!Context.Editor)
		{
			return false;
		}

		TArray<AActor*> SpawnedActors;
		bool bAppliedAny = false;
		FName EditedWorld = FName::None;
		for (const FEditorSerializedActorState& State : TargetStates)
		{
			if (!State.IsValid())
			{
				continue;
			}

			FWorldContext* WorldContext = Context.Editor->GetWorldContextFromHandle(State.ActorRef.WorldHandle);
			UWorld* World = WorldContext ? WorldContext->World : nullptr;
			if (!World)
			{
				continue;
			}

			if (AActor* ExistingActor = Context.ResolveActor(State.ActorRef))
			{
				Context.Editor->GetSelectionManager().Deselect(ExistingActor);
				World->DestroyActor(ExistingActor);
			}

			json::JSON ActorJson = json::JSON::Load(State.ActorJson);
			AActor* SpawnedActor = FSceneSaveManager::SpawnActorFromSerializedActor(World, ActorJson, true);
			if (!SpawnedActor)
			{
				continue;
			}

			SpawnedActors.push_back(SpawnedActor);
			EditedWorld = State.ActorRef.WorldHandle;
			bAppliedAny = true;
		}

		if (bAppliedAny)
		{
			FSelectionManager& Selection = Context.Editor->GetSelectionManager();
			Selection.ClearSelection();
			for (AActor* Actor : SpawnedActors)
			{
				Selection.ToggleSelect(Actor);
			}
			MarkWorldEdited(Context.Editor, EditedWorld);
		}
		return bAppliedAny;
	}

	TArray<FEditorSerializedActorState> BeforeStates;
	TArray<FEditorSerializedActorState> AfterStates;
	FString Label;
};

class FSetSceneComponentTransformCommand final : public IEditorUndoCommand
{
public:
	FSetSceneComponentTransformCommand(
		FEditorSceneComponentTransformState InBeforeState,
		FEditorSceneComponentTransformState InAfterState,
		FString InLabel)
		: BeforeState(std::move(InBeforeState))
		, AfterState(std::move(InAfterState))
		, Label(std::move(InLabel))
	{
		if (Label.empty())
		{
			Label = "Transform Component";
		}
	}

	FString GetLabel() const override { return Label; }
	bool Undo(FEditorUndoContext& Context) override { return Apply(Context, BeforeState); }
	bool Redo(FEditorUndoContext& Context) override { return Apply(Context, AfterState); }

	size_t GetMemoryUsage() const override { return sizeof(*this) + Label.capacity(); }

private:
	bool Apply(FEditorUndoContext& Context, const FEditorSceneComponentTransformState& State)
	{
		USceneComponent* Component = Cast<USceneComponent>(Context.ResolveComponent(State.ComponentRef));
		if (!Component)
		{
			return false;
		}

		Component->SetRelativeLocation(State.RelativeLocation);
		Component->SetRelativeRotation(State.RelativeRotation);
		Component->SetRelativeScale(State.RelativeScale);
		MarkWorldEdited(Context.Editor, State.ComponentRef.WorldHandle);
		return true;
	}

	FEditorSceneComponentTransformState BeforeState;
	FEditorSceneComponentTransformState AfterState;
	FString Label;
};

class FSetReflectedPropertyCommand final : public IEditorUndoCommand
{
public:
	FSetReflectedPropertyCommand(
		FEditorReflectedPropertyState InBeforeState,
		FEditorReflectedPropertyState InAfterState,
		FString InLabel)
		: BeforeState(std::move(InBeforeState))
		, AfterState(std::move(InAfterState))
		, Label(std::move(InLabel))
	{
		if (Label.empty())
		{
			Label = "Edit Property";
		}
	}

	FString GetLabel() const override { return Label; }
	bool Undo(FEditorUndoContext& Context) override { return Apply(Context, BeforeState); }
	bool Redo(FEditorUndoContext& Context) override { return Apply(Context, AfterState); }

	size_t GetMemoryUsage() const override
	{
		return sizeof(*this)
			+ Label.capacity()
			+ BeforeState.ValueBytes.capacity()
			+ AfterState.ValueBytes.capacity();
	}

private:
	UObject* ResolveObject(FEditorUndoContext& Context, const FEditorObjectRef& Ref) const
	{
		if (Ref.HasComponent())
		{
			return Context.ResolveComponent(Ref);
		}
		return Context.ResolveActor(Ref);
	}

	bool Apply(FEditorUndoContext& Context, const FEditorReflectedPropertyState& State)
	{
		UObject* Object = ResolveObject(Context, State.ObjectRef);
		const FProperty* Property = FindPropertyByName(Object, State.PropertyName);
		if (!Object || !Property)
		{
			return false;
		}

		const bool bApplied = DeserializePropertyValue(Object, *Property, State.ValueBytes);
		if (bApplied)
		{
			MarkWorldEdited(Context.Editor, State.ObjectRef.WorldHandle);
		}
		return bApplied;
	}

	FEditorReflectedPropertyState BeforeState;
	FEditorReflectedPropertyState AfterState;
	FString Label;
};

class FCreateFileSystemPathCommand final : public IEditorUndoCommand
{
public:
	FCreateFileSystemPathCommand(FEditorFileSystemState InState, FString InLabel)
		: State(std::move(InState))
		, Label(std::move(InLabel))
	{
		if (Label.empty())
		{
			Label = "Create Content";
		}
	}

	FString GetLabel() const override { return Label; }
	bool Undo(FEditorUndoContext& Context) override
	{
		const bool bOk = RemoveFileSystemRoot(State.RootPath);
		if (bOk) RefreshAfterContentUndo(Context);
		return bOk;
	}
	bool Redo(FEditorUndoContext& Context) override
	{
		const bool bOk = RestoreFileSystemEntries(State.Entries);
		if (bOk) RefreshAfterContentUndo(Context);
		return bOk;
	}
	size_t GetMemoryUsage() const override { return sizeof(*this) + Label.capacity() + State.Entries.capacity() * sizeof(FEditorFileSystemEntryState); }

private:
	FEditorFileSystemState State;
	FString Label;
};

class FDeleteFileSystemPathCommand final : public IEditorUndoCommand
{
public:
	FDeleteFileSystemPathCommand(FEditorFileSystemState InState, FString InLabel)
		: State(std::move(InState))
		, Label(std::move(InLabel))
	{
		if (Label.empty())
		{
			Label = "Delete Content";
		}
	}

	FString GetLabel() const override { return Label; }
	bool Undo(FEditorUndoContext& Context) override
	{
		const bool bOk = RestoreFileSystemEntries(State.Entries);
		if (bOk) RefreshAfterContentUndo(Context);
		return bOk;
	}
	bool Redo(FEditorUndoContext& Context) override
	{
		const bool bOk = RemoveFileSystemRoot(State.RootPath);
		if (bOk) RefreshAfterContentUndo(Context);
		return bOk;
	}
	size_t GetMemoryUsage() const override { return sizeof(*this) + Label.capacity() + State.Entries.capacity() * sizeof(FEditorFileSystemEntryState); }

private:
	FEditorFileSystemState State;
	FString Label;
};

class FModifyFileSystemPathCommand final : public IEditorUndoCommand
{
public:
	FModifyFileSystemPathCommand(FEditorFileSystemState InBeforeState, FEditorFileSystemState InAfterState, FString InLabel)
		: BeforeState(std::move(InBeforeState))
		, AfterState(std::move(InAfterState))
		, Label(std::move(InLabel))
	{
		if (Label.empty())
		{
			Label = "Modify Content";
		}
	}

	FString GetLabel() const override { return Label; }
	bool Undo(FEditorUndoContext& Context) override
	{
		RemoveFileSystemRoot(AfterState.RootPath);
		const bool bOk = RestoreFileSystemEntries(BeforeState.Entries);
		if (bOk) RefreshAfterContentUndo(Context);
		return bOk;
	}
	bool Redo(FEditorUndoContext& Context) override
	{
		RemoveFileSystemRoot(BeforeState.RootPath);
		const bool bOk = RestoreFileSystemEntries(AfterState.Entries);
		if (bOk) RefreshAfterContentUndo(Context);
		return bOk;
	}
	size_t GetMemoryUsage() const override { return sizeof(*this) + Label.capacity(); }

private:
	FEditorFileSystemState BeforeState;
	FEditorFileSystemState AfterState;
	FString Label;
};

class FRenameFileSystemPathCommand final : public IEditorUndoCommand
{
public:
	FRenameFileSystemPathCommand(FEditorFileSystemState InBeforeState, FEditorFileSystemState InAfterState, FString InLabel)
		: BeforeState(std::move(InBeforeState))
		, AfterState(std::move(InAfterState))
		, Label(std::move(InLabel))
	{
		if (Label.empty())
		{
			Label = "Rename Content";
		}
	}

	FString GetLabel() const override { return Label; }
	bool Undo(FEditorUndoContext& Context) override
	{
		RemoveFileSystemRoot(AfterState.RootPath);
		const bool bOk = RestoreFileSystemEntries(BeforeState.Entries);
		if (bOk) RefreshAfterContentUndo(Context);
		return bOk;
	}
	bool Redo(FEditorUndoContext& Context) override
	{
		RemoveFileSystemRoot(BeforeState.RootPath);
		const bool bOk = RestoreFileSystemEntries(AfterState.Entries);
		if (bOk) RefreshAfterContentUndo(Context);
		return bOk;
	}
	size_t GetMemoryUsage() const override { return sizeof(*this) + Label.capacity(); }

private:
	FEditorFileSystemState BeforeState;
	FEditorFileSystemState AfterState;
	FString Label;
};
}

AActor* FEditorUndoContext::ResolveActor(const FEditorObjectRef& Ref) const
{
	return FEditorUndoObjectResolver::ResolveActor(Editor, Ref);
}

UActorComponent* FEditorUndoContext::ResolveComponent(const FEditorObjectRef& Ref) const
{
	return FEditorUndoObjectResolver::ResolveComponent(Editor, Ref);
}

FEditorObjectRef FEditorUndoObjectResolver::MakeActorRef(UEditorEngine* Editor, AActor* Actor)
{
	FEditorObjectRef Ref;
	if (!Editor || !Actor)
	{
		return Ref;
	}

	if (FWorldContext* WorldContext = Editor->GetWorldContextFromWorld(Actor->GetWorld()))
	{
		Ref.WorldHandle = WorldContext->ContextHandle;
	}
	else
	{
		Ref.WorldHandle = Editor->GetActiveWorldHandle();
	}

	Ref.ActorUUID = Actor->GetUUID();
	Ref.ActorName = Actor->GetFName().ToString();
	return Ref;
}

AActor* FEditorUndoObjectResolver::ResolveActor(UEditorEngine* Editor, const FEditorObjectRef& Ref)
{
	if (!Editor || !Ref.IsValid())
	{
		return nullptr;
	}

	if (Ref.ActorUUID != 0)
	{
		if (AActor* Actor = Cast<AActor>(UObjectManager::Get().FindByUUID(Ref.ActorUUID)))
		{
			if (FWorldContext* ActorContext = Editor->GetWorldContextFromWorld(Actor->GetWorld()))
			{
				if (ActorContext->ContextHandle == Ref.WorldHandle)
				{
					return Actor;
				}
			}
		}
	}

	FWorldContext* WorldContext = Editor->GetWorldContextFromHandle(Ref.WorldHandle);
	UWorld* World = WorldContext ? WorldContext->World : nullptr;
	if (!World)
	{
		return nullptr;
	}

	for (AActor* Actor : World->GetActors())
	{
		if (IsValid(Actor) && Actor->GetFName().ToString() == Ref.ActorName)
		{
			return Actor;
		}
	}
	return nullptr;
}

FEditorObjectRef FEditorUndoObjectResolver::MakeComponentRef(UEditorEngine* Editor, UActorComponent* Component)
{
	FEditorObjectRef Ref;
	if (!Editor || !Component || !Component->GetOwner())
	{
		return Ref;
	}

	Ref = MakeActorRef(Editor, Component->GetOwner());
	Ref.ComponentUUID = Component->GetUUID();
	Ref.ComponentName = Component->GetFName().ToString();
	Ref.ComponentGuid = Component->EnsurePersistentGuid();
	return Ref;
}

UActorComponent* FEditorUndoObjectResolver::ResolveComponent(UEditorEngine* Editor, const FEditorObjectRef& Ref)
{
	if (!Editor || !Ref.IsValid() || !Ref.HasComponent())
	{
		return nullptr;
	}

	if (Ref.ComponentUUID != 0)
	{
		if (UActorComponent* Component = Cast<UActorComponent>(UObjectManager::Get().FindByUUID(Ref.ComponentUUID)))
		{
			if (Component->GetOwner() == ResolveActor(Editor, Ref))
			{
				return Component;
			}
		}
	}

	AActor* Actor = ResolveActor(Editor, Ref);
	if (!Actor)
	{
		return nullptr;
	}

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (!IsValid(Component))
		{
			continue;
		}
		if (!Ref.ComponentGuid.empty() && Component->GetPersistentGuid() == Ref.ComponentGuid)
		{
			return Component;
		}
		if (!Ref.ComponentName.empty() && Component->GetFName().ToString() == Ref.ComponentName)
		{
			return Component;
		}
	}
	return nullptr;
}

bool FEditorTransaction::Undo(FEditorUndoContext& Context)
{
	bool bAppliedAny = false;
	for (auto It = Commands.rbegin(); It != Commands.rend(); ++It)
	{
		if (*It)
		{
			bAppliedAny |= (*It)->Undo(Context);
		}
	}
	return bAppliedAny;
}

FLambdaEditorUndoCommand::FLambdaEditorUndoCommand(
	FString InLabel,
	TArray<uint8> InBeforeSnapshot,
	TArray<uint8> InAfterSnapshot,
	FApplySnapshotFunc InApplySnapshot)
	: Label(std::move(InLabel))
	, BeforeSnapshot(std::move(InBeforeSnapshot))
	, AfterSnapshot(std::move(InAfterSnapshot))
	, ApplySnapshot(std::move(InApplySnapshot))
{
}

FString FLambdaEditorUndoCommand::GetLabel() const
{
	return Label.empty() ? FString("Editor Edit") : Label;
}

bool FLambdaEditorUndoCommand::Undo(FEditorUndoContext& Context)
{
	return ApplySnapshot && !BeforeSnapshot.empty() && ApplySnapshot(Context, BeforeSnapshot);
}

bool FLambdaEditorUndoCommand::Redo(FEditorUndoContext& Context)
{
	return ApplySnapshot && !AfterSnapshot.empty() && ApplySnapshot(Context, AfterSnapshot);
}

size_t FLambdaEditorUndoCommand::GetMemoryUsage() const
{
	return sizeof(*this)
		+ Label.capacity()
		+ BeforeSnapshot.capacity() * sizeof(uint8)
		+ AfterSnapshot.capacity() * sizeof(uint8);
}

bool FEditorTransaction::Redo(FEditorUndoContext& Context)
{
	bool bAppliedAny = false;
	for (std::unique_ptr<IEditorUndoCommand>& Command : Commands)
	{
		if (Command)
		{
			bAppliedAny |= Command->Redo(Context);
		}
	}
	return bAppliedAny;
}

size_t FEditorTransaction::GetMemoryUsage() const
{
	size_t Total = sizeof(*this) + Label.capacity() + Commands.capacity() * sizeof(std::unique_ptr<IEditorUndoCommand>);
	for (const std::unique_ptr<IEditorUndoCommand>& Command : Commands)
	{
		if (Command)
		{
			Total += Command->GetMemoryUsage();
		}
	}
	return Total;
}

void FEditorUndoSystem::BeginTransaction(const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner) || bRestoring || bApplyingUndoRedo)
	{
		return;
	}

	if (ActiveTransaction)
	{
		if (ActiveTransaction->Label.empty())
		{
			ActiveTransaction->Label = Label;
		}
		return;
	}

	ActiveTransaction = std::make_unique<FEditorTransaction>();
	ActiveTransaction->Label = Label.empty() ? "Editor Edit" : Label;
}

bool FEditorUndoSystem::AddCommand(std::unique_ptr<IEditorUndoCommand> Command)
{
	if (!Command || !IsUndoRecordingAllowed(Owner) || bRestoring || bApplyingUndoRedo)
	{
		return false;
	}

	if (!ActiveTransaction)
	{
		BeginTransaction(Command->GetLabel());
	}
	if (!ActiveTransaction)
	{
		return false;
	}

	ActiveTransaction->Commands.push_back(std::move(Command));
	return true;
}

bool FEditorUndoSystem::EndTransaction()
{
	if (!ActiveTransaction)
	{
		return false;
	}

	if (ActiveTransaction->IsEmpty())
	{
		ActiveTransaction.reset();
		return false;
	}

	PushTransactionWithLimit(UndoTransactions, std::move(*ActiveTransaction));
	ActiveTransaction.reset();
	RedoTransactions.clear();
	RefreshHistoryLabels();
	return true;
}

void FEditorUndoSystem::CancelTransaction()
{
	ActiveTransaction.reset();
}

bool FEditorUndoSystem::CaptureSnapshot(const char* Reason)
{
	(void)Reason;
	return false;
}

bool FEditorUndoSystem::Undo()
{
	if (!Owner || UndoTransactions.empty() || bRestoring || bApplyingUndoRedo)
	{
		if (Owner && !bRestoring && !bApplyingUndoRedo)
		{
			ShowUndoRedoToast("Undo", "Nothing to undo", ENotificationType::Info);
		}
		return false;
	}

	FEditorTransaction Transaction = std::move(UndoTransactions.back());
	UndoTransactions.pop_back();
	const FString Label = GetTransactionLabel(Transaction);

	bApplyingUndoRedo = true;
	FEditorUndoContext Context = MakeContext();
	const bool bApplied = Transaction.Undo(Context);
	bApplyingUndoRedo = false;

	if (bApplied)
	{
		PushTransactionWithLimit(RedoTransactions, std::move(Transaction));
		ShowUndoRedoToast("Undo", Label, ENotificationType::Success);
	}
	else
	{
		PushTransactionWithLimit(UndoTransactions, std::move(Transaction));
		ShowUndoRedoToast("Undo failed", Label, ENotificationType::Error);
	}

	RefreshHistoryLabels();
	return bApplied;
}

bool FEditorUndoSystem::Redo()
{
	if (!Owner || RedoTransactions.empty() || bRestoring || bApplyingUndoRedo)
	{
		if (Owner && !bRestoring && !bApplyingUndoRedo)
		{
			ShowUndoRedoToast("Redo", "Nothing to redo", ENotificationType::Info);
		}
		return false;
	}

	FEditorTransaction Transaction = std::move(RedoTransactions.back());
	RedoTransactions.pop_back();
	const FString Label = GetTransactionLabel(Transaction);

	bApplyingUndoRedo = true;
	FEditorUndoContext Context = MakeContext();
	const bool bApplied = Transaction.Redo(Context);
	bApplyingUndoRedo = false;

	if (bApplied)
	{
		PushTransactionWithLimit(UndoTransactions, std::move(Transaction));
		ShowUndoRedoToast("Redo", Label, ENotificationType::Success);
	}
	else
	{
		PushTransactionWithLimit(RedoTransactions, std::move(Transaction));
		ShowUndoRedoToast("Redo failed", Label, ENotificationType::Error);
	}

	RefreshHistoryLabels();
	return bApplied;
}

TArray<FEditorActorTransformState> FEditorUndoSystem::CaptureActorTransforms(const TArray<AActor*>& Actors) const
{
	TArray<FEditorActorTransformState> States;
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring())
	{
		return States;
	}

	for (AActor* Actor : Actors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		FEditorActorTransformState State;
		State.ActorRef = FEditorUndoObjectResolver::MakeActorRef(Owner, Actor);
		if (!State.ActorRef.IsValid())
		{
			continue;
		}

		State.Location = Actor->GetActorLocation();
		State.Rotation = Actor->GetActorRotation();
		State.Scale = Actor->GetActorScale();
		States.push_back(std::move(State));
	}
	return States;
}

bool FEditorUndoSystem::RecordActorTransforms(
	const TArray<FEditorActorTransformState>& BeforeStates,
	const TArray<FEditorActorTransformState>& AfterStates,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring() || BeforeStates.empty() || AfterStates.empty())
	{
		return false;
	}

	TArray<FEditorActorTransformState> FilteredBefore;
	TArray<FEditorActorTransformState> FilteredAfter;
	for (const FEditorActorTransformState& BeforeState : BeforeStates)
	{
		if (!BeforeState.IsValid())
		{
			continue;
		}

		auto AfterIt = std::find_if(
			AfterStates.begin(),
			AfterStates.end(),
			[&BeforeState](const FEditorActorTransformState& Candidate)
			{
				return Candidate.IsValid() && SameActorTarget(BeforeState.ActorRef, Candidate.ActorRef);
			});
		if (AfterIt == AfterStates.end() || SameTransform(BeforeState, *AfterIt))
		{
			continue;
		}

		FilteredBefore.push_back(BeforeState);
		FilteredAfter.push_back(*AfterIt);
	}

	if (FilteredBefore.empty())
	{
		return false;
	}

	BeginTransaction(Label);
	AddCommand(std::make_unique<FSetActorTransformsCommand>(
		std::move(FilteredBefore),
		std::move(FilteredAfter),
		Label));
	return EndTransaction();
}

TArray<FEditorSerializedActorState> FEditorUndoSystem::CaptureActorStates(const TArray<AActor*>& Actors) const
{
	TArray<FEditorSerializedActorState> States;
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring())
	{
		return States;
	}

	for (AActor* Actor : Actors)
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		FEditorSerializedActorState State;
		State.ActorRef = FEditorUndoObjectResolver::MakeActorRef(Owner, Actor);
		if (!State.ActorRef.IsValid())
		{
			continue;
		}

		json::JSON ActorJson = FSceneSaveManager::SerializeActorForPrefab(Actor);
		State.ActorJson = ActorJson.dump();
		if (!State.ActorJson.empty())
		{
			States.push_back(std::move(State));
		}
	}
	return States;
}

bool FEditorUndoSystem::RecordActorCreation(
	const TArray<FEditorSerializedActorState>& CreatedStates,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring() || CreatedStates.empty())
	{
		return false;
	}

	TArray<FEditorSerializedActorState> ValidStates;
	for (const FEditorSerializedActorState& State : CreatedStates)
	{
		if (State.IsValid())
		{
			ValidStates.push_back(State);
		}
	}
	if (ValidStates.empty())
	{
		return false;
	}

	BeginTransaction(Label);
	AddCommand(std::make_unique<FSerializedActorLifecycleCommand>(
		std::move(ValidStates),
		Label,
		EActorLifecycleUndoMode::Created));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordActorDeletion(
	const TArray<FEditorSerializedActorState>& DeletedStates,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring() || DeletedStates.empty())
	{
		return false;
	}

	TArray<FEditorSerializedActorState> ValidStates;
	for (const FEditorSerializedActorState& State : DeletedStates)
	{
		if (State.IsValid())
		{
			ValidStates.push_back(State);
		}
	}
	if (ValidStates.empty())
	{
		return false;
	}

	BeginTransaction(Label);
	AddCommand(std::make_unique<FSerializedActorLifecycleCommand>(
		std::move(ValidStates),
		Label,
		EActorLifecycleUndoMode::Deleted));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordActorStateChange(
	const TArray<FEditorSerializedActorState>& BeforeStates,
	const TArray<FEditorSerializedActorState>& AfterStates,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring() || BeforeStates.empty() || AfterStates.empty())
	{
		return false;
	}

	TArray<FEditorSerializedActorState> FilteredBefore;
	TArray<FEditorSerializedActorState> FilteredAfter;
	for (const FEditorSerializedActorState& BeforeState : BeforeStates)
	{
		if (!BeforeState.IsValid())
		{
			continue;
		}

		auto AfterIt = std::find_if(
			AfterStates.begin(),
			AfterStates.end(),
			[&BeforeState](const FEditorSerializedActorState& Candidate)
			{
				return Candidate.IsValid() && SameActorTarget(BeforeState.ActorRef, Candidate.ActorRef);
			});
		if (AfterIt == AfterStates.end() || BeforeState.ActorJson == AfterIt->ActorJson)
		{
			continue;
		}

		FilteredBefore.push_back(BeforeState);
		FilteredAfter.push_back(*AfterIt);
	}

	if (FilteredBefore.empty())
	{
		return false;
	}

	BeginTransaction(Label);
	AddCommand(std::make_unique<FSetSerializedActorStatesCommand>(
		std::move(FilteredBefore),
		std::move(FilteredAfter),
		Label));
	return EndTransaction();
}

FEditorSceneComponentTransformState FEditorUndoSystem::CaptureSceneComponentTransform(USceneComponent* Component) const
{
	FEditorSceneComponentTransformState State;
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring() || !IsValid(Component))
	{
		return State;
	}

	State.ComponentRef = FEditorUndoObjectResolver::MakeComponentRef(Owner, Component);
	if (!State.ComponentRef.IsValid() || !State.ComponentRef.HasComponent())
	{
		return FEditorSceneComponentTransformState();
	}

	State.RelativeLocation = Component->GetRelativeLocation();
	State.RelativeRotation = Component->GetRelativeRotation();
	State.RelativeScale = Component->GetRelativeScale();
	return State;
}

bool FEditorUndoSystem::RecordSceneComponentTransform(
	const FEditorSceneComponentTransformState& BeforeState,
	const FEditorSceneComponentTransformState& AfterState,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner)
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| !SameComponentTarget(BeforeState.ComponentRef, AfterState.ComponentRef)
		|| SameComponentTransform(BeforeState, AfterState))
	{
		return false;
	}

	BeginTransaction(Label);
	AddCommand(std::make_unique<FSetSceneComponentTransformCommand>(BeforeState, AfterState, Label));
	return EndTransaction();
}

FEditorReflectedPropertyState FEditorUndoSystem::CaptureReflectedProperty(UObject* Object, const FProperty& Property) const
{
	FEditorReflectedPropertyState State;
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring() || !IsValid(Object) || !Property.Name || (Property.Flags & PF_Transient) != 0)
	{
		return State;
	}

	if (AActor* Actor = Cast<AActor>(Object))
	{
		State.ObjectRef = FEditorUndoObjectResolver::MakeActorRef(Owner, Actor);
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		State.ObjectRef = FEditorUndoObjectResolver::MakeComponentRef(Owner, Component);
	}

	if (!State.ObjectRef.IsValid())
	{
		return FEditorReflectedPropertyState();
	}

	State.PropertyName = Property.Name;
	if (!SerializePropertyValue(Object, Property, State.ValueBytes))
	{
		return FEditorReflectedPropertyState();
	}
	return State;
}

bool FEditorUndoSystem::RecordReflectedProperty(
	const FEditorReflectedPropertyState& BeforeState,
	const FEditorReflectedPropertyState& AfterState,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner)
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| BeforeState.PropertyName != AfterState.PropertyName
		|| !SameComponentTarget(BeforeState.ObjectRef, AfterState.ObjectRef)
		|| BeforeState.ValueBytes == AfterState.ValueBytes)
	{
		return false;
	}

	BeginTransaction(Label);
	AddCommand(std::make_unique<FSetReflectedPropertyCommand>(BeforeState, AfterState, Label));
	return EndTransaction();
}

FEditorFileSystemState FEditorUndoSystem::CaptureFileSystemState(
	const FString& RootPath,
	const FString& Label) const
{
	FEditorFileSystemState State;
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring() || RootPath.empty())
	{
		return State;
	}

	const std::filesystem::path Root = ResolveUndoFileSystemPath(RootPath);
	if (!IsPathInsideUndoRoot(Root))
	{
		return State;
	}

	State.RootPath = NormalizeUndoFileSystemPath(Root);
	State.Label = Label;

	std::error_code Ec;
	if (!std::filesystem::exists(Root, Ec) || Ec)
	{
		return State;
	}

	auto CaptureEntry = [&State](const std::filesystem::path& Path)
	{
		FEditorFileSystemEntryState Entry;
		Entry.Path = NormalizeUndoFileSystemPath(Path);
		std::error_code EntryEc;
		Entry.bDirectory = std::filesystem::is_directory(Path, EntryEc);
		if (!Entry.bDirectory)
		{
			ReadFileBytesForUndo(Path, Entry.Data);
		}
		State.Entries.push_back(std::move(Entry));
	};

	CaptureEntry(Root);
	if (std::filesystem::is_directory(Root, Ec) && !Ec)
	{
		for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(Root, Ec))
		{
			if (Ec)
			{
				break;
			}
			CaptureEntry(Entry.path());
		}
	}
	return State;
}

bool FEditorUndoSystem::RecordCreateFileSystemPath(
	const FEditorFileSystemState& CreatedState,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring() || !CreatedState.IsValid() || CreatedState.Entries.empty())
	{
		return false;
	}

	BeginTransaction(Label);
	AddCommand(std::make_unique<FCreateFileSystemPathCommand>(CreatedState, Label));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordCreateFileSystemPaths(
	const TArray<FEditorFileSystemState>& CreatedStates,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring() || CreatedStates.empty())
	{
		return false;
	}

	BeginTransaction(Label);
	for (const FEditorFileSystemState& CreatedState : CreatedStates)
	{
		if (CreatedState.IsValid() && !CreatedState.Entries.empty())
		{
			AddCommand(std::make_unique<FCreateFileSystemPathCommand>(CreatedState, Label));
		}
	}
	return EndTransaction();
}

bool FEditorUndoSystem::RecordDeleteFileSystemPath(
	const FEditorFileSystemState& DeletedState,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner) || IsRestoring() || !DeletedState.IsValid() || DeletedState.Entries.empty())
	{
		return false;
	}

	BeginTransaction(Label);
	AddCommand(std::make_unique<FDeleteFileSystemPathCommand>(DeletedState, Label));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordModifyFileSystemPath(
	const FEditorFileSystemState& BeforeState,
	const FEditorFileSystemState& AfterState,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner)
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| BeforeState.Entries.empty()
		|| AfterState.Entries.empty()
		|| BeforeState.RootPath != AfterState.RootPath)
	{
		return false;
	}

	BeginTransaction(Label);
	AddCommand(std::make_unique<FModifyFileSystemPathCommand>(BeforeState, AfterState, Label));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordRenameFileSystemPath(
	const FEditorFileSystemState& BeforeState,
	const FEditorFileSystemState& AfterState,
	const FString& Label)
{
	if (!IsUndoRecordingAllowed(Owner)
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| BeforeState.Entries.empty()
		|| AfterState.Entries.empty()
		|| BeforeState.RootPath == AfterState.RootPath)
	{
		return false;
	}

	BeginTransaction(Label);
	AddCommand(std::make_unique<FRenameFileSystemPathCommand>(BeforeState, AfterState, Label));
	return EndTransaction();
}

void FEditorUndoSystem::ClearHistory()
{
	ClearHistory(GetActiveWorldHandle());
}

void FEditorUndoSystem::ClearHistory(const FName& WorldHandle)
{
	if (WorldHandle == FName::None)
	{
		return;
	}
	(void)WorldHandle;
	ClearAllHistory();
}

void FEditorUndoSystem::ClearAllHistory()
{
	UndoTransactions.clear();
	RedoTransactions.clear();
	ActiveTransaction.reset();
	RefreshHistoryLabels();
}

bool FEditorUndoSystem::CanUndo() const
{
	return !UndoTransactions.empty() && !bRestoring && !bApplyingUndoRedo;
}

bool FEditorUndoSystem::CanRedo() const
{
	return !RedoTransactions.empty() && !bRestoring && !bApplyingUndoRedo;
}

const TArray<FUndoSnapshotEntry>& FEditorUndoSystem::GetUndoHistory() const
{
	RefreshHistoryLabels();
	return UndoHistoryLabels;
}

const TArray<FUndoSnapshotEntry>& FEditorUndoSystem::GetRedoHistory() const
{
	RefreshHistoryLabels();
	return RedoHistoryLabels;
}

FEditorUndoContext FEditorUndoSystem::MakeContext() const
{
	FEditorUndoContext Context;
	Context.Editor = Owner;
	return Context;
}

FName FEditorUndoSystem::GetActiveWorldHandle() const
{
	return Owner ? Owner->GetActiveWorldHandle() : FName::None;
}

void FEditorUndoSystem::PushTransactionWithLimit(TArray<FEditorTransaction>& History, FEditorTransaction Transaction)
{
	if (History.size() >= MaxTransactionHistory)
	{
		History.erase(History.begin());
	}
	History.push_back(std::move(Transaction));
}

void FEditorUndoSystem::RefreshHistoryLabels() const
{
	auto Fill = [&](const TArray<FEditorTransaction>& Source, TArray<FUndoSnapshotEntry>& Target)
	{
		Target.clear();
		Target.reserve(Source.size());
		for (const FEditorTransaction& Transaction : Source)
		{
			FUndoSnapshotEntry Entry;
			Entry.WorldHandle = GetActiveWorldHandle();
			Entry.Label = GetTransactionLabel(Transaction);
			Target.push_back(std::move(Entry));
		}
	};

	Fill(UndoTransactions, UndoHistoryLabels);
	Fill(RedoTransactions, RedoHistoryLabels);
}

FScopedEditorTransaction::FScopedEditorTransaction(FEditorUndoSystem& InUndoSystem, const FString& Label)
	: UndoSystem(InUndoSystem)
{
	UndoSystem.BeginTransaction(Label);
}

FScopedEditorTransaction::~FScopedEditorTransaction()
{
	if (bCancelled)
	{
		UndoSystem.CancelTransaction();
	}
	else
	{
		UndoSystem.EndTransaction();
	}
}

void FScopedEditorTransaction::Cancel()
{
	bCancelled = true;
}
