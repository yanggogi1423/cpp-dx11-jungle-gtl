#include "Editor/Undo/EditorUndoSystem.h"

#include "Editor/EditorEngine.h"
#include "Editor/Asset/EditorAssetService.h"
#include "Editor/Notification/EditorNotificationService.h"
#include "Editor/Scene/EditorSceneService.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Settings/ProjectSettings.h"
#include "Component/ActorComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Asset/SkeletalMesh.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/Object.h"
#include "Render/Resource/Material.h"
#include "Serialization/ActorSerialization.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <utility>

namespace
{
	struct FDeletedActorRecord
	{
		FName WorldHandle = FName::None;
		FGuid ActorGuid;
		FString ActorName;
		json::JSON ActorData;
	};

	struct FComponentRecord
	{
		FEditorObjectRef ActorRef;
		FEditorObjectRef ComponentRef;
		FString ComponentName;
		json::JSON ComponentData;
		TArray<FEditorObjectRef> ChildComponentRefs;
	};

	struct FSceneComponentAttachmentState
	{
		FEditorObjectRef ComponentRef;
		FEditorObjectRef ParentRef;
		FName SocketName = FName::None;
	};

	struct FMovementUpdatedComponentState
	{
		FEditorObjectRef ComponentRef;
		FEditorObjectRef UpdatedComponentRef;
	};

	struct FMaterialSlotState
	{
		FEditorObjectRef ComponentRef;
		int32 SlotIndex = -1;
		FString MaterialIdentifier;
	};

	bool IsSameActorTransformTarget(const FEditorActorTransformState& A, const FEditorActorTransformState& B)
	{
		return A.ActorRef.WorldHandle == B.ActorRef.WorldHandle
			&& A.ActorRef.ActorGuid == B.ActorRef.ActorGuid;
	}

	bool HasDifferentTransform(const FEditorActorTransformState& A, const FEditorActorTransformState& B)
	{
		return !A.Location.Equals(B.Location, 1.e-4f)
			|| !A.Rotation.Equals(B.Rotation, 1.e-4f)
			|| !A.Scale.Equals(B.Scale, 1.e-4f);
	}

	bool IsSameObjectTarget(const FEditorObjectTagsState& A, const FEditorObjectTagsState& B)
	{
		return A.ObjectRef.WorldHandle == B.ObjectRef.WorldHandle
			&& A.ObjectRef.ActorGuid == B.ObjectRef.ActorGuid
			&& A.ObjectRef.ComponentGuid == B.ObjectRef.ComponentGuid;
	}

	bool AreTagsEqual(const TArray<FString>& A, const TArray<FString>& B)
	{
		if (A.size() != B.size())
		{
			return false;
		}

		for (size_t Index = 0; Index < A.size(); ++Index)
		{
			if (A[Index] != B[Index])
			{
				return false;
			}
		}
		return true;
	}

	bool IsSameObjectTarget(const FEditorObjectState& A, const FEditorObjectState& B)
	{
		return A.ObjectRef.WorldHandle == B.ObjectRef.WorldHandle
			&& A.ObjectRef.ActorGuid == B.ObjectRef.ActorGuid
			&& A.ObjectRef.ComponentGuid == B.ObjectRef.ComponentGuid;
	}

	bool IsSameBonePoseTarget(const FEditorSkeletalBonePoseState& A, const FEditorSkeletalBonePoseState& B)
	{
		return A.ComponentRef.WorldHandle == B.ComponentRef.WorldHandle
			&& A.ComponentRef.ActorGuid == B.ComponentRef.ActorGuid
			&& A.ComponentRef.ComponentGuid == B.ComponentRef.ComponentGuid;
	}

	bool AreBonePoseTransformsEqual(
		const FEditorSkeletalBonePoseState& A,
		const FEditorSkeletalBonePoseState& B)
	{
		if (A.BoneIndices.size() != B.BoneIndices.size()
			|| A.LocalTransforms.size() != B.LocalTransforms.size())
		{
			return false;
		}

		for (size_t Index = 0; Index < A.BoneIndices.size(); ++Index)
		{
			if (A.BoneIndices[Index] != B.BoneIndices[Index]
				|| !A.LocalTransforms[Index].Equals(B.LocalTransforms[Index], 1.e-4f))
			{
				return false;
			}
		}
		return true;
	}

	bool AreCurvesEqual(const FFloatCurve& A, const FFloatCurve& B)
	{
		if (A.Keys.size() != B.Keys.size())
		{
			return false;
		}

		for (size_t Index = 0; Index < A.Keys.size(); ++Index)
		{
			const FCurveKey& AK = A.Keys[Index];
			const FCurveKey& BK = B.Keys[Index];
			if (std::fabs(AK.Time - BK.Time) > 1.e-4f
				|| std::fabs(AK.Value - BK.Value) > 1.e-4f
				|| AK.InterpMode != BK.InterpMode
				|| AK.TangentMode != BK.TangentMode
				|| std::fabs(AK.ArriveTangent - BK.ArriveTangent) > 1.e-4f
				|| std::fabs(AK.LeaveTangent - BK.LeaveTangent) > 1.e-4f)
			{
				return false;
			}
		}
		return true;
	}

	bool AreMaterialParamsEqual(
		const TMap<FString, FMaterialParamValue>& A,
		const TMap<FString, FMaterialParamValue>& B)
	{
		if (A.size() != B.size())
		{
			return false;
		}

		for (const auto& [Name, ParamA] : A)
		{
			auto It = B.find(Name);
			if (It == B.end())
			{
				return false;
			}

			const FMaterialParamValue& ParamB = It->second;
			if (ParamA.Type != ParamB.Type || ParamA.Value != ParamB.Value)
			{
				return false;
			}
		}
		return true;
	}

	bool AreSocketsEqual(const TArray<FSkeletalMeshSocket>& A, const TArray<FSkeletalMeshSocket>& B)
	{
		if (A.size() != B.size())
		{
			return false;
		}

		for (size_t Index = 0; Index < A.size(); ++Index)
		{
			const FSkeletalMeshSocket& SA = A[Index];
			const FSkeletalMeshSocket& SB = B[Index];
			if (SA.Name != SB.Name
				|| SA.BoneIndex != SB.BoneIndex
				|| !SA.RelativeLocation.Equals(SB.RelativeLocation, 1.e-4f)
				|| !SA.RelativeScale.Equals(SB.RelativeScale, 1.e-4f)
				|| std::fabs(SA.RelativeRotation.Pitch - SB.RelativeRotation.Pitch) > 1.e-4f
				|| std::fabs(SA.RelativeRotation.Yaw - SB.RelativeRotation.Yaw) > 1.e-4f
				|| std::fabs(SA.RelativeRotation.Roll - SB.RelativeRotation.Roll) > 1.e-4f)
			{
				return false;
			}
		}
		return true;
	}

	bool AreStringArraysEqual(const TArray<FString>& A, const TArray<FString>& B)
	{
		if (A.size() != B.size())
		{
			return false;
		}

		for (size_t Index = 0; Index < A.size(); ++Index)
		{
			if (FPaths::Normalize(A[Index]) != FPaths::Normalize(B[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool AreGameBuildSettingsEqual(const FGameBuildSettings& A, const FGameBuildSettings& B)
	{
		return A.GameName == B.GameName
			&& FPaths::Normalize(A.StartupScene) == FPaths::Normalize(B.StartupScene)
			&& AreStringArraysEqual(A.IncludedScenes, B.IncludedScenes)
			&& A.GameModeClass == B.GameModeClass
			&& A.PlayerControllerClass == B.PlayerControllerClass
			&& A.DefaultPawnClass == B.DefaultPawnClass
			&& FPaths::Normalize(A.DefaultPawnPrefabPath) == FPaths::Normalize(B.DefaultPawnPrefabPath)
			&& FPaths::Normalize(A.OutputDirectory) == FPaths::Normalize(B.OutputDirectory)
			&& FPaths::Normalize(A.IconPath) == FPaths::Normalize(B.IconPath)
			&& FPaths::Normalize(A.SplashImagePath) == FPaths::Normalize(B.SplashImagePath)
			&& std::fabs(A.SplashMinSeconds - B.SplashMinSeconds) <= 1.e-4f
			&& A.Configuration == B.Configuration
			&& A.bCleanOutput == B.bCleanOutput
			&& A.bRunAfterBuild == B.bRunAfterBuild;
	}

	bool AreProjectSettingsEqual(const FEditorProjectSettingsState& A, const FEditorProjectSettingsState& B)
	{
		return FPaths::Normalize(A.SettingsPath) == FPaths::Normalize(B.SettingsPath)
			&& AreGameBuildSettingsEqual(A.BuildSettings, B.BuildSettings)
			&& FPaths::Normalize(A.LastScenePath) == FPaths::Normalize(B.LastScenePath);
	}

	bool AreWorldGameModeSettingsEqual(
		const FEditorWorldGameModeSettingsState& A,
		const FEditorWorldGameModeSettingsState& B)
	{
		return A.WorldHandle == B.WorldHandle
			&& A.bOverrideGameMode == B.bOverrideGameMode
			&& A.GameModeClass == B.GameModeClass
			&& A.PlayerControllerClass == B.PlayerControllerClass
			&& A.DefaultPawnClass == B.DefaultPawnClass
			&& FPaths::Normalize(A.DefaultPawnPrefabPath) == FPaths::Normalize(B.DefaultPawnPrefabPath);
	}

	std::filesystem::path ResolveUndoFileSystemPath(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(Path));
		if (!FsPath.is_absolute())
		{
			FsPath = std::filesystem::path(FPaths::RootDir()) / FsPath;
		}
		return FsPath.lexically_normal();
	}

	FString NormalizeUndoFileSystemPath(const std::filesystem::path& Path)
	{
		return FPaths::Normalize(FPaths::ToUtf8(Path.lexically_normal().generic_wstring()));
	}

	bool IsPathInsideUndoRoot(const std::filesystem::path& Path)
	{
		const std::filesystem::path Root = std::filesystem::path(FPaths::RootDir()).lexically_normal();
		const std::filesystem::path NormalizedPath = Path.lexically_normal();
		if (NormalizedPath == Root)
		{
			return false;
		}

		const std::filesystem::path Relative = NormalizedPath.lexically_relative(Root);
		if (Relative.empty())
		{
			return NormalizedPath == Root;
		}

		for (const std::filesystem::path& Part : Relative)
		{
			if (Part == L"..")
			{
				return false;
			}
		}
		return true;
	}

	bool ReadFileBytesForUndo(const std::filesystem::path& Path, TArray<uint8>& OutBytes)
	{
		std::ifstream File(Path, std::ios::binary);
		if (!File.is_open())
		{
			return false;
		}

		File.seekg(0, std::ios::end);
		const std::streamoff Size = File.tellg();
		File.seekg(0, std::ios::beg);
		if (Size < 0)
		{
			return false;
		}

		OutBytes.resize(static_cast<size_t>(Size));
		if (Size > 0)
		{
			File.read(reinterpret_cast<char*>(OutBytes.data()), Size);
		}
		return true;
	}

	bool RestoreFileSystemStateForUndo(const FEditorFileSystemState& State)
	{
		if (!State.IsValid())
		{
			return false;
		}

		for (const FEditorFileSystemEntryState& Entry : State.Entries)
		{
			std::filesystem::path EntryPath = ResolveUndoFileSystemPath(Entry.Path);
			if (!IsPathInsideUndoRoot(EntryPath))
			{
				return false;
			}

			std::error_code Ec;
			if (Entry.bDirectory)
			{
				std::filesystem::create_directories(EntryPath, Ec);
				if (Ec)
				{
					return false;
				}
				continue;
			}

			if (EntryPath.has_parent_path())
			{
				std::filesystem::create_directories(EntryPath.parent_path(), Ec);
				if (Ec)
				{
					return false;
				}
			}

			std::ofstream File(EntryPath, std::ios::binary | std::ios::trunc);
			if (!File.is_open())
			{
				return false;
			}
			if (!Entry.Data.empty())
			{
				File.write(reinterpret_cast<const char*>(Entry.Data.data()), static_cast<std::streamsize>(Entry.Data.size()));
			}
		}
		return true;
	}

	bool DeleteFileSystemRootForUndo(const FString& RootPath)
	{
		if (RootPath.empty())
		{
			return false;
		}

		const std::filesystem::path Path = ResolveUndoFileSystemPath(RootPath);
		if (!IsPathInsideUndoRoot(Path))
		{
			return false;
		}

		std::error_code Ec;
		if (std::filesystem::is_directory(Path, Ec))
		{
			std::filesystem::remove_all(Path, Ec);
		}
		else
		{
			std::filesystem::remove(Path, Ec);
		}
		return !Ec;
	}

	void RefreshAssetsAfterFileUndo(FEditorUndoContext& Context)
	{
		if (Context.Editor)
		{
			Context.Editor->GetAssetService().RefreshAssetDatabase();
			Context.Editor->GetMainPanel().RefreshContentBrowser();
		}
	}

	bool ApplyTagsToObject(UObject* Object, const TArray<FString>& Tags)
	{
		if (AActor* Actor = Cast<AActor>(Object))
		{
			Actor->ClearTags();
			for (const FString& Tag : Tags)
			{
				Actor->AddTag(Tag);
			}
			return true;
		}

		if (UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			Component->ClearTags();
			for (const FString& Tag : Tags)
			{
				Component->AddTag(Tag);
			}
			return true;
		}

		return false;
	}

	FWorldContext* GetWorldContextForActor(FEditorUndoContext& Context, AActor* Actor)
	{
		if (!Context.Editor || !Actor)
		{
			return nullptr;
		}
		return Context.Editor->GetWorldContextFromWorld(Actor->GetFocusedWorld());
	}

	bool DestroyComponentForUndo(FEditorUndoContext& Context, const FEditorObjectRef& ComponentRef)
	{
		UActorComponent* Component = Context.ResolveComponent(ComponentRef);
		if (!Component)
		{
			return false;
		}

		AActor* Owner = Component->GetOwner();
		if (!Owner || Component == Owner->GetRootComponent())
		{
			return false;
		}

		if (FWorldContext* WorldContext = GetWorldContextForActor(Context, Owner))
		{
			if (WorldContext->SelectionManager)
			{
				WorldContext->SelectionManager->OnComponentDestroyed(Component);
			}
		}

		Owner->RemoveComponent(Component);
		if (UWorld* World = Owner->GetFocusedWorld())
		{
			World->SyncSpatialIndex();
		}
		return true;
	}

	bool RestoreComponentForUndo(FEditorUndoContext& Context, FComponentRecord& Record, bool bSelectRestored)
	{
		if (Context.ResolveComponent(Record.ComponentRef))
		{
			return true;
		}

		AActor* Owner = Context.ResolveActor(Record.ActorRef);
		if (!Owner)
		{
			return false;
		}

		json::JSON ComponentData = Record.ComponentData;
		UActorComponent* RestoredComponent = FActorSerialization::AddComponentFromJson(Owner, ComponentData, true);
		if (!RestoredComponent)
		{
			return false;
		}

		if (USceneComponent* RestoredSceneComponent = Cast<USceneComponent>(RestoredComponent))
		{
			for (const FEditorObjectRef& ChildRef : Record.ChildComponentRefs)
			{
				USceneComponent* ChildComponent = Cast<USceneComponent>(Context.ResolveComponent(ChildRef));
				if (ChildComponent)
				{
					ChildComponent->AttachToComponent(RestoredSceneComponent);
				}
			}
		}

		if (bSelectRestored)
		{
			if (FWorldContext* WorldContext = GetWorldContextForActor(Context, Owner))
			{
				if (WorldContext->SelectionManager)
				{
					WorldContext->SelectionManager->Select(Owner);
					WorldContext->SelectionManager->SelectComponent(RestoredComponent);
				}
			}
		}
		return true;
	}

	size_t GetComponentRecordsMemoryUsage(const TArray<FComponentRecord>& Records)
	{
		size_t Total = Records.size() * sizeof(FComponentRecord);
		for (const FComponentRecord& Record : Records)
		{
			Total += Record.ComponentName.capacity();
			Total += Record.ComponentData.dump().capacity();
			Total += Record.ChildComponentRefs.size() * sizeof(FEditorObjectRef);
		}
		return Total;
	}

	bool ApplySceneComponentAttachmentState(FEditorUndoContext& Context, const FSceneComponentAttachmentState& State)
	{
		USceneComponent* Component = Cast<USceneComponent>(Context.ResolveComponent(State.ComponentRef));
		if (!Component)
		{
			return false;
		}

		USceneComponent* Parent = Cast<USceneComponent>(Context.ResolveComponent(State.ParentRef));
		if (Parent)
		{
			Component->AttachToComponent(Parent, State.SocketName);
		}
		else
		{
			Component->SetParent(nullptr);
		}

		Component->MarkTransformDirty();
		if (AActor* Owner = Component->GetOwner())
		{
			if (UWorld* World = Owner->GetFocusedWorld())
			{
				World->SyncSpatialIndex();
			}
		}
		return true;
	}

	bool ApplyMovementUpdatedComponentState(FEditorUndoContext& Context, const FMovementUpdatedComponentState& State)
	{
		UMovementComponent* Component = Cast<UMovementComponent>(Context.ResolveComponent(State.ComponentRef));
		if (!Component)
		{
			return false;
		}

		USceneComponent* UpdatedComponent = Cast<USceneComponent>(Context.ResolveComponent(State.UpdatedComponentRef));
		Component->SetUpdatedComponent(UpdatedComponent);
		return true;
	}

	FString GetMaterialIdentifier(UMaterialInterface* Material)
	{
		if (!Material)
		{
			return FString();
		}

		return Material->GetFilePath().empty()
			? Material->GetName()
			: FPaths::Normalize(Material->GetFilePath());
	}

	bool ApplyMaterialSlotState(FEditorUndoContext& Context, const FMaterialSlotState& State)
	{
		if (!Context.Editor || State.SlotIndex < 0)
		{
			return false;
		}

		UPrimitiveComponent* Component = Cast<UPrimitiveComponent>(Context.ResolveComponent(State.ComponentRef));
		if (!Component)
		{
			return false;
		}

		UMaterialInterface* Material = State.MaterialIdentifier.empty()
			? nullptr
			: Context.Editor->GetAssetService().ResolveMaterialInterface(State.MaterialIdentifier);
		Component->SetMaterial(State.SlotIndex, Material);
		Component->PostEditChangeProperty({ "Materials", EPropertyChangeType::ValueSet });
		Context.Editor->GetSceneService().MarkDirty();
		return true;
	}

	class FSetActorTransformsCommand final : public IEditorUndoCommand
	{
	public:
		FSetActorTransformsCommand(
			TArray<FEditorActorTransformState> InBeforeStates,
			TArray<FEditorActorTransformState> InAfterStates)
			: BeforeStates(std::move(InBeforeStates))
			, AfterStates(std::move(InAfterStates))
		{
		}

		FString GetLabel() const override
		{
			return BeforeStates.size() > 1 ? "Transform Actors" : "Transform Actor";
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyStates(Context, BeforeStates);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyStates(Context, AfterStates);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetActorTransformsCommand)
				+ BeforeStates.size() * sizeof(FEditorActorTransformState)
				+ AfterStates.size() * sizeof(FEditorActorTransformState);
		}

		bool IsEmpty() const { return BeforeStates.empty() || AfterStates.empty(); }

	private:
		bool ApplyStates(FEditorUndoContext& Context, const TArray<FEditorActorTransformState>& States)
		{
			if (!Context.Editor)
			{
				return false;
			}

			bool bAnyApplied = false;
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
				if (UWorld* World = Actor->GetFocusedWorld())
				{
					World->SyncSpatialIndex();
				}
				bAnyApplied = true;
			}

			if (bAnyApplied)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyApplied || States.empty();
		}

	private:
		TArray<FEditorActorTransformState> BeforeStates;
		TArray<FEditorActorTransformState> AfterStates;
	};

	class FSetObjectTagsCommand final : public IEditorUndoCommand
	{
	public:
		FSetObjectTagsCommand(
			TArray<FEditorObjectTagsState> InBeforeStates,
			TArray<FEditorObjectTagsState> InAfterStates)
			: BeforeStates(std::move(InBeforeStates))
			, AfterStates(std::move(InAfterStates))
		{
		}

		FString GetLabel() const override
		{
			return BeforeStates.size() > 1 ? "Edit Tags" : "Edit Tag";
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyStates(Context, BeforeStates);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyStates(Context, AfterStates);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetObjectTagsCommand)
				+ GetStatesMemoryUsage(BeforeStates)
				+ GetStatesMemoryUsage(AfterStates);
		}

		bool IsEmpty() const { return BeforeStates.empty() || AfterStates.empty(); }

	private:
		size_t GetStatesMemoryUsage(const TArray<FEditorObjectTagsState>& States) const
		{
			size_t Total = States.size() * sizeof(FEditorObjectTagsState);
			for (const FEditorObjectTagsState& State : States)
			{
				for (const FString& Tag : State.Tags)
				{
					Total += Tag.capacity();
				}
			}
			return Total;
		}

		bool ApplyStates(FEditorUndoContext& Context, const TArray<FEditorObjectTagsState>& States)
		{
			if (!Context.Editor)
			{
				return false;
			}

			bool bAnyApplied = false;
			for (const FEditorObjectTagsState& State : States)
			{
				UObject* Object = Context.ResolveObject(State.ObjectRef);
				if (!Object)
				{
					continue;
				}

				bAnyApplied |= ApplyTagsToObject(Object, State.Tags);
			}

			if (bAnyApplied)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyApplied || States.empty();
		}

	private:
		TArray<FEditorObjectTagsState> BeforeStates;
		TArray<FEditorObjectTagsState> AfterStates;
	};

	class FSetObjectStateCommand final : public IEditorUndoCommand
	{
	public:
		FSetObjectStateCommand(
			FEditorObjectState InBeforeState,
			FEditorObjectState InAfterState,
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

		FString GetLabel() const override
		{
			return Label;
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, BeforeState);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, AfterState);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetObjectStateCommand)
				+ Label.capacity()
				+ BeforeState.Data.dump().capacity()
				+ AfterState.Data.dump().capacity();
		}

	private:
		bool ApplyState(FEditorUndoContext& Context, FEditorObjectState& State)
		{
			if (!Context.Editor || !State.IsValid())
			{
				return false;
			}

			if (State.ObjectRef.ComponentGuid.IsValid())
			{
				UActorComponent* Component = Context.ResolveComponent(State.ObjectRef);
				if (!Component)
				{
					return false;
				}

				json::JSON Data = State.Data;
				if (!FActorSerialization::ApplyComponentJson(Component, Data, true))
				{
					return false;
				}
			}
			else
			{
				AActor* Actor = Context.ResolveActor(State.ObjectRef);
				if (!Actor)
				{
					return false;
				}

				json::JSON Data = State.Data;
				if (!FActorSerialization::ApplyActorJson(Actor, Data, true))
				{
					return false;
				}
			}

			Context.Editor->GetSceneService().MarkDirty();
			return true;
		}

	private:
		FEditorObjectState BeforeState;
		FEditorObjectState AfterState;
		FString Label;
	};

	class FSetObjectStatesCommand final : public IEditorUndoCommand
	{
	public:
		FSetObjectStatesCommand(
			TArray<FEditorObjectState> InBeforeStates,
			TArray<FEditorObjectState> InAfterStates,
			FString InLabel)
			: BeforeStates(std::move(InBeforeStates))
			, AfterStates(std::move(InAfterStates))
			, Label(std::move(InLabel))
		{
			if (Label.empty())
			{
				Label = "Edit Property";
			}
		}

		FString GetLabel() const override
		{
			return Label;
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyStates(Context, BeforeStates);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyStates(Context, AfterStates);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetObjectStatesCommand)
				+ Label.capacity()
				+ GetStatesMemoryUsage(BeforeStates)
				+ GetStatesMemoryUsage(AfterStates);
		}

	private:
		size_t GetStatesMemoryUsage(const TArray<FEditorObjectState>& States) const
		{
			size_t Total = States.size() * sizeof(FEditorObjectState);
			for (const FEditorObjectState& State : States)
			{
				Total += State.Label.capacity();
				Total += State.Data.dump().capacity();
			}
			return Total;
		}

		bool ApplyStates(FEditorUndoContext& Context, TArray<FEditorObjectState>& States)
		{
			if (!Context.Editor)
			{
				return false;
			}

			bool bAnyApplied = false;
			for (FEditorObjectState& State : States)
			{
				if (!State.IsValid())
				{
					continue;
				}

				if (State.ObjectRef.ComponentGuid.IsValid())
				{
					UActorComponent* Component = Context.ResolveComponent(State.ObjectRef);
					if (!Component)
					{
						continue;
					}

					json::JSON Data = State.Data;
					bAnyApplied |= FActorSerialization::ApplyComponentJson(Component, Data, true);
				}
				else
				{
					AActor* Actor = Context.ResolveActor(State.ObjectRef);
					if (!Actor)
					{
						continue;
					}

					json::JSON Data = State.Data;
					bAnyApplied |= FActorSerialization::ApplyActorJson(Actor, Data, true);
				}
			}

			if (bAnyApplied)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyApplied || States.empty();
		}

	private:
		TArray<FEditorObjectState> BeforeStates;
		TArray<FEditorObjectState> AfterStates;
		FString Label;
	};

	class FSetSkeletalBonePoseCommand final : public IEditorUndoCommand
	{
	public:
		FSetSkeletalBonePoseCommand(
			FEditorSkeletalBonePoseState InBeforeState,
			FEditorSkeletalBonePoseState InAfterState,
			FString InLabel)
			: BeforeState(std::move(InBeforeState))
			, AfterState(std::move(InAfterState))
			, Label(std::move(InLabel))
		{
			if (Label.empty())
			{
				Label = "Edit Bone Pose";
			}
		}

		FString GetLabel() const override
		{
			return Label;
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, BeforeState);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, AfterState);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetSkeletalBonePoseCommand)
				+ Label.capacity()
				+ GetStateMemoryUsage(BeforeState)
				+ GetStateMemoryUsage(AfterState);
		}

	private:
		size_t GetStateMemoryUsage(const FEditorSkeletalBonePoseState& State) const
		{
			return State.BoneIndices.size() * sizeof(int32)
				+ State.LocalTransforms.size() * sizeof(FMatrix);
		}

		bool ApplyState(FEditorUndoContext& Context, const FEditorSkeletalBonePoseState& State)
		{
			if (!Context.Editor || !State.IsValid())
			{
				return false;
			}

			USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(
				Context.ResolveComponent(State.ComponentRef));
			if (!Component)
			{
				return false;
			}

			bool bAnyApplied = false;
			for (size_t Index = 0; Index < State.BoneIndices.size(); ++Index)
			{
				Component->SetBoneLocalTransform(State.BoneIndices[Index], State.LocalTransforms[Index]);
				bAnyApplied = true;
			}

			if (bAnyApplied)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyApplied;
		}

	private:
		FEditorSkeletalBonePoseState BeforeState;
		FEditorSkeletalBonePoseState AfterState;
		FString Label;
	};

	class FSetCurveAssetStateCommand final : public IEditorUndoCommand
	{
	public:
		FSetCurveAssetStateCommand(
			FEditorCurveAssetState InBeforeState,
			FEditorCurveAssetState InAfterState,
			FString InLabel)
			: BeforeState(std::move(InBeforeState))
			, AfterState(std::move(InAfterState))
			, Label(std::move(InLabel))
		{
			if (Label.empty())
			{
				Label = "Edit Curve";
			}
		}

		FString GetLabel() const override
		{
			return Label;
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, BeforeState);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, AfterState);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetCurveAssetStateCommand)
				+ Label.capacity()
				+ GetStateMemoryUsage(BeforeState)
				+ GetStateMemoryUsage(AfterState);
		}

	private:
		size_t GetStateMemoryUsage(const FEditorCurveAssetState& State) const
		{
			return State.AssetRef.AssetPath.capacity()
				+ State.AssetRef.AssetType.capacity()
				+ State.Label.capacity()
				+ State.Curve.Keys.size() * sizeof(FCurveKey);
		}

		bool ApplyState(FEditorUndoContext& Context, const FEditorCurveAssetState& State)
		{
			if (!Context.Editor || !State.IsValid())
			{
				return false;
			}

			UCurveFloatAsset* Curve = FResourceManager::Get().LoadCurve(State.AssetRef.AssetPath);
			if (!Curve)
			{
				return false;
			}

			Curve->GetMutableCurve() = State.Curve;
			return true;
		}

	private:
		FEditorCurveAssetState BeforeState;
		FEditorCurveAssetState AfterState;
		FString Label;
	};

	class FSetMaterialStateCommand final : public IEditorUndoCommand
	{
	public:
		FSetMaterialStateCommand(
			FEditorMaterialState InBeforeState,
			FEditorMaterialState InAfterState,
			FString InLabel)
			: BeforeState(std::move(InBeforeState))
			, AfterState(std::move(InAfterState))
			, Label(std::move(InLabel))
		{
			if (Label.empty())
			{
				Label = "Edit Material";
			}
		}

		FString GetLabel() const override
		{
			return Label;
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, BeforeState);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, AfterState);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetMaterialStateCommand)
				+ Label.capacity()
				+ GetStateMemoryUsage(BeforeState)
				+ GetStateMemoryUsage(AfterState);
		}

	private:
		size_t GetStateMemoryUsage(const FEditorMaterialState& State) const
		{
			size_t Total = State.AssetRef.AssetPath.capacity()
				+ State.AssetRef.AssetType.capacity()
				+ State.Label.capacity()
				+ State.Params.size() * sizeof(FMaterialParamValue);
			for (const auto& [Name, Param] : State.Params)
			{
				Total += Name.capacity();
			}
			return Total;
		}

		bool ApplyState(FEditorUndoContext& Context, const FEditorMaterialState& State)
		{
			if (!Context.Editor || !State.IsValid())
			{
				return false;
			}

			UMaterialInterface* Material = FResourceManager::Get().GetMaterialInterface(State.AssetRef.AssetPath);
			if (!Material)
			{
				return false;
			}

			if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
			{
				Instance->OverridedParams = State.Params;
				Context.Editor->GetAssetService().SaveMaterialInstance(State.AssetRef.AssetPath, Instance);
				return true;
			}

			if (UMaterial* BaseMaterial = Cast<UMaterial>(Material))
			{
				BaseMaterial->MaterialParams = State.Params;
				FResourceManager::Get().SerializeMaterial(State.AssetRef.AssetPath, BaseMaterial);
				return true;
			}

			return false;
		}

	private:
		FEditorMaterialState BeforeState;
		FEditorMaterialState AfterState;
		FString Label;
	};

	class FSetSkeletalMeshSocketStateCommand final : public IEditorUndoCommand
	{
	public:
		FSetSkeletalMeshSocketStateCommand(
			FEditorSkeletalMeshSocketState InBeforeState,
			FEditorSkeletalMeshSocketState InAfterState,
			FString InLabel)
			: BeforeState(std::move(InBeforeState))
			, AfterState(std::move(InAfterState))
			, Label(std::move(InLabel))
		{
			if (Label.empty())
			{
				Label = "Edit Skeletal Mesh Socket";
			}
		}

		FString GetLabel() const override
		{
			return Label;
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, BeforeState);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, AfterState);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetSkeletalMeshSocketStateCommand)
				+ Label.capacity()
				+ GetStateMemoryUsage(BeforeState)
				+ GetStateMemoryUsage(AfterState);
		}

	private:
		size_t GetStateMemoryUsage(const FEditorSkeletalMeshSocketState& State) const
		{
			size_t Total = State.AssetRef.AssetPath.capacity()
				+ State.AssetRef.AssetType.capacity()
				+ State.Label.capacity()
				+ State.Sockets.size() * sizeof(FSkeletalMeshSocket);
			for (const FSkeletalMeshSocket& Socket : State.Sockets)
			{
				Total += Socket.Name.ToString().capacity();
			}
			return Total;
		}

		bool ApplyState(FEditorUndoContext& Context, const FEditorSkeletalMeshSocketState& State)
		{
			if (!Context.Editor || !State.IsValid())
			{
				return false;
			}

			USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(State.AssetRef.AssetPath);
			FSkeletalMesh* MeshData = Mesh ? Mesh->GetMeshData() : nullptr;
			if (!MeshData)
			{
				return false;
			}

			MeshData->Sockets = State.Sockets;
			FResourceManager::Get().SaveSkeletalMesh(Mesh);
			return true;
		}

	private:
		FEditorSkeletalMeshSocketState BeforeState;
		FEditorSkeletalMeshSocketState AfterState;
		FString Label;
	};

	class FSetProjectSettingsStateCommand final : public IEditorUndoCommand
	{
	public:
		FSetProjectSettingsStateCommand(
			FEditorProjectSettingsState InBeforeState,
			FEditorProjectSettingsState InAfterState,
			FString InLabel)
			: BeforeState(std::move(InBeforeState))
			, AfterState(std::move(InAfterState))
			, Label(std::move(InLabel))
		{
			if (Label.empty())
			{
				Label = "Edit Project Settings";
			}
		}

		FString GetLabel() const override
		{
			return Label;
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, BeforeState);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, AfterState);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetProjectSettingsStateCommand)
				+ Label.capacity()
				+ GetStateMemoryUsage(BeforeState)
				+ GetStateMemoryUsage(AfterState);
		}

	private:
		size_t GetBuildSettingsMemoryUsage(const FGameBuildSettings& Settings) const
		{
			size_t Total = Settings.GameName.capacity()
				+ Settings.StartupScene.capacity()
				+ Settings.GameModeClass.capacity()
				+ Settings.PlayerControllerClass.capacity()
				+ Settings.DefaultPawnClass.capacity()
				+ Settings.DefaultPawnPrefabPath.capacity()
				+ Settings.OutputDirectory.capacity()
				+ Settings.IconPath.capacity()
				+ Settings.SplashImagePath.capacity();
			for (const FString& Scene : Settings.IncludedScenes)
			{
				Total += Scene.capacity();
			}
			return Total;
		}

		size_t GetStateMemoryUsage(const FEditorProjectSettingsState& State) const
		{
			return State.SettingsPath.capacity()
				+ State.LastScenePath.capacity()
				+ State.Label.capacity()
				+ GetBuildSettingsMemoryUsage(State.BuildSettings);
		}

		bool ApplyState(FEditorUndoContext& Context, const FEditorProjectSettingsState& State)
		{
			if (!Context.Editor || !State.IsValid())
			{
				return false;
			}

			FProjectSettings& ProjectSettings = FProjectSettings::Get();
			ProjectSettings.BuildSettings = State.BuildSettings;
			ProjectSettings.LastScenePath = State.LastScenePath;
			ProjectSettings.SaveToFile(State.SettingsPath);
			return true;
		}

	private:
		FEditorProjectSettingsState BeforeState;
		FEditorProjectSettingsState AfterState;
		FString Label;
	};

	class FSetWorldGameModeSettingsCommand final : public IEditorUndoCommand
	{
	public:
		FSetWorldGameModeSettingsCommand(
			FEditorWorldGameModeSettingsState InBeforeState,
			FEditorWorldGameModeSettingsState InAfterState,
			FString InLabel)
			: BeforeState(std::move(InBeforeState))
			, AfterState(std::move(InAfterState))
			, Label(std::move(InLabel))
		{
			if (Label.empty())
			{
				Label = "Edit World Settings";
			}
		}

		FString GetLabel() const override
		{
			return Label;
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, BeforeState);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyState(Context, AfterState);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetWorldGameModeSettingsCommand)
				+ Label.capacity()
				+ GetStateMemoryUsage(BeforeState)
				+ GetStateMemoryUsage(AfterState);
		}

	private:
		size_t GetStateMemoryUsage(const FEditorWorldGameModeSettingsState& State) const
		{
			return State.GameModeClass.capacity()
				+ State.PlayerControllerClass.capacity()
				+ State.DefaultPawnClass.capacity()
				+ State.DefaultPawnPrefabPath.capacity()
				+ State.Label.capacity();
		}

		bool ApplyState(FEditorUndoContext& Context, const FEditorWorldGameModeSettingsState& State)
		{
			if (!Context.Editor || !State.IsValid())
			{
				return false;
			}

			FWorldContext* WorldContext = Context.Editor->GetWorldContextFromHandle(State.WorldHandle);
			UWorld* World = WorldContext ? WorldContext->World : nullptr;
			if (!World)
			{
				return false;
			}

			FWorldGameModeSettings Settings;
			Settings.bOverrideGameMode = State.bOverrideGameMode;
			Settings.GameModeClass = State.GameModeClass;
			Settings.PlayerControllerClass = State.PlayerControllerClass;
			Settings.DefaultPawnClass = State.DefaultPawnClass;
			Settings.DefaultPawnPrefabPath = State.DefaultPawnPrefabPath;
			World->SetGameModeSettings(Settings);
			Context.Editor->GetSceneService().MarkDirty();
			return true;
		}

	private:
		FEditorWorldGameModeSettingsState BeforeState;
		FEditorWorldGameModeSettingsState AfterState;
		FString Label;
	};

	class FCreateFileSystemPathCommand final : public IEditorUndoCommand
	{
	public:
		FCreateFileSystemPathCommand(FEditorFileSystemState InAfterState, FString InLabel)
			: AfterState(std::move(InAfterState))
			, Label(std::move(InLabel))
		{
			if (Label.empty())
			{
				Label = "Create Asset";
			}
		}

		FString GetLabel() const override { return Label; }

		bool Undo(FEditorUndoContext& Context) override
		{
			const bool bResult = DeleteFileSystemRootForUndo(AfterState.RootPath);
			RefreshAssetsAfterFileUndo(Context);
			return bResult;
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			const bool bResult = RestoreFileSystemStateForUndo(AfterState);
			RefreshAssetsAfterFileUndo(Context);
			return bResult;
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FCreateFileSystemPathCommand) + Label.capacity() + GetStateMemoryUsage(AfterState);
		}

	private:
		size_t GetStateMemoryUsage(const FEditorFileSystemState& State) const
		{
			size_t Total = State.RootPath.capacity() + State.Label.capacity();
			for (const FEditorFileSystemEntryState& Entry : State.Entries)
			{
				Total += Entry.Path.capacity() + Entry.Data.size();
			}
			return Total;
		}

	private:
		FEditorFileSystemState AfterState;
		FString Label;
	};

	class FDeleteFileSystemPathCommand final : public IEditorUndoCommand
	{
	public:
		FDeleteFileSystemPathCommand(FEditorFileSystemState InBeforeState, FString InLabel)
			: BeforeState(std::move(InBeforeState))
			, Label(std::move(InLabel))
		{
			if (Label.empty())
			{
				Label = "Delete Asset";
			}
		}

		FString GetLabel() const override { return Label; }

		bool Undo(FEditorUndoContext& Context) override
		{
			const bool bResult = RestoreFileSystemStateForUndo(BeforeState);
			RefreshAssetsAfterFileUndo(Context);
			return bResult;
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			const bool bResult = DeleteFileSystemRootForUndo(BeforeState.RootPath);
			RefreshAssetsAfterFileUndo(Context);
			return bResult;
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FDeleteFileSystemPathCommand) + Label.capacity() + GetStateMemoryUsage(BeforeState);
		}

	private:
		size_t GetStateMemoryUsage(const FEditorFileSystemState& State) const
		{
			size_t Total = State.RootPath.capacity() + State.Label.capacity();
			for (const FEditorFileSystemEntryState& Entry : State.Entries)
			{
				Total += Entry.Path.capacity() + Entry.Data.size();
			}
			return Total;
		}

	private:
		FEditorFileSystemState BeforeState;
		FString Label;
	};

	class FRenameFileSystemPathCommand final : public IEditorUndoCommand
	{
	public:
		FRenameFileSystemPathCommand(FString InOldPath, FString InNewPath, FString InLabel)
			: OldPath(std::move(InOldPath))
			, NewPath(std::move(InNewPath))
			, Label(std::move(InLabel))
		{
			if (Label.empty())
			{
				Label = "Rename Asset";
			}
		}

		FString GetLabel() const override { return Label; }

		bool Undo(FEditorUndoContext& Context) override
		{
			const bool bResult = RenamePath(NewPath, OldPath);
			RefreshAssetsAfterFileUndo(Context);
			return bResult;
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			const bool bResult = RenamePath(OldPath, NewPath);
			RefreshAssetsAfterFileUndo(Context);
			return bResult;
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FRenameFileSystemPathCommand)
				+ OldPath.capacity()
				+ NewPath.capacity()
				+ Label.capacity();
		}

	private:
		bool RenamePath(const FString& Source, const FString& Target) const
		{
			const std::filesystem::path SourcePath = ResolveUndoFileSystemPath(Source);
			const std::filesystem::path TargetPath = ResolveUndoFileSystemPath(Target);
			if (!IsPathInsideUndoRoot(SourcePath) || !IsPathInsideUndoRoot(TargetPath))
			{
				return false;
			}

			std::error_code Ec;
			if (!std::filesystem::exists(SourcePath, Ec) || std::filesystem::exists(TargetPath, Ec))
			{
				return false;
			}
			std::filesystem::rename(SourcePath, TargetPath, Ec);
			return !Ec;
		}

	private:
		FString OldPath;
		FString NewPath;
		FString Label;
	};

	class FRenameObjectCommand final : public IEditorUndoCommand
	{
	public:
		FRenameObjectCommand(const FEditorObjectRef& InObjectRef, const FName& InOldName, const FName& InNewName)
			: ObjectRef(InObjectRef)
			, OldName(InOldName)
			, NewName(InNewName)
		{
		}

		FString GetLabel() const override
		{
			return "Rename";
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyName(Context, OldName);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyName(Context, NewName);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FRenameObjectCommand)
				+ OldName.ToString().capacity()
				+ NewName.ToString().capacity();
		}

	private:
		bool ApplyName(FEditorUndoContext& Context, const FName& Name)
		{
			if (!Context.Editor)
			{
				return false;
			}

			UObject* Object = Context.ResolveObject(ObjectRef);
			if (!Object)
			{
				return false;
			}

			Object->SetFName(Name);
			Context.Editor->GetSceneService().MarkDirty();
			return true;
		}

	private:
		FEditorObjectRef ObjectRef;
		FName OldName;
		FName NewName;
	};

	class FCreateComponentsCommand final : public IEditorUndoCommand
	{
	public:
		FCreateComponentsCommand(UEditorEngine* Editor, const TArray<UActorComponent*>& Components)
		{
			if (!Editor)
			{
				return;
			}

			std::unordered_set<UActorComponent*> Seen;
			for (UActorComponent* Component : Components)
			{
				if (!Component || !Component->GetOwner() || Seen.contains(Component))
				{
					continue;
				}
				Seen.insert(Component);

				FComponentRecord Record;
				Record.ActorRef = FEditorUndoObjectResolver::MakeActorRef(Editor, Component->GetOwner());
				Record.ComponentRef = FEditorUndoObjectResolver::MakeComponentRef(Editor, Component);
				if (!Record.ActorRef.IsValid() || !Record.ComponentRef.IsValid())
				{
					continue;
				}
				Record.ComponentName = Component->GetName();
				Record.ComponentData = FActorSerialization::BuildComponentJson(Component);
				CreatedComponents.push_back(std::move(Record));
			}
		}

		FString GetLabel() const override
		{
			return CreatedComponents.size() > 1 ? "Create Components" : "Create Component";
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			bool bAnyDeleted = false;
			for (const FComponentRecord& Record : CreatedComponents)
			{
				bAnyDeleted |= DestroyComponentForUndo(Context, Record.ComponentRef);
			}
			if (bAnyDeleted && Context.Editor)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyDeleted || CreatedComponents.empty();
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			bool bAnyRestored = false;
			for (FComponentRecord& Record : CreatedComponents)
			{
				bAnyRestored |= RestoreComponentForUndo(Context, Record, true);
			}
			if (bAnyRestored && Context.Editor)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyRestored || CreatedComponents.empty();
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FCreateComponentsCommand) + GetComponentRecordsMemoryUsage(CreatedComponents);
		}

		bool IsEmpty() const { return CreatedComponents.empty(); }

	private:
		TArray<FComponentRecord> CreatedComponents;
	};

	class FDeleteComponentsCommand final : public IEditorUndoCommand
	{
	public:
		FDeleteComponentsCommand(UEditorEngine* Editor, const TArray<UActorComponent*>& Components)
		{
			if (!Editor)
			{
				return;
			}

			std::unordered_set<UActorComponent*> Seen;
			for (UActorComponent* Component : Components)
			{
				if (!Component || !Component->GetOwner() || Seen.contains(Component))
				{
					continue;
				}
				Seen.insert(Component);

				FComponentRecord Record;
				Record.ActorRef = FEditorUndoObjectResolver::MakeActorRef(Editor, Component->GetOwner());
				Record.ComponentRef = FEditorUndoObjectResolver::MakeComponentRef(Editor, Component);
				if (!Record.ActorRef.IsValid() || !Record.ComponentRef.IsValid())
				{
					continue;
				}
				Record.ComponentName = Component->GetName();
				Record.ComponentData = FActorSerialization::BuildComponentJson(Component);

				if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
				{
					for (USceneComponent* ChildComponent : SceneComponent->GetChildren())
					{
						if (ChildComponent)
						{
							FEditorObjectRef ChildRef = FEditorUndoObjectResolver::MakeComponentRef(Editor, ChildComponent);
							if (ChildRef.IsValid())
							{
								Record.ChildComponentRefs.push_back(ChildRef);
							}
						}
					}
				}

				DeletedComponents.push_back(std::move(Record));
			}
		}

		FString GetLabel() const override
		{
			return DeletedComponents.size() > 1 ? "Delete Components" : "Delete Component";
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			bool bAnyRestored = false;
			for (FComponentRecord& Record : DeletedComponents)
			{
				bAnyRestored |= RestoreComponentForUndo(Context, Record, true);
			}
			if (bAnyRestored && Context.Editor)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyRestored || DeletedComponents.empty();
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			bool bAnyDeleted = false;
			for (const FComponentRecord& Record : DeletedComponents)
			{
				bAnyDeleted |= DestroyComponentForUndo(Context, Record.ComponentRef);
			}
			if (bAnyDeleted && Context.Editor)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyDeleted || DeletedComponents.empty();
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FDeleteComponentsCommand) + GetComponentRecordsMemoryUsage(DeletedComponents);
		}

		bool IsEmpty() const { return DeletedComponents.empty(); }

	private:
		TArray<FComponentRecord> DeletedComponents;
	};

	class FSetSceneComponentAttachmentCommand final : public IEditorUndoCommand
	{
	public:
		FSetSceneComponentAttachmentCommand(
			const FSceneComponentAttachmentState& InBeforeState,
			const FSceneComponentAttachmentState& InAfterState)
			: BeforeState(InBeforeState)
			, AfterState(InAfterState)
		{
		}

		FString GetLabel() const override
		{
			return "Attach Component";
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			const bool bApplied = ApplySceneComponentAttachmentState(Context, BeforeState);
			if (bApplied && Context.Editor)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bApplied;
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			const bool bApplied = ApplySceneComponentAttachmentState(Context, AfterState);
			if (bApplied && Context.Editor)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bApplied;
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetSceneComponentAttachmentCommand);
		}

	private:
		FSceneComponentAttachmentState BeforeState;
		FSceneComponentAttachmentState AfterState;
	};

	class FSetMovementUpdatedComponentCommand final : public IEditorUndoCommand
	{
	public:
		FSetMovementUpdatedComponentCommand(
			const FMovementUpdatedComponentState& InBeforeState,
			const FMovementUpdatedComponentState& InAfterState)
			: BeforeState(InBeforeState)
			, AfterState(InAfterState)
		{
		}

		FString GetLabel() const override
		{
			return "Set Updated Component";
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			const bool bApplied = ApplyMovementUpdatedComponentState(Context, BeforeState);
			if (bApplied && Context.Editor)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bApplied;
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			const bool bApplied = ApplyMovementUpdatedComponentState(Context, AfterState);
			if (bApplied && Context.Editor)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bApplied;
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetMovementUpdatedComponentCommand);
		}

	private:
		FMovementUpdatedComponentState BeforeState;
		FMovementUpdatedComponentState AfterState;
	};

	class FSetMaterialSlotCommand final : public IEditorUndoCommand
	{
	public:
		FSetMaterialSlotCommand(const FMaterialSlotState& InBeforeState, const FMaterialSlotState& InAfterState)
			: BeforeState(InBeforeState)
			, AfterState(InAfterState)
		{
		}

		FString GetLabel() const override
		{
			return "Edit Material Slot";
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return ApplyMaterialSlotState(Context, BeforeState);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return ApplyMaterialSlotState(Context, AfterState);
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FSetMaterialSlotCommand)
				+ BeforeState.MaterialIdentifier.capacity()
				+ AfterState.MaterialIdentifier.capacity();
		}

	private:
		FMaterialSlotState BeforeState;
		FMaterialSlotState AfterState;
	};

	class FCreateActorsCommand final : public IEditorUndoCommand
	{
	public:
		FCreateActorsCommand(UEditorEngine* Editor, const TArray<AActor*>& Actors)
		{
			if (!Editor)
			{
				return;
			}

			std::unordered_set<AActor*> Seen;
			for (AActor* Actor : Actors)
			{
				if (!Actor || Seen.contains(Actor))
				{
					continue;
				}
				Seen.insert(Actor);

				FEditorObjectRef ActorRef = FEditorUndoObjectResolver::MakeActorRef(Editor, Actor);
				if (!ActorRef.IsValid())
				{
					continue;
				}

				FDeletedActorRecord Record;
				Record.WorldHandle = ActorRef.WorldHandle;
				Record.ActorGuid = ActorRef.ActorGuid;
				Record.ActorName = Actor->GetName();
				Record.ActorData = FActorSerialization::BuildActorJson(Actor);
				CreatedActors.push_back(std::move(Record));
			}
		}

		FString GetLabel() const override
		{
			return CreatedActors.size() > 1 ? "Create Actors" : "Create Actor";
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			if (!Context.Editor)
			{
				return false;
			}

			bool bAnyDeleted = false;
			for (const FDeletedActorRecord& Record : CreatedActors)
			{
				FEditorObjectRef ActorRef;
				ActorRef.WorldHandle = Record.WorldHandle;
				ActorRef.ActorGuid = Record.ActorGuid;

				AActor* Actor = Context.ResolveActor(ActorRef);
				if (!Actor)
				{
					continue;
				}

				FWorldContext* WorldContext = Context.Editor->GetWorldContextFromHandle(Record.WorldHandle);
				UWorld* World = WorldContext ? WorldContext->World : nullptr;
				if (!World)
				{
					continue;
				}

				if (WorldContext->SelectionManager)
				{
					WorldContext->SelectionManager->Deselect(Actor);
				}
				World->DestroyActor(Actor);
				bAnyDeleted = true;
			}

			if (bAnyDeleted)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyDeleted || CreatedActors.empty();
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			if (!Context.Editor)
			{
				return false;
			}

			bool bAnyRestored = false;
			for (FDeletedActorRecord& Record : CreatedActors)
			{
				if (Record.WorldHandle == FName::None || !Record.ActorGuid.IsValid())
				{
					continue;
				}

				FEditorObjectRef ExistingRef;
				ExistingRef.WorldHandle = Record.WorldHandle;
				ExistingRef.ActorGuid = Record.ActorGuid;
				if (Context.ResolveActor(ExistingRef))
				{
					continue;
				}

				FWorldContext* WorldContext = Context.Editor->GetWorldContextFromHandle(Record.WorldHandle);
				UWorld* World = WorldContext ? WorldContext->World : nullptr;
				if (!World)
				{
					continue;
				}

				json::JSON ActorData = Record.ActorData;
				FActorLoadOptions Options;
				Options.bPreserveUUIDs = true;
				Options.bPreserveName = true;
				Options.bMakeNameUnique = false;
				AActor* RestoredActor = FActorSerialization::SpawnActorFromJson(World, ActorData, Options);
				if (!RestoredActor)
				{
					continue;
				}

				if (WorldContext->SelectionManager)
				{
					WorldContext->SelectionManager->AddSelect(RestoredActor);
				}
				bAnyRestored = true;
			}

			if (bAnyRestored)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyRestored || CreatedActors.empty();
		}

		size_t GetMemoryUsage() const override
		{
			size_t Total = sizeof(FCreateActorsCommand);
			for (const FDeletedActorRecord& Record : CreatedActors)
			{
				Total += sizeof(FDeletedActorRecord);
				Total += Record.ActorName.capacity();
				Total += Record.ActorData.dump().capacity();
			}
			return Total;
		}

		bool IsEmpty() const { return CreatedActors.empty(); }

	private:
		TArray<FDeletedActorRecord> CreatedActors;
	};

	class FDeleteActorsCommand final : public IEditorUndoCommand
	{
	public:
		FDeleteActorsCommand(UEditorEngine* Editor, const TArray<AActor*>& Actors)
		{
			if (!Editor)
			{
				return;
			}

			std::unordered_set<AActor*> Seen;
			for (AActor* Actor : Actors)
			{
				if (!Actor || Seen.contains(Actor))
				{
					continue;
				}
				Seen.insert(Actor);

				FEditorObjectRef ActorRef = FEditorUndoObjectResolver::MakeActorRef(Editor, Actor);
				if (!ActorRef.IsValid())
				{
					continue;
				}

				FDeletedActorRecord Record;
				Record.WorldHandle = ActorRef.WorldHandle;
				Record.ActorGuid = ActorRef.ActorGuid;
				Record.ActorName = Actor->GetName();
				Record.ActorData = FActorSerialization::BuildActorJson(Actor);
				DeletedActors.push_back(std::move(Record));
			}
		}

		FString GetLabel() const override
		{
			return DeletedActors.size() > 1 ? "Delete Actors" : "Delete Actor";
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			if (!Context.Editor)
			{
				return false;
			}

			bool bAnyRestored = false;
			for (FDeletedActorRecord& Record : DeletedActors)
			{
				if (Record.WorldHandle == FName::None || !Record.ActorGuid.IsValid())
				{
					continue;
				}

				FEditorObjectRef ExistingRef;
				ExistingRef.WorldHandle = Record.WorldHandle;
				ExistingRef.ActorGuid = Record.ActorGuid;
				if (Context.ResolveActor(ExistingRef))
				{
					continue;
				}

				FWorldContext* WorldContext = Context.Editor->GetWorldContextFromHandle(Record.WorldHandle);
				UWorld* World = WorldContext ? WorldContext->World : nullptr;
				if (!World)
				{
					continue;
				}

				json::JSON ActorData = Record.ActorData;
				FActorLoadOptions Options;
				Options.bPreserveUUIDs = true;
				Options.bPreserveName = true;
				Options.bMakeNameUnique = false;
				AActor* RestoredActor = FActorSerialization::SpawnActorFromJson(World, ActorData, Options);
				if (!RestoredActor)
				{
					continue;
				}

				if (WorldContext->SelectionManager)
				{
					WorldContext->SelectionManager->AddSelect(RestoredActor);
				}
				bAnyRestored = true;
			}

			if (bAnyRestored)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyRestored || DeletedActors.empty();
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			if (!Context.Editor)
			{
				return false;
			}

			bool bAnyDeleted = false;
			for (const FDeletedActorRecord& Record : DeletedActors)
			{
				FEditorObjectRef ActorRef;
				ActorRef.WorldHandle = Record.WorldHandle;
				ActorRef.ActorGuid = Record.ActorGuid;

				AActor* Actor = Context.ResolveActor(ActorRef);
				if (!Actor)
				{
					continue;
				}

				FWorldContext* WorldContext = Context.Editor->GetWorldContextFromHandle(Record.WorldHandle);
				UWorld* World = WorldContext ? WorldContext->World : nullptr;
				if (!World)
				{
					continue;
				}

				if (WorldContext->SelectionManager)
				{
					WorldContext->SelectionManager->Deselect(Actor);
				}
				World->DestroyActor(Actor);
				bAnyDeleted = true;
			}

			if (bAnyDeleted)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAnyDeleted || DeletedActors.empty();
		}

		size_t GetMemoryUsage() const override
		{
			size_t Total = sizeof(FDeleteActorsCommand);
			for (const FDeletedActorRecord& Record : DeletedActors)
			{
				Total += sizeof(FDeletedActorRecord);
				Total += Record.ActorName.capacity();
				Total += Record.ActorData.dump().capacity();
			}
			return Total;
		}

		bool IsEmpty() const { return DeletedActors.empty(); }

	private:
		TArray<FDeletedActorRecord> DeletedActors;
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

UObject* FEditorUndoContext::ResolveObject(const FEditorObjectRef& Ref) const
{
	return FEditorUndoObjectResolver::ResolveObject(Editor, Ref);
}

FEditorObjectRef FEditorUndoObjectResolver::MakeActorRef(UEditorEngine* Editor, AActor* Actor)
{
	FEditorObjectRef Ref;
	if (!Editor || !Actor)
	{
		return Ref;
	}

	Actor->EnsurePersistentGuid();
	const FWorldContext* Context = Editor->GetWorldContextFromWorld(Actor->GetFocusedWorld());
	if (!Context)
	{
		return Ref;
	}

	Ref.WorldHandle = Context->ContextHandle;
	Ref.ActorGuid = Actor->GetPersistentGuid();
	return Ref;
}

FEditorObjectRef FEditorUndoObjectResolver::MakeComponentRef(UEditorEngine* Editor, UActorComponent* Component)
{
	if (!Editor || !Component || !Component->GetOwner())
	{
		return FEditorObjectRef();
	}

	FEditorObjectRef Ref = MakeActorRef(Editor, Component->GetOwner());
	if (!Ref.HasActor())
	{
		return FEditorObjectRef();
	}

	Component->EnsurePersistentGuid();
	Ref.ComponentGuid = Component->GetPersistentGuid();
	return Ref;
}

AActor* FEditorUndoObjectResolver::ResolveActor(UEditorEngine* Editor, const FEditorObjectRef& Ref)
{
	if (!Editor || Ref.WorldHandle == FName::None || !Ref.ActorGuid.IsValid())
	{
		return nullptr;
	}

	const FWorldContext* Context = Editor->GetWorldContextFromHandle(Ref.WorldHandle);
	UWorld* World = Context ? Context->World : nullptr;
	if (!World)
	{
		return nullptr;
	}

	for (AActor* Actor : World->GetActors())
	{
		if (Actor && Actor->GetPersistentGuid() == Ref.ActorGuid)
		{
			return Actor;
		}
	}
	return nullptr;
}

UActorComponent* FEditorUndoObjectResolver::ResolveComponent(UEditorEngine* Editor, const FEditorObjectRef& Ref)
{
	if (!Ref.ComponentGuid.IsValid())
	{
		return nullptr;
	}

	AActor* Actor = ResolveActor(Editor, Ref);
	if (!Actor)
	{
		return nullptr;
	}

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (Component && Component->GetPersistentGuid() == Ref.ComponentGuid)
		{
			return Component;
		}
	}
	return nullptr;
}

UObject* FEditorUndoObjectResolver::ResolveObject(UEditorEngine* Editor, const FEditorObjectRef& Ref)
{
	if (Ref.ComponentGuid.IsValid())
	{
		return ResolveComponent(Editor, Ref);
	}
	return ResolveActor(Editor, Ref);
}

bool FEditorTransaction::Undo(FEditorUndoContext& Context)
{
	for (auto It = Commands.rbegin(); It != Commands.rend(); ++It)
	{
		if (!(*It) || !(*It)->Undo(Context))
		{
			return false;
		}
	}
	return true;
}

bool FEditorTransaction::Redo(FEditorUndoContext& Context)
{
	for (const std::unique_ptr<IEditorUndoCommand>& Command : Commands)
	{
		if (!Command || !Command->Redo(Context))
		{
			return false;
		}
	}
	return true;
}

size_t FEditorTransaction::GetMemoryUsage() const
{
	size_t Total = Label.capacity() + sizeof(FEditorTransaction);
	for (const std::unique_ptr<IEditorUndoCommand>& Command : Commands)
	{
		Total += sizeof(Command);
		if (Command)
		{
			Total += Command->GetMemoryUsage();
		}
	}
	return std::max(Total, CachedMemoryUsage);
}

void FEditorUndoSystem::BeginTransaction(const FString& Label)
{
	if (bApplyingUndoRedo)
	{
		return;
	}

	if (ActiveTransaction)
	{
		CancelTransaction();
	}

	ActiveTransaction = std::make_unique<FEditorTransaction>();
	ActiveTransaction->Label = Label.empty() ? FString("Editor Transaction") : Label;
}

bool FEditorUndoSystem::AddCommand(std::unique_ptr<IEditorUndoCommand> Command)
{
	if (bApplyingUndoRedo || !Command)
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

	ActiveTransaction->CachedMemoryUsage += Command->GetMemoryUsage();
	ActiveTransaction->Commands.push_back(std::move(Command));
	bPendingMutationCapture = false;
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

	RedoStack.clear();
	PushUndoTransaction(std::move(*ActiveTransaction));
	ActiveTransaction.reset();
	bPendingMutationCapture = false;
	++TransactionRevision;
	TrimHistoryToLimits();
	return true;
}

void FEditorUndoSystem::CancelTransaction()
{
	ActiveTransaction.reset();
	bPendingMutationCapture = false;
}

bool FEditorUndoSystem::Undo()
{
	if (!Owner || bApplyingUndoRedo || UndoStack.empty())
	{
		return false;
	}

	FEditorTransaction Transaction = std::move(UndoStack.back());
	UndoStack.pop_back();

	FEditorUndoContext Context = MakeContext();
	bApplyingUndoRedo = true;
	const bool bSuccess = Transaction.Undo(Context);
	bApplyingUndoRedo = false;

	if (!bSuccess)
	{
		PushUndoTransaction(std::move(Transaction));
		if (Owner)
		{
			Owner->GetNotificationService().Error("Undo failed");
		}
		return false;
	}

	if (Owner)
	{
		Owner->GetNotificationService().Info("Undo: " + Transaction.Label);
	}
	PushRedoTransaction(std::move(Transaction));
	return true;
}

bool FEditorUndoSystem::Redo()
{
	if (!Owner || bApplyingUndoRedo || RedoStack.empty())
	{
		return false;
	}

	FEditorTransaction Transaction = std::move(RedoStack.back());
	RedoStack.pop_back();

	FEditorUndoContext Context = MakeContext();
	bApplyingUndoRedo = true;
	const bool bSuccess = Transaction.Redo(Context);
	bApplyingUndoRedo = false;

	if (!bSuccess)
	{
		PushRedoTransaction(std::move(Transaction));
		if (Owner)
		{
			Owner->GetNotificationService().Error("Redo failed");
		}
		return false;
	}

	if (Owner)
	{
		Owner->GetNotificationService().Info("Redo: " + Transaction.Label);
	}
	PushUndoTransaction(std::move(Transaction));
	TrimHistoryToLimits();
	return true;
}

bool FEditorUndoSystem::RestoreHistoryIndex(int32 Index)
{
	if (Index < 0 || Index >= static_cast<int32>(UndoStack.size()))
	{
		return false;
	}

	bool bAnyRestored = false;
	while (static_cast<int32>(UndoStack.size()) > Index)
	{
		if (!Undo())
		{
			return bAnyRestored;
		}
		bAnyRestored = true;
	}
	return bAnyRestored;
}

TArray<FEditorActorTransformState> FEditorUndoSystem::CaptureActorTransforms(const TArray<AActor*>& Actors) const
{
	TArray<FEditorActorTransformState> States;
	if (!Owner)
	{
		return States;
	}
	NoteMutationCapture();

	std::unordered_set<AActor*> Seen;
	for (AActor* Actor : Actors)
	{
		if (!Actor || Seen.contains(Actor))
		{
			continue;
		}
		Seen.insert(Actor);

		FEditorActorTransformState State;
		State.ActorRef = FEditorUndoObjectResolver::MakeActorRef(Owner, Actor);
		if (!State.ActorRef.IsValid())
		{
			continue;
		}
		State.Location = Actor->GetActorLocation();
		State.Rotation = Actor->GetActorRotation();
		State.Scale = Actor->GetActorScale();
		States.push_back(State);
	}
	return States;
}

bool FEditorUndoSystem::RecordActorTransforms(
	const TArray<FEditorActorTransformState>& BeforeStates,
	const TArray<FEditorActorTransformState>& AfterStates)
{
	if (!Owner || bApplyingUndoRedo || BeforeStates.empty() || AfterStates.empty())
	{
		return false;
	}

	TArray<FEditorActorTransformState> FilteredBeforeStates;
	TArray<FEditorActorTransformState> FilteredAfterStates;
	for (const FEditorActorTransformState& BeforeState : BeforeStates)
	{
		if (!BeforeState.IsValid())
		{
			continue;
		}

		for (const FEditorActorTransformState& AfterState : AfterStates)
		{
			if (!AfterState.IsValid() || !IsSameActorTransformTarget(BeforeState, AfterState))
			{
				continue;
			}

			if (HasDifferentTransform(BeforeState, AfterState))
			{
				FilteredBeforeStates.push_back(BeforeState);
				FilteredAfterStates.push_back(AfterState);
			}
			break;
		}
	}

	if (FilteredBeforeStates.empty())
	{
		return false;
	}

	std::unique_ptr<FSetActorTransformsCommand> Command = std::make_unique<FSetActorTransformsCommand>(
		std::move(FilteredBeforeStates),
		std::move(FilteredAfterStates));
	if (!Command || Command->IsEmpty())
	{
		return false;
	}

	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

TArray<FEditorObjectTagsState> FEditorUndoSystem::CaptureObjectTags(const TArray<UObject*>& Objects) const
{
	TArray<FEditorObjectTagsState> States;
	if (!Owner)
	{
		return States;
	}
	NoteMutationCapture();

	std::unordered_set<UObject*> Seen;
	for (UObject* Object : Objects)
	{
		if (!Object || Seen.contains(Object))
		{
			continue;
		}
		Seen.insert(Object);

		FEditorObjectTagsState State;
		if (AActor* Actor = Cast<AActor>(Object))
		{
			State.ObjectRef = FEditorUndoObjectResolver::MakeActorRef(Owner, Actor);
			State.Tags = Actor->GetTags();
		}
		else if (UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			State.ObjectRef = FEditorUndoObjectResolver::MakeComponentRef(Owner, Component);
			State.Tags = Component->GetTags();
		}

		if (State.IsValid())
		{
			States.push_back(std::move(State));
		}
	}
	return States;
}

bool FEditorUndoSystem::RecordObjectTags(
	const TArray<FEditorObjectTagsState>& BeforeStates,
	const TArray<FEditorObjectTagsState>& AfterStates)
{
	if (!Owner || bApplyingUndoRedo || BeforeStates.empty() || AfterStates.empty())
	{
		return false;
	}

	TArray<FEditorObjectTagsState> FilteredBeforeStates;
	TArray<FEditorObjectTagsState> FilteredAfterStates;
	for (const FEditorObjectTagsState& BeforeState : BeforeStates)
	{
		if (!BeforeState.IsValid())
		{
			continue;
		}

		for (const FEditorObjectTagsState& AfterState : AfterStates)
		{
			if (!AfterState.IsValid() || !IsSameObjectTarget(BeforeState, AfterState))
			{
				continue;
			}

			if (!AreTagsEqual(BeforeState.Tags, AfterState.Tags))
			{
				FilteredBeforeStates.push_back(BeforeState);
				FilteredAfterStates.push_back(AfterState);
			}
			break;
		}
	}

	if (FilteredBeforeStates.empty())
	{
		return false;
	}

	std::unique_ptr<FSetObjectTagsCommand> Command = std::make_unique<FSetObjectTagsCommand>(
		std::move(FilteredBeforeStates),
		std::move(FilteredAfterStates));
	if (!Command || Command->IsEmpty())
	{
		return false;
	}

	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorObjectState FEditorUndoSystem::CaptureObjectState(UObject* Object, const FString& Label) const
{
	FEditorObjectState State;
	if (!Owner || !Object)
	{
		return State;
	}
	NoteMutationCapture();

	State.Label = Label;
	if (AActor* Actor = Cast<AActor>(Object))
	{
		State.ObjectRef = FEditorUndoObjectResolver::MakeActorRef(Owner, Actor);
		if (State.ObjectRef.IsValid())
		{
			State.Data = FActorSerialization::BuildActorJson(Actor);
		}
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		State.ObjectRef = FEditorUndoObjectResolver::MakeComponentRef(Owner, Component);
		if (State.ObjectRef.IsValid())
		{
			State.Data = FActorSerialization::BuildComponentJson(Component);
		}
	}

	return State;
}

TArray<FEditorObjectState> FEditorUndoSystem::CaptureObjectStates(const TArray<UObject*>& Objects, const FString& Label) const
{
	TArray<FEditorObjectState> States;
	if (!Owner)
	{
		return States;
	}

	std::unordered_set<UObject*> Seen;
	for (UObject* Object : Objects)
	{
		if (!Object || Seen.contains(Object))
		{
			continue;
		}
		Seen.insert(Object);

		FEditorObjectState State = CaptureObjectState(Object, Label);
		if (State.IsValid())
		{
			States.push_back(std::move(State));
		}
	}
	return States;
}

bool FEditorUndoSystem::RecordObjectState(
	const FEditorObjectState& BeforeState,
	const FEditorObjectState& AfterState,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || !BeforeState.IsValid() || !AfterState.IsValid())
	{
		return false;
	}

	if (!IsSameObjectTarget(BeforeState, AfterState))
	{
		return false;
	}

	const FString BeforeJson = BeforeState.Data.dump();
	const FString AfterJson = AfterState.Data.dump();
	if (BeforeJson == AfterJson)
	{
		return false;
	}

	const FString CommandLabel = Label.empty()
		? (!AfterState.Label.empty() ? AfterState.Label : "Edit Property")
		: Label;
	std::unique_ptr<FSetObjectStateCommand> Command = std::make_unique<FSetObjectStateCommand>(
		BeforeState,
		AfterState,
		CommandLabel);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordObjectStates(
	const TArray<FEditorObjectState>& BeforeStates,
	const TArray<FEditorObjectState>& AfterStates,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || BeforeStates.empty() || AfterStates.empty())
	{
		return false;
	}

	TArray<FEditorObjectState> FilteredBeforeStates;
	TArray<FEditorObjectState> FilteredAfterStates;
	for (const FEditorObjectState& BeforeState : BeforeStates)
	{
		if (!BeforeState.IsValid())
		{
			continue;
		}

		for (const FEditorObjectState& AfterState : AfterStates)
		{
			if (!AfterState.IsValid() || !IsSameObjectTarget(BeforeState, AfterState))
			{
				continue;
			}

			if (BeforeState.Data.dump() != AfterState.Data.dump())
			{
				FilteredBeforeStates.push_back(BeforeState);
				FilteredAfterStates.push_back(AfterState);
			}
			break;
		}
	}

	if (FilteredBeforeStates.empty())
	{
		return false;
	}

	std::unique_ptr<FSetObjectStatesCommand> Command = std::make_unique<FSetObjectStatesCommand>(
		std::move(FilteredBeforeStates),
		std::move(FilteredAfterStates),
		Label.empty() ? FString("Edit Property") : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorSkeletalBonePoseState FEditorUndoSystem::CaptureSkeletalBonePose(
	USkeletalMeshComponent* Component,
	int32 BoneIndex) const
{
	FEditorSkeletalBonePoseState State;
	if (!Owner || !Component)
	{
		return State;
	}
	NoteMutationCapture();

	State.ComponentRef = FEditorUndoObjectResolver::MakeComponentRef(Owner, Component);
	if (!State.ComponentRef.IsValid() || !State.ComponentRef.ComponentGuid.IsValid())
	{
		return State;
	}

	USkeletalMesh* Mesh = Component->GetSkeletalMesh();
	if (!Mesh)
	{
		return State;
	}

	const int32 BoneCount = static_cast<int32>(Mesh->GetBones().size());
	if (BoneIndex >= 0)
	{
		if (BoneIndex >= BoneCount)
		{
			return State;
		}

		State.BoneIndices.push_back(BoneIndex);
		State.LocalTransforms.push_back(Component->GetBoneLocalTransform(BoneIndex));
		return State;
	}

	State.BoneIndices.reserve(BoneCount);
	State.LocalTransforms.reserve(BoneCount);
	for (int32 Index = 0; Index < BoneCount; ++Index)
	{
		State.BoneIndices.push_back(Index);
		State.LocalTransforms.push_back(Component->GetBoneLocalTransform(Index));
	}
	return State;
}

bool FEditorUndoSystem::RecordSkeletalBonePose(
	const FEditorSkeletalBonePoseState& BeforeState,
	const FEditorSkeletalBonePoseState& AfterState,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || !BeforeState.IsValid() || !AfterState.IsValid())
	{
		return false;
	}

	if (!IsSameBonePoseTarget(BeforeState, AfterState))
	{
		return false;
	}

	if (AreBonePoseTransformsEqual(BeforeState, AfterState))
	{
		return false;
	}

	std::unique_ptr<FSetSkeletalBonePoseCommand> Command = std::make_unique<FSetSkeletalBonePoseCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? FString("Edit Bone Pose") : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorCurveAssetState FEditorUndoSystem::CaptureCurveAssetState(
	UCurveFloatAsset* Curve,
	const FString& AssetPath,
	const FString& Label) const
{
	FEditorCurveAssetState State;
	if (!Owner || !Curve || AssetPath.empty())
	{
		return State;
	}
	NoteMutationCapture();

	State.AssetRef.AssetPath = FPaths::Normalize(AssetPath);
	State.AssetRef.AssetType = "CurveFloat";
	State.Curve = Curve->GetCurve();
	State.Label = Label;
	return State;
}

bool FEditorUndoSystem::RecordCurveAssetState(
	const FEditorCurveAssetState& BeforeState,
	const FEditorCurveAssetState& AfterState,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || !BeforeState.IsValid() || !AfterState.IsValid())
	{
		return false;
	}

	if (FPaths::Normalize(BeforeState.AssetRef.AssetPath) != FPaths::Normalize(AfterState.AssetRef.AssetPath))
	{
		return false;
	}

	if (AreCurvesEqual(BeforeState.Curve, AfterState.Curve))
	{
		return false;
	}

	std::unique_ptr<FSetCurveAssetStateCommand> Command = std::make_unique<FSetCurveAssetStateCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? FString("Edit Curve") : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorMaterialState FEditorUndoSystem::CaptureMaterialState(
	UMaterialInterface* Material,
	const FString& Label) const
{
	FEditorMaterialState State;
	if (!Owner || !Material)
	{
		return State;
	}
	NoteMutationCapture();

	const FString MaterialPath = FPaths::Normalize(Material->GetFilePath());
	if (MaterialPath.empty())
	{
		return State;
	}

	State.AssetRef.AssetPath = MaterialPath;
	State.AssetRef.AssetType = Material->IsA<UMaterialInstance>() ? "MaterialInstance" : "Material";
	State.Label = Label;
	if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
	{
		State.Params = Instance->OverridedParams;
	}
	else if (UMaterial* BaseMaterial = Cast<UMaterial>(Material))
	{
		State.Params = BaseMaterial->MaterialParams;
	}
	return State;
}

bool FEditorUndoSystem::RecordMaterialState(
	const FEditorMaterialState& BeforeState,
	const FEditorMaterialState& AfterState,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || !BeforeState.IsValid() || !AfterState.IsValid())
	{
		return false;
	}

	if (FPaths::Normalize(BeforeState.AssetRef.AssetPath) != FPaths::Normalize(AfterState.AssetRef.AssetPath))
	{
		return false;
	}

	if (AreMaterialParamsEqual(BeforeState.Params, AfterState.Params))
	{
		return false;
	}

	std::unique_ptr<FSetMaterialStateCommand> Command = std::make_unique<FSetMaterialStateCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? FString("Edit Material") : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorSkeletalMeshSocketState FEditorUndoSystem::CaptureSkeletalMeshSocketState(
	USkeletalMesh* Mesh,
	const FString& Label) const
{
	FEditorSkeletalMeshSocketState State;
	if (!Owner || !Mesh)
	{
		return State;
	}
	NoteMutationCapture();

	const FString MeshPath = FPaths::Normalize(Mesh->GetAssetPathFileName());
	FSkeletalMesh* MeshData = Mesh->GetMeshData();
	if (MeshPath.empty() || !MeshData)
	{
		return State;
	}

	State.AssetRef.AssetPath = MeshPath;
	State.AssetRef.AssetType = "SkeletalMesh";
	State.Sockets = MeshData->Sockets;
	State.Label = Label;
	return State;
}

bool FEditorUndoSystem::RecordSkeletalMeshSocketState(
	const FEditorSkeletalMeshSocketState& BeforeState,
	const FEditorSkeletalMeshSocketState& AfterState,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || !BeforeState.IsValid() || !AfterState.IsValid())
	{
		return false;
	}

	if (FPaths::Normalize(BeforeState.AssetRef.AssetPath) != FPaths::Normalize(AfterState.AssetRef.AssetPath))
	{
		return false;
	}

	if (AreSocketsEqual(BeforeState.Sockets, AfterState.Sockets))
	{
		return false;
	}

	std::unique_ptr<FSetSkeletalMeshSocketStateCommand> Command = std::make_unique<FSetSkeletalMeshSocketStateCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? FString("Edit Skeletal Mesh Socket") : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorProjectSettingsState FEditorUndoSystem::CaptureProjectSettings(const FString& Label) const
{
	FEditorProjectSettingsState State;
	if (!Owner)
	{
		return State;
	}
	NoteMutationCapture();

	FProjectSettings& ProjectSettings = FProjectSettings::Get();
	State.SettingsPath = FProjectSettings::GetDefaultSettingsPath();
	State.BuildSettings = ProjectSettings.BuildSettings;
	State.LastScenePath = ProjectSettings.LastScenePath;
	State.Label = Label;
	return State;
}

bool FEditorUndoSystem::RecordProjectSettings(
	const FEditorProjectSettingsState& BeforeState,
	const FEditorProjectSettingsState& AfterState,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || !BeforeState.IsValid() || !AfterState.IsValid())
	{
		return false;
	}

	if (FPaths::Normalize(BeforeState.SettingsPath) != FPaths::Normalize(AfterState.SettingsPath))
	{
		return false;
	}

	if (AreProjectSettingsEqual(BeforeState, AfterState))
	{
		return false;
	}

	std::unique_ptr<FSetProjectSettingsStateCommand> Command = std::make_unique<FSetProjectSettingsStateCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? FString("Edit Project Settings") : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorWorldGameModeSettingsState FEditorUndoSystem::CaptureWorldGameModeSettings(
	UWorld* World,
	const FString& Label) const
{
	FEditorWorldGameModeSettingsState State;
	if (!Owner || !World)
	{
		return State;
	}
	NoteMutationCapture();

	const FWorldContext* Context = Owner->GetWorldContextFromWorld(World);
	if (!Context)
	{
		return State;
	}

	const FWorldGameModeSettings& Settings = World->GetGameModeSettings();
	State.WorldHandle = Context->ContextHandle;
	State.bOverrideGameMode = Settings.bOverrideGameMode;
	State.GameModeClass = Settings.GameModeClass;
	State.PlayerControllerClass = Settings.PlayerControllerClass;
	State.DefaultPawnClass = Settings.DefaultPawnClass;
	State.DefaultPawnPrefabPath = Settings.DefaultPawnPrefabPath;
	State.Label = Label;
	return State;
}

bool FEditorUndoSystem::RecordWorldGameModeSettings(
	const FEditorWorldGameModeSettingsState& BeforeState,
	const FEditorWorldGameModeSettingsState& AfterState,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || !BeforeState.IsValid() || !AfterState.IsValid())
	{
		return false;
	}

	if (BeforeState.WorldHandle != AfterState.WorldHandle)
	{
		return false;
	}

	if (AreWorldGameModeSettingsEqual(BeforeState, AfterState))
	{
		return false;
	}

	std::unique_ptr<FSetWorldGameModeSettingsCommand> Command = std::make_unique<FSetWorldGameModeSettingsCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? FString("Edit World Settings") : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorFileSystemState FEditorUndoSystem::CaptureFileSystemState(
	const FString& Path,
	const FString& Label) const
{
	FEditorFileSystemState State;
	if (!Owner || Path.empty())
	{
		return State;
	}
	NoteMutationCapture();

	const std::filesystem::path RootPath = ResolveUndoFileSystemPath(Path);
	if (!IsPathInsideUndoRoot(RootPath))
	{
		return State;
	}

	std::error_code Ec;
	if (!std::filesystem::exists(RootPath, Ec))
	{
		return State;
	}

	State.RootPath = NormalizeUndoFileSystemPath(RootPath);
	State.Label = Label;

	auto AddEntry = [&State](const std::filesystem::path& EntryPath, bool bDirectory) -> bool
	{
		FEditorFileSystemEntryState Entry;
		Entry.Path = NormalizeUndoFileSystemPath(EntryPath);
		Entry.bDirectory = bDirectory;
		if (!bDirectory && !ReadFileBytesForUndo(EntryPath, Entry.Data))
		{
			return false;
		}
		State.Entries.push_back(std::move(Entry));
		return true;
	};

	if (std::filesystem::is_directory(RootPath, Ec))
	{
		if (!AddEntry(RootPath, true))
		{
			State.Entries.clear();
			return State;
		}

		for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(RootPath, Ec))
		{
			if (Ec)
			{
				State.Entries.clear();
				return State;
			}

			if (!AddEntry(Entry.path(), Entry.is_directory()))
			{
				State.Entries.clear();
				return State;
			}
		}
	}
	else if (!AddEntry(RootPath, false))
	{
		State.Entries.clear();
	}

	return State;
}

bool FEditorUndoSystem::RecordCreateFileSystemPath(
	const FEditorFileSystemState& AfterState,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || !AfterState.IsValid() || AfterState.Entries.empty())
	{
		return false;
	}

	std::unique_ptr<FCreateFileSystemPathCommand> Command =
		std::make_unique<FCreateFileSystemPathCommand>(
			AfterState,
			Label.empty() ? FString("Create Asset") : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordDeleteFileSystemPath(
	const FEditorFileSystemState& BeforeState,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || !BeforeState.IsValid() || BeforeState.Entries.empty())
	{
		return false;
	}

	std::unique_ptr<FDeleteFileSystemPathCommand> Command =
		std::make_unique<FDeleteFileSystemPathCommand>(
			BeforeState,
			Label.empty() ? FString("Delete Asset") : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordRenameFileSystemPath(
	const FString& OldPath,
	const FString& NewPath,
	const FString& Label)
{
	if (!Owner || bApplyingUndoRedo || OldPath.empty() || NewPath.empty())
	{
		return false;
	}

	const FString NormalizedOldPath = NormalizeUndoFileSystemPath(ResolveUndoFileSystemPath(OldPath));
	const FString NormalizedNewPath = NormalizeUndoFileSystemPath(ResolveUndoFileSystemPath(NewPath));
	if (NormalizedOldPath == NormalizedNewPath)
	{
		return false;
	}

	std::unique_ptr<FRenameFileSystemPathCommand> Command =
		std::make_unique<FRenameFileSystemPathCommand>(
			NormalizedOldPath,
			NormalizedNewPath,
			Label.empty() ? FString("Rename Asset") : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordObjectRename(UObject* Object, const FName& OldName, const FName& NewName)
{
	if (!Owner || bApplyingUndoRedo || !Object || OldName == NewName)
	{
		return false;
	}

	FEditorObjectRef ObjectRef;
	if (AActor* Actor = Cast<AActor>(Object))
	{
		ObjectRef = FEditorUndoObjectResolver::MakeActorRef(Owner, Actor);
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		ObjectRef = FEditorUndoObjectResolver::MakeComponentRef(Owner, Component);
	}

	if (!ObjectRef.IsValid())
	{
		return false;
	}

	std::unique_ptr<FRenameObjectCommand> Command = std::make_unique<FRenameObjectCommand>(
		ObjectRef,
		OldName,
		NewName);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordCreateActors(const TArray<AActor*>& Actors)
{
	if (!Owner || bApplyingUndoRedo || Actors.empty())
	{
		return false;
	}

	std::unique_ptr<FCreateActorsCommand> Command = std::make_unique<FCreateActorsCommand>(Owner, Actors);
	if (!Command || Command->IsEmpty())
	{
		return false;
	}

	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordDeleteActors(const TArray<AActor*>& Actors)
{
	if (!Owner || bApplyingUndoRedo || Actors.empty())
	{
		return false;
	}

	std::unique_ptr<FDeleteActorsCommand> Command = std::make_unique<FDeleteActorsCommand>(Owner, Actors);
	if (!Command || Command->IsEmpty())
	{
		return false;
	}

	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordCreateComponents(const TArray<UActorComponent*>& Components)
{
	if (!Owner || bApplyingUndoRedo || Components.empty())
	{
		return false;
	}

	std::unique_ptr<FCreateComponentsCommand> Command = std::make_unique<FCreateComponentsCommand>(Owner, Components);
	if (!Command || Command->IsEmpty())
	{
		return false;
	}

	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordDeleteComponents(const TArray<UActorComponent*>& Components)
{
	if (!Owner || bApplyingUndoRedo || Components.empty())
	{
		return false;
	}

	std::unique_ptr<FDeleteComponentsCommand> Command = std::make_unique<FDeleteComponentsCommand>(Owner, Components);
	if (!Command || Command->IsEmpty())
	{
		return false;
	}

	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordSceneComponentAttachment(
	USceneComponent* Component,
	USceneComponent* OldParent,
	USceneComponent* NewParent,
	const FName& OldSocketName,
	const FName& NewSocketName)
{
	if (!Owner || bApplyingUndoRedo || !Component || OldParent == NewParent)
	{
		return false;
	}

	FSceneComponentAttachmentState BeforeState;
	BeforeState.ComponentRef = FEditorUndoObjectResolver::MakeComponentRef(Owner, Component);
	BeforeState.ParentRef = OldParent ? FEditorUndoObjectResolver::MakeComponentRef(Owner, OldParent) : FEditorObjectRef();
	BeforeState.SocketName = OldSocketName;

	FSceneComponentAttachmentState AfterState;
	AfterState.ComponentRef = BeforeState.ComponentRef;
	AfterState.ParentRef = NewParent ? FEditorUndoObjectResolver::MakeComponentRef(Owner, NewParent) : FEditorObjectRef();
	AfterState.SocketName = NewSocketName;

	if (!BeforeState.ComponentRef.IsValid())
	{
		return false;
	}

	std::unique_ptr<FSetSceneComponentAttachmentCommand> Command =
		std::make_unique<FSetSceneComponentAttachmentCommand>(BeforeState, AfterState);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordMovementUpdatedComponent(
	UMovementComponent* Component,
	USceneComponent* OldUpdatedComponent,
	USceneComponent* NewUpdatedComponent)
{
	if (!Owner || bApplyingUndoRedo || !Component || OldUpdatedComponent == NewUpdatedComponent)
	{
		return false;
	}

	FMovementUpdatedComponentState BeforeState;
	BeforeState.ComponentRef = FEditorUndoObjectResolver::MakeComponentRef(Owner, Component);
	BeforeState.UpdatedComponentRef = OldUpdatedComponent
		? FEditorUndoObjectResolver::MakeComponentRef(Owner, OldUpdatedComponent)
		: FEditorObjectRef();

	FMovementUpdatedComponentState AfterState;
	AfterState.ComponentRef = BeforeState.ComponentRef;
	AfterState.UpdatedComponentRef = NewUpdatedComponent
		? FEditorUndoObjectResolver::MakeComponentRef(Owner, NewUpdatedComponent)
		: FEditorObjectRef();

	if (!BeforeState.ComponentRef.IsValid())
	{
		return false;
	}

	std::unique_ptr<FSetMovementUpdatedComponentCommand> Command =
		std::make_unique<FSetMovementUpdatedComponentCommand>(BeforeState, AfterState);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordMaterialSlot(
	UPrimitiveComponent* Component,
	int32 SlotIndex,
	UMaterialInterface* OldMaterial,
	UMaterialInterface* NewMaterial)
{
	if (!Owner || bApplyingUndoRedo || !Component || SlotIndex < 0 || OldMaterial == NewMaterial)
	{
		return false;
	}

	FMaterialSlotState BeforeState;
	BeforeState.ComponentRef = FEditorUndoObjectResolver::MakeComponentRef(Owner, Component);
	BeforeState.SlotIndex = SlotIndex;
	BeforeState.MaterialIdentifier = GetMaterialIdentifier(OldMaterial);

	FMaterialSlotState AfterState;
	AfterState.ComponentRef = BeforeState.ComponentRef;
	AfterState.SlotIndex = SlotIndex;
	AfterState.MaterialIdentifier = GetMaterialIdentifier(NewMaterial);

	if (!BeforeState.ComponentRef.IsValid())
	{
		return false;
	}

	std::unique_ptr<FSetMaterialSlotCommand> Command =
		std::make_unique<FSetMaterialSlotCommand>(BeforeState, AfterState);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

void FEditorUndoSystem::ClearHistory()
{
	ClearAllHistory();
}

void FEditorUndoSystem::ClearHistory(const FName& WorldHandle)
{
	(void)WorldHandle;
	ClearAllHistory();
}

void FEditorUndoSystem::ClearAllHistory()
{
	const bool bHadHistory = !UndoStack.empty() || !RedoStack.empty() || ActiveTransaction != nullptr;
	UndoStack.clear();
	RedoStack.clear();
	ActiveTransaction.reset();

	if (bHadHistory && Owner)
	{
		Owner->GetNotificationService().Info("Undo history cleared");
	}
}

FUndoHistoryStats FEditorUndoSystem::GetStats() const
{
	FUndoHistoryStats Stats;
	Stats.MaxEntries = MaxUndoHistory;
	Stats.UndoCount = static_cast<int32>(UndoStack.size());
	Stats.RedoCount = static_cast<int32>(RedoStack.size());

	auto Accumulate = [&Stats](const TArray<FEditorTransaction>& Stack)
	{
		for (const FEditorTransaction& Transaction : Stack)
		{
			Stats.LogicalBytes += Transaction.Label.size();
			Stats.ReservedBytes += Transaction.Label.capacity();
			Stats.LogicalBytes += Transaction.GetMemoryUsage();
		}
		Stats.EntryOverheadBytes += Stack.size() * sizeof(FEditorTransaction);
	};

	Accumulate(UndoStack);
	Accumulate(RedoStack);
	Stats.ApproxTotalBytes = Stats.ReservedBytes + Stats.EntryOverheadBytes + Stats.LogicalBytes;
	return Stats;
}

FEditorUndoContext FEditorUndoSystem::MakeContext() const
{
	FEditorUndoContext Context;
	Context.Editor = Owner;
	return Context;
}

void FEditorUndoSystem::PushUndoTransaction(FEditorTransaction Transaction)
{
	UndoStack.push_back(std::move(Transaction));
}

void FEditorUndoSystem::PushRedoTransaction(FEditorTransaction Transaction)
{
	RedoStack.push_back(std::move(Transaction));
}

void FEditorUndoSystem::TrimHistoryToLimits()
{
	while (static_cast<int32>(UndoStack.size()) > MaxUndoHistory)
	{
		UndoStack.erase(UndoStack.begin());
	}

	while (!UndoStack.empty() && GetUndoRedoMemoryUsage() > MaxUndoMemoryBytes)
	{
		UndoStack.erase(UndoStack.begin());
	}
}

size_t FEditorUndoSystem::GetUndoRedoMemoryUsage() const
{
	size_t Total = 0;
	for (const FEditorTransaction& Transaction : UndoStack)
	{
		Total += Transaction.GetMemoryUsage();
	}
	for (const FEditorTransaction& Transaction : RedoStack)
	{
		Total += Transaction.GetMemoryUsage();
	}
	return Total;
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
		return;
	}

	UndoSystem.EndTransaction();
}

void FScopedEditorTransaction::Cancel()
{
	bCancelled = true;
}
