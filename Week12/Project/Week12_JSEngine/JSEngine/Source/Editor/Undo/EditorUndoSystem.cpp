#include "Editor/Undo/EditorUndoSystem.h"

#include "Editor/EditorEngine.h"
#include "Editor/Settings/ProjectSettings.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/UI/EditorMainPanel.h"
#include "Component/ActorComponent.h"
#include "Component/SceneComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/Class.h"
#include "Object/Property.h"
#include "Serialization/ActorSerialization.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

namespace
{
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

	bool IsSameSceneComponentTransformTarget(const FEditorSceneComponentTransformState& A, const FEditorSceneComponentTransformState& B)
	{
		return A.ComponentRef.WorldHandle == B.ComponentRef.WorldHandle
			&& A.ComponentRef.ActorGuid == B.ComponentRef.ActorGuid
			&& A.ComponentRef.ComponentGuid == B.ComponentRef.ComponentGuid;
	}

	bool HasDifferentSceneComponentTransform(
		const FEditorSceneComponentTransformState& A,
		const FEditorSceneComponentTransformState& B)
	{
		return !A.RelativeLocation.Equals(B.RelativeLocation, 1.e-4f)
			|| !A.RelativeRotation.Equals(B.RelativeRotation, 1.e-4f)
			|| !A.RelativeScale.Equals(B.RelativeScale, 1.e-4f);
	}

	bool IsSameBonePoseTarget(const FEditorSkeletalBonePoseState& A, const FEditorSkeletalBonePoseState& B)
	{
		return A.ComponentRef.WorldHandle == B.ComponentRef.WorldHandle
			&& A.ComponentRef.ActorGuid == B.ComponentRef.ActorGuid
			&& A.ComponentRef.ComponentGuid == B.ComponentRef.ComponentGuid;
	}

	bool HasDifferentBonePose(
		const FEditorSkeletalBonePoseState& A,
		const FEditorSkeletalBonePoseState& B)
	{
		if (A.BoneIndices.size() != B.BoneIndices.size()
			|| A.LocalTransforms.size() != B.LocalTransforms.size())
		{
			return true;
		}

		for (size_t Index = 0; Index < A.BoneIndices.size(); ++Index)
		{
			if (A.BoneIndices[Index] != B.BoneIndices[Index]
				|| !A.LocalTransforms[Index].Equals(B.LocalTransforms[Index], 1.e-4f))
			{
				return true;
			}
		}
		return false;
	}

	bool AreSocketsEqual(const TArray<FSkeletalMeshSocket>& A, const TArray<FSkeletalMeshSocket>& B)
	{
		if (A.size() != B.size())
		{
			return false;
		}

		for (size_t Index = 0; Index < A.size(); ++Index)
		{
			const FSkeletalMeshSocket& SocketA = A[Index];
			const FSkeletalMeshSocket& SocketB = B[Index];
			if (SocketA.Name != SocketB.Name
				|| SocketA.BoneIndex != SocketB.BoneIndex
				|| !SocketA.RelativeLocation.Equals(SocketB.RelativeLocation, 1.e-4f)
				|| !SocketA.RelativeScale.Equals(SocketB.RelativeScale, 1.e-4f)
				|| std::fabs(SocketA.RelativeRotation.Pitch - SocketB.RelativeRotation.Pitch) > 1.e-4f
				|| std::fabs(SocketA.RelativeRotation.Yaw - SocketB.RelativeRotation.Yaw) > 1.e-4f
				|| std::fabs(SocketA.RelativeRotation.Roll - SocketB.RelativeRotation.Roll) > 1.e-4f)
			{
				return false;
			}
		}
		return true;
	}

	bool IsSameSerializedActorTarget(const FEditorSerializedActorState& A, const FEditorSerializedActorState& B)
	{
		return A.ActorRef.WorldHandle == B.ActorRef.WorldHandle
			&& A.ActorRef.ActorGuid == B.ActorRef.ActorGuid;
	}

	bool AreGameBuildSettingsEqual(const FGameBuildSettings& A, const FGameBuildSettings& B)
	{
		if (A.IncludedScenes.size() != B.IncludedScenes.size())
		{
			return false;
		}

		for (size_t Index = 0; Index < A.IncludedScenes.size(); ++Index)
		{
			if (FPaths::Normalize(A.IncludedScenes[Index]) != FPaths::Normalize(B.IncludedScenes[Index]))
			{
				return false;
			}
		}

		return A.GameName == B.GameName
			&& FPaths::Normalize(A.StartupScene) == FPaths::Normalize(B.StartupScene)
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

	bool AreCurveKeysEqual(const FCurveKey& A, const FCurveKey& B)
	{
		return std::fabs(A.Time - B.Time) <= 1.e-6f
			&& std::fabs(A.Value - B.Value) <= 1.e-6f
			&& A.InterpMode == B.InterpMode
			&& A.TangentMode == B.TangentMode
			&& std::fabs(A.ArriveTangent - B.ArriveTangent) <= 1.e-6f
			&& std::fabs(A.LeaveTangent - B.LeaveTangent) <= 1.e-6f;
	}

	bool AreCurvesEqual(const FFloatCurve& A, const FFloatCurve& B)
	{
		if (A.Keys.size() != B.Keys.size())
		{
			return false;
		}

		for (size_t Index = 0; Index < A.Keys.size(); ++Index)
		{
			if (!AreCurveKeysEqual(A.Keys[Index], B.Keys[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool AreMaterialParamValuesEqualForUndo(const FMaterialParamValue& A, const FMaterialParamValue& B)
	{
		if (A.Type != B.Type || A.Value.index() != B.Value.index())
		{
			return false;
		}

		switch (A.Type)
		{
		case EMaterialParamType::Bool:
			return std::get<bool>(A.Value) == std::get<bool>(B.Value);
		case EMaterialParamType::Int:
			return std::get<int32>(A.Value) == std::get<int32>(B.Value);
		case EMaterialParamType::UInt:
			return std::get<uint32>(A.Value) == std::get<uint32>(B.Value);
		case EMaterialParamType::Float:
			return std::fabs(std::get<float>(A.Value) - std::get<float>(B.Value)) <= 1.e-6f;
		case EMaterialParamType::Vector2:
		{
			const FVector2& AV = std::get<FVector2>(A.Value);
			const FVector2& BV = std::get<FVector2>(B.Value);
			return std::fabs(AV.X - BV.X) <= 1.e-6f && std::fabs(AV.Y - BV.Y) <= 1.e-6f;
		}
		case EMaterialParamType::Vector3:
			return std::get<FVector>(A.Value).Equals(std::get<FVector>(B.Value), 1.e-6f);
		case EMaterialParamType::Vector4:
		{
			const FVector4& AV = std::get<FVector4>(A.Value);
			const FVector4& BV = std::get<FVector4>(B.Value);
			return std::fabs(AV.X - BV.X) <= 1.e-6f
				&& std::fabs(AV.Y - BV.Y) <= 1.e-6f
				&& std::fabs(AV.Z - BV.Z) <= 1.e-6f
				&& std::fabs(AV.W - BV.W) <= 1.e-6f;
		}
		case EMaterialParamType::Matrix4:
			return std::memcmp(&std::get<FMatrix>(A.Value), &std::get<FMatrix>(B.Value), sizeof(FMatrix)) == 0;
		case EMaterialParamType::Texture:
			return std::get<UTexture*>(A.Value) == std::get<UTexture*>(B.Value);
		default:
			return false;
		}
	}

	bool AreMaterialParamMapsEqualForUndo(
		const TMap<FString, FMaterialParamValue>& A,
		const TMap<FString, FMaterialParamValue>& B)
	{
		if (A.size() != B.size())
		{
			return false;
		}

		for (const auto& [Name, Value] : A)
		{
			auto It = B.find(Name);
			if (It == B.end() || !AreMaterialParamValuesEqualForUndo(Value, It->second))
			{
				return false;
			}
		}
		return true;
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
		std::filesystem::path Resolved(FPaths::ToWide(Path));
		if (Resolved.is_relative())
		{
			Resolved = std::filesystem::path(FPaths::RootDir()) / Resolved;
		}
		return std::filesystem::absolute(Resolved).lexically_normal();
	}

	FString NormalizeUndoFileSystemPath(const std::filesystem::path& Path)
	{
		return FPaths::Normalize(FPaths::ToUtf8(Path.lexically_normal().generic_wstring()));
	}

	bool IsPathInsideUndoRoot(const std::filesystem::path& Path)
	{
		std::error_code Ec;
		const std::filesystem::path Root = std::filesystem::weakly_canonical(std::filesystem::path(FPaths::RootDir()), Ec);
		if (Ec || Root.empty())
		{
			return false;
		}

		const std::filesystem::path NormalizedPath = std::filesystem::absolute(Path).lexically_normal();
		const std::filesystem::path Relative = std::filesystem::relative(NormalizedPath, Root, Ec);
		if (Ec || Relative.empty())
		{
			return false;
		}

		const std::wstring RelativeText = Relative.native();
		return RelativeText != L"." && RelativeText.rfind(L"..", 0) != 0;
	}

	bool ReadFileBytesForUndo(const std::filesystem::path& Path, TArray<uint8>& OutData)
	{
		std::ifstream InFile(Path, std::ios::binary);
		if (!InFile.is_open())
		{
			return false;
		}

		InFile.seekg(0, std::ios::end);
		const std::streamoff Size = InFile.tellg();
		InFile.seekg(0, std::ios::beg);
		if (Size < 0)
		{
			return false;
		}

		OutData.resize(static_cast<size_t>(Size));
		if (!OutData.empty())
		{
			InFile.read(reinterpret_cast<char*>(OutData.data()), Size);
		}
		return true;
	}

	bool DeleteFileSystemRootForUndo(const FEditorFileSystemState& State)
	{
		if (!State.IsValid())
		{
			return false;
		}

		const std::filesystem::path RootPath = ResolveUndoFileSystemPath(State.RootPath);
		if (!IsPathInsideUndoRoot(RootPath))
		{
			return false;
		}

		std::error_code Ec;
		if (std::filesystem::is_directory(RootPath, Ec))
		{
			std::filesystem::remove_all(RootPath, Ec);
		}
		else
		{
			std::filesystem::remove(RootPath, Ec);
		}
		return !Ec;
	}

	bool RestoreFileSystemStateForUndo(const FEditorFileSystemState& State)
	{
		if (!State.IsValid())
		{
			return false;
		}

		const std::filesystem::path RootPath = ResolveUndoFileSystemPath(State.RootPath);
		if (!IsPathInsideUndoRoot(RootPath))
		{
			return false;
		}

		DeleteFileSystemRootForUndo(State);

		TArray<FEditorFileSystemEntryState> Entries = State.Entries;
		std::stable_sort(
			Entries.begin(),
			Entries.end(),
			[](const FEditorFileSystemEntryState& A, const FEditorFileSystemEntryState& B)
			{
				if (A.bDirectory != B.bDirectory)
				{
					return A.bDirectory;
				}
				return A.Path.size() < B.Path.size();
			});

		bool bRestoredAny = false;
		for (const FEditorFileSystemEntryState& Entry : Entries)
		{
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

			std::ofstream OutFile(EntryPath, std::ios::binary | std::ios::trunc);
			if (!OutFile.is_open())
			{
				continue;
			}
			if (!Entry.Data.empty())
			{
				OutFile.write(reinterpret_cast<const char*>(Entry.Data.data()), static_cast<std::streamsize>(Entry.Data.size()));
			}
			bRestoredAny = true;
		}
		return bRestoredAny || Entries.empty();
	}

	void RefreshAssetsAfterFileUndo(FEditorUndoContext& Context)
	{
		if (!Context.Editor)
		{
			return;
		}

		Context.Editor->GetAssetService().RefreshAssetDatabase();
		Context.Editor->GetMainPanel().RefreshContentBrowser();
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
			return sizeof(FSetActorTransformsCommand)
				+ Label.capacity()
				+ BeforeStates.capacity() * sizeof(FEditorActorTransformState)
				+ AfterStates.capacity() * sizeof(FEditorActorTransformState);
		}

	private:
		bool ApplyStates(FEditorUndoContext& Context, const TArray<FEditorActorTransformState>& States)
		{
			bool bAppliedAny = false;
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
				bAppliedAny = true;
			}

			if (bAppliedAny && Context.Editor)
			{
				if (UWorld* World = Context.Editor->GetWorld())
				{
					World->SyncSpatialIndex();
				}
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAppliedAny;
		}

	private:
		TArray<FEditorActorTransformState> BeforeStates;
		TArray<FEditorActorTransformState> AfterStates;
		FString Label;
	};

	enum class ESerializedActorLifecycleMode
	{
		Created,
		Deleted
	};

	class FSerializedActorLifecycleCommand final : public IEditorUndoCommand
	{
	public:
		FSerializedActorLifecycleCommand(
			TArray<FEditorSerializedActorState> InActorStates,
			FString InLabel,
			ESerializedActorLifecycleMode InMode)
			: ActorStates(std::move(InActorStates))
			, Label(std::move(InLabel))
			, Mode(InMode)
		{
			if (Label.empty())
			{
				Label = Mode == ESerializedActorLifecycleMode::Created ? "Create Actors" : "Delete Actors";
			}
		}

		FString GetLabel() const override
		{
			return Label;
		}

		bool Undo(FEditorUndoContext& Context) override
		{
			return Mode == ESerializedActorLifecycleMode::Created
				? DestroyActors(Context)
				: SpawnActors(Context);
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			return Mode == ESerializedActorLifecycleMode::Created
				? SpawnActors(Context)
				: DestroyActors(Context);
		}

		size_t GetMemoryUsage() const override
		{
			size_t Total = sizeof(FSerializedActorLifecycleCommand) + Label.capacity()
				+ ActorStates.capacity() * sizeof(FEditorSerializedActorState);
			for (const FEditorSerializedActorState& State : ActorStates)
			{
				Total += State.ActorRef.ObjectPath.capacity();
				Total += State.ActorJson.capacity();
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

			TMap<FName, TArray<AActor*>, FName::Hash> SpawnedActorsByWorld;
			bool bAppliedAny = false;
			for (const FEditorSerializedActorState& State : ActorStates)
			{
				if (!State.IsValid())
				{
					continue;
				}

				if (Context.ResolveActor(State.ActorRef))
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
				FActorLoadOptions Options;
				Options.bPreserveUUIDs = true;
				Options.bPreserveName = true;
				Options.bMakeNameUnique = false;
				Options.bCallBeginPlayIfWorldBegunPlay = true;

				AActor* SpawnedActor = FActorSerialization::SpawnActorFromJson(World, ActorJson, Options);
				if (!SpawnedActor)
				{
					continue;
				}

				SpawnedActorsByWorld[State.ActorRef.WorldHandle].push_back(SpawnedActor);
				bAppliedAny = true;
			}

			if (!bAppliedAny)
			{
				return false;
			}

			for (auto& Entry : SpawnedActorsByWorld)
			{
				FWorldContext* WorldContext = Context.Editor->GetWorldContextFromHandle(Entry.first);
				if (!WorldContext || !WorldContext->World)
				{
					continue;
				}

				WorldContext->World->SyncSpatialIndex();
				if (WorldContext->SelectionManager)
				{
					WorldContext->SelectionManager->BeginBatchUpdate();
					WorldContext->SelectionManager->ClearSelection();
					for (AActor* Actor : Entry.second)
					{
						WorldContext->SelectionManager->AddSelect(Actor);
					}
					WorldContext->SelectionManager->EndBatchUpdate();
				}
			}

			Context.Editor->GetSceneService().MarkDirty();
			return true;
		}

		bool DestroyActors(FEditorUndoContext& Context)
		{
			if (!Context.Editor)
			{
				return false;
			}

			TMap<FName, TArray<AActor*>, FName::Hash> ActorsByWorld;
			for (const FEditorSerializedActorState& State : ActorStates)
			{
				AActor* Actor = Context.ResolveActor(State.ActorRef);
				if (!Actor)
				{
					continue;
				}
				ActorsByWorld[State.ActorRef.WorldHandle].push_back(Actor);
			}

			bool bAppliedAny = false;
			for (auto& Entry : ActorsByWorld)
			{
				FWorldContext* WorldContext = Context.Editor->GetWorldContextFromHandle(Entry.first);
				UWorld* World = WorldContext ? WorldContext->World : nullptr;
				if (!World)
				{
					continue;
				}

				if (WorldContext->SelectionManager)
				{
					WorldContext->SelectionManager->BeginBatchUpdate();
				}

				for (AActor* Actor : Entry.second)
				{
					if (!Actor)
					{
						continue;
					}

					if (WorldContext->SelectionManager)
					{
						WorldContext->SelectionManager->Deselect(Actor);
					}
					World->DestroyActor(Actor);
					bAppliedAny = true;
				}

				if (WorldContext->SelectionManager)
				{
					WorldContext->SelectionManager->EndBatchUpdate();
				}
				World->SyncSpatialIndex();
			}

			if (bAppliedAny)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return bAppliedAny;
		}

	private:
		TArray<FEditorSerializedActorState> ActorStates;
		FString Label;
		ESerializedActorLifecycleMode Mode = ESerializedActorLifecycleMode::Deleted;
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
			return sizeof(FSetSerializedActorStatesCommand)
				+ Label.capacity()
				+ GetStateMemoryUsage(BeforeStates)
				+ GetStateMemoryUsage(AfterStates);
		}

	private:
		size_t GetStateMemoryUsage(const TArray<FEditorSerializedActorState>& States) const
		{
			size_t Total = States.capacity() * sizeof(FEditorSerializedActorState);
			for (const FEditorSerializedActorState& State : States)
			{
				Total += State.ActorRef.ObjectPath.capacity();
				Total += State.ActorJson.capacity();
			}
			return Total;
		}

		bool ApplyStates(FEditorUndoContext& Context, const TArray<FEditorSerializedActorState>& States)
		{
			if (!Context.Editor)
			{
				return false;
			}

			TMap<FName, TArray<AActor*>, FName::Hash> SpawnedActorsByWorld;
			bool bAppliedAny = false;
			for (const FEditorSerializedActorState& State : States)
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
					if (WorldContext->SelectionManager)
					{
						WorldContext->SelectionManager->Deselect(ExistingActor);
					}
					World->DestroyActor(ExistingActor);
				}

				json::JSON ActorJson = json::JSON::Load(State.ActorJson);
				FActorLoadOptions Options;
				Options.bPreserveUUIDs = true;
				Options.bPreserveName = true;
				Options.bMakeNameUnique = false;
				Options.bCallBeginPlayIfWorldBegunPlay = true;

				AActor* SpawnedActor = FActorSerialization::SpawnActorFromJson(World, ActorJson, Options);
				if (!SpawnedActor)
				{
					continue;
				}

				SpawnedActorsByWorld[State.ActorRef.WorldHandle].push_back(SpawnedActor);
				bAppliedAny = true;
			}

			if (!bAppliedAny)
			{
				return false;
			}

			for (auto& Entry : SpawnedActorsByWorld)
			{
				FWorldContext* WorldContext = Context.Editor->GetWorldContextFromHandle(Entry.first);
				if (!WorldContext || !WorldContext->World)
				{
					continue;
				}

				WorldContext->World->SyncSpatialIndex();
				if (WorldContext->SelectionManager)
				{
					WorldContext->SelectionManager->BeginBatchUpdate();
					WorldContext->SelectionManager->ClearSelection();
					for (AActor* Actor : Entry.second)
					{
						WorldContext->SelectionManager->AddSelect(Actor);
					}
					WorldContext->SelectionManager->EndBatchUpdate();
				}
			}

			Context.Editor->GetSceneService().MarkDirty();
			return true;
		}

	private:
		TArray<FEditorSerializedActorState> BeforeStates;
		TArray<FEditorSerializedActorState> AfterStates;
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
			return sizeof(FSetReflectedPropertyCommand)
				+ Label.capacity()
				+ BeforeState.ObjectRef.ObjectPath.capacity()
				+ BeforeState.PropertyName.capacity()
				+ BeforeState.ValueJson.capacity()
				+ AfterState.ObjectRef.ObjectPath.capacity()
				+ AfterState.PropertyName.capacity()
				+ AfterState.ValueJson.capacity();
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

		bool ApplyState(FEditorUndoContext& Context, const FEditorReflectedPropertyState& State)
		{
			UObject* Object = ResolveObject(Context, State.ObjectRef);
			if (!Object || !Object->GetClass() || State.PropertyName.empty())
			{
				return false;
			}

			const FProperty* Property = Object->GetClass()->FindProperty(State.PropertyName.c_str());
			void* ValuePtr = Property ? Property->GetValuePtr(Object) : nullptr;
			if (!Property || !ValuePtr)
			{
				return false;
			}

			json::JSON Root = json::JSON::Load(State.ValueJson);
			FJsonReader Reader(Root);
			Reader << "Value";
			Property->SerializeValue(Reader, ValuePtr);
			Object->PostEditProperty(Property->Name);

			if (Context.Editor)
			{
				if (AActor* Actor = Cast<AActor>(Object))
				{
					if (UWorld* World = Actor->GetFocusedWorld())
					{
						World->SyncSpatialIndex();
					}
				}
				else if (UActorComponent* Component = Cast<UActorComponent>(Object))
				{
					if (AActor* OwnerActor = Component->GetOwner())
					{
						if (UWorld* World = OwnerActor->GetFocusedWorld())
						{
							World->SyncSpatialIndex();
						}
					}
				}
				Context.Editor->GetSceneService().MarkDirty();
			}
			return true;
		}

	private:
		FEditorReflectedPropertyState BeforeState;
		FEditorReflectedPropertyState AfterState;
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
			return sizeof(FSetSceneComponentTransformCommand) + Label.capacity();
		}

	private:
		bool ApplyState(FEditorUndoContext& Context, const FEditorSceneComponentTransformState& State)
		{
			USceneComponent* Component = Cast<USceneComponent>(Context.ResolveComponent(State.ComponentRef));
			if (!Component)
			{
				return false;
			}

			Component->SetRelativeLocation(State.RelativeLocation);
			Component->SetRelativeRotation(State.RelativeRotation);
			Component->SetRelativeScale(State.RelativeScale);
			Component->MarkTransformDirty();

			if (Context.Editor)
			{
				if (UWorld* World = Context.Editor->GetWorld())
				{
					World->SyncSpatialIndex();
				}
				Context.Editor->GetSceneService().MarkDirty();
			}
			return true;
		}

	private:
		FEditorSceneComponentTransformState BeforeState;
		FEditorSceneComponentTransformState AfterState;
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
				+ BeforeState.BoneIndices.size() * sizeof(int32)
				+ BeforeState.LocalTransforms.size() * sizeof(FMatrix)
				+ AfterState.BoneIndices.size() * sizeof(int32)
				+ AfterState.LocalTransforms.size() * sizeof(FMatrix);
		}

	private:
		bool ApplyState(FEditorUndoContext& Context, const FEditorSkeletalBonePoseState& State)
		{
			if (!State.IsValid())
			{
				return false;
			}

			USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(Context.ResolveComponent(State.ComponentRef));
			if (!Component)
			{
				return false;
			}

			for (size_t Index = 0; Index < State.BoneIndices.size(); ++Index)
			{
				Component->SetBoneLocalTransform(State.BoneIndices[Index], State.LocalTransforms[Index]);
			}

			if (Context.Editor)
			{
				Context.Editor->GetSceneService().MarkDirty();
			}
			return true;
		}

	private:
		FEditorSkeletalBonePoseState BeforeState;
		FEditorSkeletalBonePoseState AfterState;
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
			size_t Total = State.MeshPath.capacity()
				+ State.Label.capacity()
				+ State.Sockets.size() * sizeof(FSkeletalMeshSocket);
			for (const FSkeletalMeshSocket& Socket : State.Sockets)
			{
				Total += Socket.Name.ToString().capacity();
			}
			return Total;
		}

		bool ApplyState(FEditorUndoContext&, const FEditorSkeletalMeshSocketState& State)
		{
			if (!State.IsValid())
			{
				return false;
			}

			USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(State.MeshPath);
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
				+ State.Curve.Keys.capacity() * sizeof(FCurveKey);
		}

		bool ApplyState(FEditorUndoContext&, const FEditorCurveAssetState& State)
		{
			if (!State.IsValid())
			{
				return false;
			}

			UCurveFloatAsset* Curve = FResourceManager::Get().LoadCurve(State.AssetRef.AssetPath);
			if (!Curve)
			{
				return false;
			}

			Curve->GetMutableCurve() = State.Curve;
			Curve->SetAssetPath(State.AssetRef.AssetPath);
			return FResourceManager::Get().SaveCurve(State.AssetRef.AssetPath, Curve);
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
				+ State.Params.size() * (sizeof(FString) + sizeof(FMaterialParamValue));
			for (const auto& [Name, Value] : State.Params)
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

			UMaterialInterface* Material = Context.Editor->GetAssetService().ResolveMaterialInterface(State.AssetRef.AssetPath);
			if (!Material)
			{
				return false;
			}

			for (const auto& [Name, Value] : State.Params)
			{
				Material->SetParam(Name, Value);
			}

			if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
			{
				return Context.Editor->GetAssetService().SaveMaterialInstance(State.AssetRef.AssetPath, Instance);
			}
			if (UMaterial* BaseMaterial = Cast<UMaterial>(Material))
			{
				return FResourceManager::Get().SerializeMaterial(State.AssetRef.AssetPath, BaseMaterial);
			}
			return true;
		}

	private:
		FEditorMaterialState BeforeState;
		FEditorMaterialState AfterState;
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
				Label = "Edit World GameMode Settings";
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
			const bool bResult = DeleteFileSystemRootForUndo(State);
			if (bResult)
			{
				RefreshAssetsAfterFileUndo(Context);
			}
			return bResult;
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			const bool bResult = RestoreFileSystemStateForUndo(State);
			if (bResult)
			{
				RefreshAssetsAfterFileUndo(Context);
			}
			return bResult;
		}

		size_t GetMemoryUsage() const override
		{
			return sizeof(FCreateFileSystemPathCommand) + Label.capacity() + GetStateMemoryUsage(State);
		}

	private:
		size_t GetStateMemoryUsage(const FEditorFileSystemState& InState) const
		{
			size_t Total = InState.RootPath.capacity() + InState.Label.capacity()
				+ InState.Entries.capacity() * sizeof(FEditorFileSystemEntryState);
			for (const FEditorFileSystemEntryState& Entry : InState.Entries)
			{
				Total += Entry.Path.capacity();
				Total += Entry.Data.capacity();
			}
			return Total;
		}

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
			const bool bResult = RestoreFileSystemStateForUndo(State);
			if (bResult)
			{
				RefreshAssetsAfterFileUndo(Context);
			}
			return bResult;
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			const bool bResult = DeleteFileSystemRootForUndo(State);
			if (bResult)
			{
				RefreshAssetsAfterFileUndo(Context);
			}
			return bResult;
		}

		size_t GetMemoryUsage() const override
		{
			size_t Total = sizeof(FDeleteFileSystemPathCommand) + Label.capacity()
				+ State.RootPath.capacity()
				+ State.Label.capacity()
				+ State.Entries.capacity() * sizeof(FEditorFileSystemEntryState);
			for (const FEditorFileSystemEntryState& Entry : State.Entries)
			{
				Total += Entry.Path.capacity();
				Total += Entry.Data.capacity();
			}
			return Total;
		}

	private:
		FEditorFileSystemState State;
		FString Label;
	};

	class FRenameFileSystemPathCommand final : public IEditorUndoCommand
	{
	public:
		FRenameFileSystemPathCommand(
			FEditorFileSystemState InBeforeState,
			FEditorFileSystemState InAfterState,
			FString InLabel)
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
			DeleteFileSystemRootForUndo(AfterState);
			const bool bResult = RestoreFileSystemStateForUndo(BeforeState);
			if (bResult)
			{
				RefreshAssetsAfterFileUndo(Context);
			}
			return bResult;
		}

		bool Redo(FEditorUndoContext& Context) override
		{
			DeleteFileSystemRootForUndo(BeforeState);
			const bool bResult = RestoreFileSystemStateForUndo(AfterState);
			if (bResult)
			{
				RefreshAssetsAfterFileUndo(Context);
			}
			return bResult;
		}

		size_t GetMemoryUsage() const override
		{
			auto StateMemory = [](const FEditorFileSystemState& InState)
			{
				size_t Total = InState.RootPath.capacity() + InState.Label.capacity()
					+ InState.Entries.capacity() * sizeof(FEditorFileSystemEntryState);
				for (const FEditorFileSystemEntryState& Entry : InState.Entries)
				{
					Total += Entry.Path.capacity();
					Total += Entry.Data.capacity();
				}
				return Total;
			};

			return sizeof(FRenameFileSystemPathCommand)
				+ Label.capacity()
				+ StateMemory(BeforeState)
				+ StateMemory(AfterState);
		}

	private:
		FEditorFileSystemState BeforeState;
		FEditorFileSystemState AfterState;
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
}

FEditorObjectRef FEditorUndoObjectResolver::MakeActorRef(UEditorEngine* Editor, AActor* Actor)
{
	FEditorObjectRef Ref;
	if (!Editor || !Actor)
	{
		return Ref;
	}

	FWorldContext* WorldContext = Editor->GetWorldContextFromWorld(Actor->GetFocusedWorld());
	if (!WorldContext)
	{
		return Ref;
	}

	Actor->EnsurePersistentGuid();
	Ref.WorldHandle = WorldContext->ContextHandle;
	Ref.ActorGuid = Actor->GetPersistentGuid();
	Ref.ObjectPath = Actor->GetFName().ToString();
	return Ref;
}

AActor* FEditorUndoObjectResolver::ResolveActor(UEditorEngine* Editor, const FEditorObjectRef& Ref)
{
	if (!Editor || !Ref.IsValid())
	{
		return nullptr;
	}

	FWorldContext* WorldContext = Editor->GetWorldContextFromHandle(Ref.WorldHandle);
	UWorld* World = WorldContext ? WorldContext->World : nullptr;
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

		Actor->EnsurePersistentGuid();
		if (Actor->GetPersistentGuid() == Ref.ActorGuid)
		{
			return Actor;
		}
	}
	return nullptr;
}

FEditorObjectRef FEditorUndoObjectResolver::MakeComponentRef(UEditorEngine* Editor, UActorComponent* Component)
{
	FEditorObjectRef Ref;
	if (!Editor || !Component)
	{
		return Ref;
	}

	AActor* OwnerActor = Component->GetOwner();
	if (!OwnerActor)
	{
		return Ref;
	}

	Ref = MakeActorRef(Editor, OwnerActor);
	if (!Ref.IsValid())
	{
		return FEditorObjectRef();
	}

	Component->EnsurePersistentGuid();
	Ref.ComponentGuid = Component->GetPersistentGuid();
	Ref.ObjectPath = Ref.ObjectPath + "." + Component->GetFName().ToString();
	return Ref;
}

UActorComponent* FEditorUndoObjectResolver::ResolveComponent(UEditorEngine* Editor, const FEditorObjectRef& Ref)
{
	if (!Editor || !Ref.IsValid() || !Ref.HasComponent())
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
		if (!Component)
		{
			continue;
		}

		Component->EnsurePersistentGuid();
		if (Component->GetPersistentGuid() == Ref.ComponentGuid)
		{
			return Component;
		}
	}
	return nullptr;
}

AActor* FEditorUndoContext::ResolveActor(const FEditorObjectRef& Ref) const
{
	return FEditorUndoObjectResolver::ResolveActor(Editor, Ref);
}

UActorComponent* FEditorUndoContext::ResolveComponent(const FEditorObjectRef& Ref) const
{
	return FEditorUndoObjectResolver::ResolveComponent(Editor, Ref);
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
	for (std::unique_ptr<IEditorUndoCommand>& Command : Commands)
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
	if (CachedMemoryUsage != 0)
	{
		return CachedMemoryUsage;
	}

	size_t Total = sizeof(FEditorTransaction) + Label.capacity();
	for (const std::unique_ptr<IEditorUndoCommand>& Command : Commands)
	{
		Total += sizeof(std::unique_ptr<IEditorUndoCommand>);
		if (Command)
		{
			Total += Command->GetMemoryUsage();
		}
	}
	return Total;
}

void FEditorUndoSystem::BeginTransaction(const FString& Label)
{
	if (IsRestoring())
	{
		return;
	}

	if (ActiveTransaction)
	{
		CancelTransaction();
	}

	ActiveTransaction = std::make_unique<FEditorTransaction>();
	ActiveTransaction->Label = Label.empty() ? "Editor Transaction" : Label;
}

bool FEditorUndoSystem::AddCommand(std::unique_ptr<IEditorUndoCommand> Command)
{
	if (!Command || IsRestoring())
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

	if (ActiveTransaction->Label.empty())
	{
		ActiveTransaction->Label = ActiveTransaction->Commands.front()->GetLabel();
	}

	PushTransactionWithLimit(UndoTransactions, std::move(*ActiveTransaction));
	ActiveTransaction.reset();
	RedoTransactions.clear();
	++TransactionRevision;
	return true;
}

void FEditorUndoSystem::CancelTransaction()
{
	ActiveTransaction.reset();
}

bool FEditorUndoSystem::CaptureSnapshot(const char* Reason)
{
	if (Owner == nullptr || IsRestoring() || Owner->GetEditorState() != EViewportPlayState::Editing)
	{
		return false;
	}

	const FName WorldHandle = GetActiveWorldHandle();
	if (WorldHandle == FName::None)
	{
		return false;
	}

	FString Snapshot = Owner->CaptureSceneSnapshot();
	if (Snapshot.empty())
	{
		return false;
	}

	bool bClearedRedo = false;
	const bool bCaptured = PushSnapshot(WorldHandle, std::move(Snapshot), Reason, bClearedRedo);
	if (bClearedRedo)
	{
		Owner->GetNotificationService().Info("Redo history cleared");
	}
	return bCaptured;
}

bool FEditorUndoSystem::Undo()
{
	if (Owner == nullptr || IsRestoring() || Owner->GetEditorState() != EViewportPlayState::Editing)
	{
		return false;
	}

	if (!UndoTransactions.empty())
	{
		FEditorTransaction Transaction = std::move(UndoTransactions.back());
		UndoTransactions.pop_back();

		bApplyingUndoRedo = true;
		FEditorUndoContext Context = MakeContext();
		const bool bApplied = Transaction.Undo(Context);
		bApplyingUndoRedo = false;

		if (bApplied)
		{
			Owner->GetNotificationService().Info("Undo: " + Transaction.Label);
			PushTransactionWithLimit(RedoTransactions, std::move(Transaction));
			++TransactionRevision;
			return true;
		}

		PushTransactionWithLimit(UndoTransactions, std::move(Transaction));
		return false;
	}

	const FName WorldHandle = GetActiveWorldHandle();
	if (WorldHandle == FName::None)
	{
		return false;
	}

	FString Current = Owner->CaptureSceneSnapshot();
	FUndoSnapshotEntry Previous;
	if (!PopUndoSnapshot(WorldHandle, std::move(Current), Previous))
	{
		return false;
	}

	const bool bRestored = Owner->RestoreSceneSnapshot(Previous.Snapshot, Previous.WorldHandle);
	if (bRestored)
	{
		Owner->GetNotificationService().Info("Undo: " + Previous.Label);
	}
	return bRestored;
}

bool FEditorUndoSystem::Redo()
{
	if (Owner == nullptr || IsRestoring() || Owner->GetEditorState() != EViewportPlayState::Editing)
	{
		return false;
	}

	if (!RedoTransactions.empty())
	{
		FEditorTransaction Transaction = std::move(RedoTransactions.back());
		RedoTransactions.pop_back();

		bApplyingUndoRedo = true;
		FEditorUndoContext Context = MakeContext();
		const bool bApplied = Transaction.Redo(Context);
		bApplyingUndoRedo = false;

		if (bApplied)
		{
			Owner->GetNotificationService().Info("Redo: " + Transaction.Label);
			PushTransactionWithLimit(UndoTransactions, std::move(Transaction));
			++TransactionRevision;
			return true;
		}

		PushTransactionWithLimit(RedoTransactions, std::move(Transaction));
		return false;
	}

	const FName WorldHandle = GetActiveWorldHandle();
	if (WorldHandle == FName::None)
	{
		return false;
	}

	FString Current = Owner->CaptureSceneSnapshot();
	FUndoSnapshotEntry Next;
	if (!PopRedoSnapshot(WorldHandle, std::move(Current), Next))
	{
		return false;
	}

	const bool bRestored = Owner->RestoreSceneSnapshot(Next.Snapshot, Next.WorldHandle);
	if (bRestored)
	{
		Owner->GetNotificationService().Info("Redo: " + Next.Label);
	}
	return bRestored;
}

bool FEditorUndoSystem::RestoreHistoryIndex(int32 Index)
{
	if (Owner == nullptr || IsRestoring() || Owner->GetEditorState() != EViewportPlayState::Editing)
	{
		return false;
	}

	if (!UndoTransactions.empty())
	{
		return false;
	}

	const FName WorldHandle = GetActiveWorldHandle();
	if (WorldHandle == FName::None)
	{
		return false;
	}

	FString Current = Owner->CaptureSceneSnapshot();
	FUndoSnapshotEntry Target;
	if (!RestoreHistorySnapshotIndex(WorldHandle, Index, std::move(Current), Target))
	{
		return false;
	}

	const bool bRestored = Owner->RestoreSceneSnapshot(Target.Snapshot, Target.WorldHandle);
	if (bRestored)
	{
		Owner->GetNotificationService().Info("History restored: " + Target.Label);
	}
	return bRestored;
}

TArray<FEditorActorTransformState> FEditorUndoSystem::CaptureActorTransforms(const TArray<AActor*>& Actors) const
{
	TArray<FEditorActorTransformState> States;
	if (Owner == nullptr || IsRestoring())
	{
		return States;
	}

	for (AActor* Actor : Actors)
	{
		if (!Actor)
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
		States.push_back(State);
	}
	return States;
}

bool FEditorUndoSystem::RecordActorTransforms(
	const TArray<FEditorActorTransformState>& BeforeStates,
	const TArray<FEditorActorTransformState>& AfterStates)
{
	if (Owner == nullptr || IsRestoring() || BeforeStates.empty() || AfterStates.empty())
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
				return Candidate.IsValid() && IsSameActorTransformTarget(BeforeState, Candidate);
			});
		if (AfterIt == AfterStates.end() || !HasDifferentTransform(BeforeState, *AfterIt))
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

	auto Command = std::make_unique<FSetActorTransformsCommand>(
		std::move(FilteredBefore),
		std::move(FilteredAfter),
		"Transform Actors");
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

TArray<FEditorSerializedActorState> FEditorUndoSystem::CaptureActorStates(const TArray<AActor*>& Actors) const
{
	TArray<FEditorSerializedActorState> States;
	if (Owner == nullptr || IsRestoring())
	{
		return States;
	}

	for (AActor* Actor : Actors)
	{
		if (!Actor || !FActorSerialization::ShouldSerializeActor(Actor))
		{
			continue;
		}

		FEditorSerializedActorState State;
		State.ActorRef = FEditorUndoObjectResolver::MakeActorRef(Owner, Actor);
		if (!State.ActorRef.IsValid())
		{
			continue;
		}

		State.ActorJson = FActorSerialization::BuildActorJson(Actor).dump();
		if (!State.ActorJson.empty())
		{
			States.push_back(std::move(State));
		}
	}
	return States;
}

bool FEditorUndoSystem::RecordActorDeletion(
	const TArray<FEditorSerializedActorState>& DeletedStates,
	const FString& Label)
{
	if (Owner == nullptr || IsRestoring() || DeletedStates.empty())
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

	auto Command = std::make_unique<FSerializedActorLifecycleCommand>(
		std::move(ValidStates),
		Label.empty() ? "Delete Actors" : Label,
		ESerializedActorLifecycleMode::Deleted);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordActorCreation(
	const TArray<FEditorSerializedActorState>& CreatedStates,
	const FString& Label)
{
	if (Owner == nullptr || IsRestoring() || CreatedStates.empty())
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

	auto Command = std::make_unique<FSerializedActorLifecycleCommand>(
		std::move(ValidStates),
		Label.empty() ? "Create Actors" : Label,
		ESerializedActorLifecycleMode::Created);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordActorStateChange(
	const TArray<FEditorSerializedActorState>& BeforeStates,
	const TArray<FEditorSerializedActorState>& AfterStates,
	const FString& Label)
{
	if (Owner == nullptr || IsRestoring() || BeforeStates.empty() || AfterStates.empty())
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
				return Candidate.IsValid() && IsSameSerializedActorTarget(BeforeState, Candidate);
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

	auto Command = std::make_unique<FSetSerializedActorStatesCommand>(
		std::move(FilteredBefore),
		std::move(FilteredAfter),
		Label.empty() ? "Edit Actor" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorSceneComponentTransformState FEditorUndoSystem::CaptureSceneComponentTransform(USceneComponent* Component) const
{
	FEditorSceneComponentTransformState State;
	if (Owner == nullptr || IsRestoring() || Component == nullptr)
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
	if (Owner == nullptr
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| !IsSameSceneComponentTransformTarget(BeforeState, AfterState)
		|| !HasDifferentSceneComponentTransform(BeforeState, AfterState))
	{
		return false;
	}

	auto Command = std::make_unique<FSetSceneComponentTransformCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? "Transform Component" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorSkeletalBonePoseState FEditorUndoSystem::CaptureSkeletalBonePose(
	USkeletalMeshComponent* Component,
	int32 BoneIndex) const
{
	FEditorSkeletalBonePoseState State;
	if (Owner == nullptr || IsRestoring() || Component == nullptr)
	{
		return State;
	}

	State.ComponentRef = FEditorUndoObjectResolver::MakeComponentRef(Owner, Component);
	if (!State.ComponentRef.IsValid() || !State.ComponentRef.HasComponent())
	{
		return FEditorSkeletalBonePoseState();
	}

	USkeletalMesh* Mesh = Component->GetSkeletalMesh();
	if (!Mesh)
	{
		return FEditorSkeletalBonePoseState();
	}

	const int32 BoneCount = static_cast<int32>(Mesh->GetBones().size());
	if (BoneIndex >= 0)
	{
		if (BoneIndex >= BoneCount)
		{
			return FEditorSkeletalBonePoseState();
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
	if (Owner == nullptr
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| !IsSameBonePoseTarget(BeforeState, AfterState)
		|| !HasDifferentBonePose(BeforeState, AfterState))
	{
		return false;
	}

	auto Command = std::make_unique<FSetSkeletalBonePoseCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? "Edit Bone Pose" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorSkeletalMeshSocketState FEditorUndoSystem::CaptureSkeletalMeshSocketState(
	USkeletalMesh* Mesh,
	const FString& Label) const
{
	FEditorSkeletalMeshSocketState State;
	if (Owner == nullptr || IsRestoring() || Mesh == nullptr)
	{
		return State;
	}

	FSkeletalMesh* MeshData = Mesh->GetMeshData();
	const FString MeshPath = FPaths::Normalize(Mesh->GetAssetPathFileName());
	if (!MeshData || MeshPath.empty())
	{
		return State;
	}

	State.MeshPath = MeshPath;
	State.Sockets = MeshData->Sockets;
	State.Label = Label;
	return State;
}

bool FEditorUndoSystem::RecordSkeletalMeshSocketState(
	const FEditorSkeletalMeshSocketState& BeforeState,
	const FEditorSkeletalMeshSocketState& AfterState,
	const FString& Label)
{
	if (Owner == nullptr
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| FPaths::Normalize(BeforeState.MeshPath) != FPaths::Normalize(AfterState.MeshPath)
		|| AreSocketsEqual(BeforeState.Sockets, AfterState.Sockets))
	{
		return false;
	}

	auto Command = std::make_unique<FSetSkeletalMeshSocketStateCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? "Edit Skeletal Mesh Socket" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorProjectSettingsState FEditorUndoSystem::CaptureProjectSettings(const FString& Label) const
{
	FEditorProjectSettingsState State;
	if (Owner == nullptr || IsRestoring())
	{
		return State;
	}

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
	if (Owner == nullptr
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| FPaths::Normalize(BeforeState.SettingsPath) != FPaths::Normalize(AfterState.SettingsPath)
		|| AreProjectSettingsEqual(BeforeState, AfterState))
	{
		return false;
	}

	auto Command = std::make_unique<FSetProjectSettingsStateCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? "Edit Project Settings" : Label);
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
	if (Owner == nullptr || IsRestoring() || Curve == nullptr)
	{
		return State;
	}

	const FString ResolvedPath = !AssetPath.empty() ? AssetPath : Curve->GetAssetPath();
	if (ResolvedPath.empty())
	{
		return State;
	}

	State.AssetRef.AssetPath = FPaths::Normalize(ResolvedPath);
	State.AssetRef.AssetType = "Curve";
	State.Curve = Curve->GetCurve();
	State.Label = Label;
	return State;
}

bool FEditorUndoSystem::RecordCurveAssetState(
	const FEditorCurveAssetState& BeforeState,
	const FEditorCurveAssetState& AfterState,
	const FString& Label)
{
	if (Owner == nullptr
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| FPaths::Normalize(BeforeState.AssetRef.AssetPath) != FPaths::Normalize(AfterState.AssetRef.AssetPath)
		|| AreCurvesEqual(BeforeState.Curve, AfterState.Curve))
	{
		return false;
	}

	auto Command = std::make_unique<FSetCurveAssetStateCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? "Edit Curve" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorMaterialState FEditorUndoSystem::CaptureMaterialState(
	UMaterialInterface* Material,
	const FString& AssetPath,
	const FString& Label) const
{
	FEditorMaterialState State;
	if (Owner == nullptr || IsRestoring() || Material == nullptr)
	{
		return State;
	}

	const FString ResolvedPath = !AssetPath.empty() ? AssetPath : Material->GetFilePath();
	if (ResolvedPath.empty())
	{
		return State;
	}

	State.AssetRef.AssetPath = FPaths::Normalize(ResolvedPath);
	State.AssetRef.AssetType = Cast<UMaterialInstance>(Material) ? "MaterialInstance" : "Material";
	Material->GatherAllParams(State.Params);
	State.Label = Label;
	return State;
}

bool FEditorUndoSystem::RecordMaterialState(
	const FEditorMaterialState& BeforeState,
	const FEditorMaterialState& AfterState,
	const FString& Label)
{
	if (Owner == nullptr
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| FPaths::Normalize(BeforeState.AssetRef.AssetPath) != FPaths::Normalize(AfterState.AssetRef.AssetPath)
		|| AreMaterialParamMapsEqualForUndo(BeforeState.Params, AfterState.Params))
	{
		return false;
	}

	auto Command = std::make_unique<FSetMaterialStateCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? "Edit Material" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorWorldGameModeSettingsState FEditorUndoSystem::CaptureWorldGameModeSettings(
	UWorld* World,
	const FString& Label) const
{
	FEditorWorldGameModeSettingsState State;
	if (Owner == nullptr || IsRestoring() || World == nullptr)
	{
		return State;
	}

	FWorldContext* WorldContext = Owner->GetWorldContextFromWorld(World);
	if (!WorldContext)
	{
		return State;
	}

	const FWorldGameModeSettings Settings = World->GetGameModeSettings();
	State.WorldHandle = WorldContext->ContextHandle;
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
	if (Owner == nullptr
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| BeforeState.WorldHandle != AfterState.WorldHandle
		|| AreWorldGameModeSettingsEqual(BeforeState, AfterState))
	{
		return false;
	}

	auto Command = std::make_unique<FSetWorldGameModeSettingsCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? "Edit World GameMode Settings" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorFileSystemState FEditorUndoSystem::CaptureFileSystemState(
	const FString& RootPath,
	const FString& Label) const
{
	FEditorFileSystemState State;
	if (Owner == nullptr || IsRestoring() || RootPath.empty())
	{
		return State;
	}

	const std::filesystem::path ResolvedRoot = ResolveUndoFileSystemPath(RootPath);
	if (!IsPathInsideUndoRoot(ResolvedRoot))
	{
		return State;
	}

	State.RootPath = NormalizeUndoFileSystemPath(ResolvedRoot);
	State.Label = Label;

	std::error_code Ec;
	if (!std::filesystem::exists(ResolvedRoot, Ec) || Ec)
	{
		return State;
	}

	auto CaptureEntry = [&State](const std::filesystem::path& Path)
	{
		std::error_code EntryEc;
		FEditorFileSystemEntryState Entry;
		Entry.Path = NormalizeUndoFileSystemPath(Path);
		Entry.bDirectory = std::filesystem::is_directory(Path, EntryEc);
		if (!Entry.bDirectory)
		{
			ReadFileBytesForUndo(Path, Entry.Data);
		}
		State.Entries.push_back(std::move(Entry));
	};

	CaptureEntry(ResolvedRoot);
	if (std::filesystem::is_directory(ResolvedRoot, Ec) && !Ec)
	{
		for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(ResolvedRoot, Ec))
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
	if (Owner == nullptr || IsRestoring() || !CreatedState.IsValid() || CreatedState.Entries.empty())
	{
		return false;
	}

	auto Command = std::make_unique<FCreateFileSystemPathCommand>(
		CreatedState,
		Label.empty() ? "Create Content" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordDeleteFileSystemPath(
	const FEditorFileSystemState& DeletedState,
	const FString& Label)
{
	if (Owner == nullptr || IsRestoring() || !DeletedState.IsValid() || DeletedState.Entries.empty())
	{
		return false;
	}

	auto Command = std::make_unique<FDeleteFileSystemPathCommand>(
		DeletedState,
		Label.empty() ? "Delete Content" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

bool FEditorUndoSystem::RecordRenameFileSystemPath(
	const FEditorFileSystemState& BeforeState,
	const FEditorFileSystemState& AfterState,
	const FString& Label)
{
	if (Owner == nullptr
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| BeforeState.Entries.empty()
		|| AfterState.Entries.empty()
		|| FPaths::Normalize(BeforeState.RootPath) == FPaths::Normalize(AfterState.RootPath))
	{
		return false;
	}

	auto Command = std::make_unique<FRenameFileSystemPathCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? "Rename Content" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

FEditorReflectedPropertyState FEditorUndoSystem::CaptureReflectedProperty(UObject* Object, const FProperty& Property) const
{
	FEditorReflectedPropertyState State;
	if (Owner == nullptr || IsRestoring() || !Object || !Property.Name || Property.IsTransient())
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

	void* ValuePtr = Property.GetValuePtr(Object);
	if (!ValuePtr)
	{
		return FEditorReflectedPropertyState();
	}

	json::JSON Root = json::Object();
	FJsonWriter Writer(Root);
	Writer << "Value";
	Property.SerializeValue(Writer, ValuePtr);

	State.PropertyName = Property.Name;
	State.ValueJson = Root.dump();
	return State;
}

bool FEditorUndoSystem::RecordReflectedProperty(
	const FEditorReflectedPropertyState& BeforeState,
	const FEditorReflectedPropertyState& AfterState,
	const FString& Label)
{
	if (Owner == nullptr
		|| IsRestoring()
		|| !BeforeState.IsValid()
		|| !AfterState.IsValid()
		|| BeforeState.PropertyName != AfterState.PropertyName
		|| BeforeState.ObjectRef.WorldHandle != AfterState.ObjectRef.WorldHandle
		|| BeforeState.ObjectRef.ActorGuid != AfterState.ObjectRef.ActorGuid
		|| BeforeState.ObjectRef.ComponentGuid != AfterState.ObjectRef.ComponentGuid
		|| BeforeState.ValueJson == AfterState.ValueJson)
	{
		return false;
	}

	auto Command = std::make_unique<FSetReflectedPropertyCommand>(
		BeforeState,
		AfterState,
		Label.empty() ? "Edit Property" : Label);
	BeginTransaction(Command->GetLabel());
	AddCommand(std::move(Command));
	return EndTransaction();
}

void FEditorUndoSystem::ClearHistory()
{
	ClearHistory(GetActiveWorldHandle());
}

void FEditorUndoSystem::ClearHistory(const FName& WorldHandle)
{
	if (Owner == nullptr)
	{
		ClearStorage(WorldHandle);
		return;
	}

	if (ClearStorage(WorldHandle))
	{
		Owner->GetNotificationService().Info("Undo history cleared");
	}
}

void FEditorUndoSystem::ClearAllHistory()
{
	if (Owner == nullptr)
	{
		ClearStorage();
		return;
	}

	if (ClearStorage())
	{
		Owner->GetNotificationService().Info("Undo history cleared");
	}
}

bool FEditorUndoSystem::CanUndo() const
{
	if (!UndoTransactions.empty())
	{
		return true;
	}

	const FWorldUndoHistory* History = FindHistory(GetActiveWorldHandle());
	return History && !History->UndoHistory.empty();
}

bool FEditorUndoSystem::CanRedo() const
{
	if (!RedoTransactions.empty())
	{
		return true;
	}

	const FWorldUndoHistory* History = FindHistory(GetActiveWorldHandle());
	return History && !History->RedoHistory.empty();
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

FWorldUndoHistory* FEditorUndoSystem::FindHistory(const FName& WorldHandle)
{
	auto It = HistoriesByWorld.find(WorldHandle);
	return It != HistoriesByWorld.end() ? &It->second : nullptr;
}

const FWorldUndoHistory* FEditorUndoSystem::FindHistory(const FName& WorldHandle) const
{
	auto It = HistoriesByWorld.find(WorldHandle);
	return It != HistoriesByWorld.end() ? &It->second : nullptr;
}

FWorldUndoHistory& FEditorUndoSystem::GetOrCreateHistory(const FName& WorldHandle)
{
	return HistoriesByWorld[WorldHandle];
}

bool FEditorUndoSystem::PushSnapshot(const FName& WorldHandle, FString Snapshot, const char* Reason, bool& bOutClearedRedo)
{
	bOutClearedRedo = false;
	if (WorldHandle == FName::None || Snapshot.empty())
	{
		return false;
	}

	FWorldUndoHistory& History = GetOrCreateHistory(WorldHandle);
	if (!History.UndoHistory.empty() && History.UndoHistory.back().Snapshot == Snapshot)
	{
		return false;
	}

	FUndoSnapshotEntry Entry;
	Entry.WorldHandle = WorldHandle;
	Entry.Label = (Reason && Reason[0] != '\0') ? Reason : "Scene Edit";
	Entry.Snapshot = std::move(Snapshot);
	PushWithLimit(History.UndoHistory, std::move(Entry));

	if (!History.RedoHistory.empty())
	{
		History.RedoHistory.clear();
		bOutClearedRedo = true;
	}
	return true;
}

bool FEditorUndoSystem::PopUndoSnapshot(const FName& WorldHandle, FString CurrentSnapshot, FUndoSnapshotEntry& OutEntry)
{
	FWorldUndoHistory* History = FindHistory(WorldHandle);
	if (!History || History->UndoHistory.empty())
	{
		return false;
	}

	OutEntry = std::move(History->UndoHistory.back());
	History->UndoHistory.pop_back();

	if (!CurrentSnapshot.empty())
	{
		FUndoSnapshotEntry CurrentEntry;
		CurrentEntry.WorldHandle = WorldHandle;
		CurrentEntry.Label = "Current";
		CurrentEntry.Snapshot = std::move(CurrentSnapshot);
		PushWithLimit(History->RedoHistory, std::move(CurrentEntry));
	}
	return true;
}

bool FEditorUndoSystem::PopRedoSnapshot(const FName& WorldHandle, FString CurrentSnapshot, FUndoSnapshotEntry& OutEntry)
{
	FWorldUndoHistory* History = FindHistory(WorldHandle);
	if (!History || History->RedoHistory.empty())
	{
		return false;
	}

	OutEntry = std::move(History->RedoHistory.back());
	History->RedoHistory.pop_back();

	if (!CurrentSnapshot.empty())
	{
		FUndoSnapshotEntry CurrentEntry;
		CurrentEntry.WorldHandle = WorldHandle;
		CurrentEntry.Label = "Current";
		CurrentEntry.Snapshot = std::move(CurrentSnapshot);
		PushWithLimit(History->UndoHistory, std::move(CurrentEntry));
	}
	return true;
}

bool FEditorUndoSystem::RestoreHistorySnapshotIndex(const FName& WorldHandle, int32 Index, FString CurrentSnapshot, FUndoSnapshotEntry& OutEntry)
{
	FWorldUndoHistory* History = FindHistory(WorldHandle);
	if (!History || Index < 0 || Index >= static_cast<int32>(History->UndoHistory.size()))
	{
		return false;
	}

	FUndoSnapshotEntry Target = History->UndoHistory[Index];

	History->RedoHistory.clear();
	if (!CurrentSnapshot.empty())
	{
		FUndoSnapshotEntry CurrentEntry;
		CurrentEntry.WorldHandle = WorldHandle;
		CurrentEntry.Label = "Current";
		CurrentEntry.Snapshot = std::move(CurrentSnapshot);
		PushWithLimit(History->RedoHistory, std::move(CurrentEntry));
	}

	for (int32 HistoryIndex = static_cast<int32>(History->UndoHistory.size()) - 1; HistoryIndex > Index; --HistoryIndex)
	{
		PushWithLimit(History->RedoHistory, std::move(History->UndoHistory[HistoryIndex]));
	}

	History->UndoHistory.erase(History->UndoHistory.begin() + Index, History->UndoHistory.end());
	OutEntry = std::move(Target);
	return true;
}

bool FEditorUndoSystem::ClearStorage()
{
	const bool bHadHistory = !HistoriesByWorld.empty() || !UndoTransactions.empty() || !RedoTransactions.empty() || ActiveTransaction != nullptr;
	HistoriesByWorld.clear();
	UndoTransactions.clear();
	RedoTransactions.clear();
	ActiveTransaction.reset();
	return bHadHistory;
}

bool FEditorUndoSystem::ClearStorage(const FName& WorldHandle)
{
	if (WorldHandle == FName::None)
	{
		return false;
	}

	auto It = HistoriesByWorld.find(WorldHandle);
	const bool bHadSnapshotHistory = It != HistoriesByWorld.end();
	const bool bHadTransactionHistory = !UndoTransactions.empty() || !RedoTransactions.empty() || ActiveTransaction != nullptr;
	if (!bHadSnapshotHistory && !bHadTransactionHistory)
	{
		return false;
	}

	if (bHadSnapshotHistory)
	{
		HistoriesByWorld.erase(It);
	}
	UndoTransactions.clear();
	RedoTransactions.clear();
	ActiveTransaction.reset();
	return true;
}

const TArray<FUndoSnapshotEntry>& FEditorUndoSystem::GetUndoHistory() const
{
	const FWorldUndoHistory* History = FindHistory(GetActiveWorldHandle());
	return History ? History->UndoHistory : EmptyHistory;
}

const TArray<FUndoSnapshotEntry>& FEditorUndoSystem::GetRedoHistory() const
{
	const FWorldUndoHistory* History = FindHistory(GetActiveWorldHandle());
	return History ? History->RedoHistory : EmptyHistory;
}

FUndoHistoryStats FEditorUndoSystem::GetStats() const
{
	FUndoHistoryStats Stats;
	Stats.MaxEntries = std::max(MaxUndoHistory, MaxTransactionHistory);

	const FWorldUndoHistory* History = FindHistory(GetActiveWorldHandle());
	if (History)
	{
		Stats.UndoCount += static_cast<int32>(History->UndoHistory.size());
		Stats.RedoCount += static_cast<int32>(History->RedoHistory.size());

		for (const FUndoSnapshotEntry& Entry : History->UndoHistory)
		{
			Stats.LogicalBytes += Entry.Label.size();
			Stats.LogicalBytes += Entry.Snapshot.size();
			Stats.ReservedBytes += Entry.Label.capacity();
			Stats.ReservedBytes += Entry.Snapshot.capacity();
		}

		for (const FUndoSnapshotEntry& Entry : History->RedoHistory)
		{
			Stats.LogicalBytes += Entry.Label.size();
			Stats.LogicalBytes += Entry.Snapshot.size();
			Stats.ReservedBytes += Entry.Label.capacity();
			Stats.ReservedBytes += Entry.Snapshot.capacity();
		}

		Stats.EntryOverheadBytes += (History->UndoHistory.size() + History->RedoHistory.size()) * sizeof(FUndoSnapshotEntry);
	}

	Stats.UndoCount += static_cast<int32>(UndoTransactions.size());
	Stats.RedoCount += static_cast<int32>(RedoTransactions.size());
	for (const FEditorTransaction& Transaction : UndoTransactions)
	{
		Stats.ReservedBytes += Transaction.GetMemoryUsage();
	}
	for (const FEditorTransaction& Transaction : RedoTransactions)
	{
		Stats.ReservedBytes += Transaction.GetMemoryUsage();
	}
	Stats.EntryOverheadBytes += (UndoTransactions.size() + RedoTransactions.size()) * sizeof(FEditorTransaction);
	Stats.ApproxTotalBytes = Stats.ReservedBytes + Stats.EntryOverheadBytes;
	return Stats;
}

void FEditorUndoSystem::PushWithLimit(TArray<FUndoSnapshotEntry>& History, FUndoSnapshotEntry Entry)
{
	History.push_back(std::move(Entry));
	if (static_cast<int32>(History.size()) > MaxUndoHistory)
	{
		History.erase(History.begin());
	}
}

void FEditorUndoSystem::PushTransactionWithLimit(TArray<FEditorTransaction>& History, FEditorTransaction Transaction)
{
	History.push_back(std::move(Transaction));
	if (static_cast<int32>(History.size()) > MaxTransactionHistory)
	{
		History.erase(History.begin());
	}
}

size_t FEditorUndoSystem::GetTransactionMemoryUsage() const
{
	size_t Total = 0;
	for (const FEditorTransaction& Transaction : UndoTransactions)
	{
		Total += Transaction.GetMemoryUsage();
	}
	for (const FEditorTransaction& Transaction : RedoTransactions)
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
