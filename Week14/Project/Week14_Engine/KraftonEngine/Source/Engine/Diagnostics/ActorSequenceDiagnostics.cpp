#include "Diagnostics/ActorSequenceDiagnostics.h"

#include "Animation/ActorSequence.h"
#include "Component/ActorSequenceComponent.h"
#include "Component/SceneComponent.h"
#include "FloatCurve/FloatCurveAsset.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/FloatCurve.h"
#include "Object/GarbageCollection.h"
#include "Platform/Paths.h"
#include "Serialization/PrefabManager.h"
#include "Serialization/SceneSaveManager.h"
#include "SimpleJSON/json.hpp"

#include <cmath>
#include <filesystem>

namespace
{
	constexpr float ExpectedEndX = 42.0f;
	constexpr float FloatTolerance = 0.01f;
	constexpr const char* SelfTestPrefabPath = "Saved/Diagnostics/ActorSequenceRoundTripSelfTest.prefab";

	struct FActorSequenceSelfTestContext
	{
		FActorSequenceRoundTripSelfTestResult Result;

		void Check(bool bCondition, const char* Message)
		{
			++Result.ChecksRun;
			if (bCondition)
			{
				return;
			}

			Result.bPassed = false;
			if (!Result.Message.empty())
			{
				Result.Message += "\n";
			}
			Result.Message += Message ? Message : "unknown failure";
		}
	};

	UWorld* CreateDiagnosticsWorld()
	{
		UWorld* World = UObjectManager::Get().CreateObject<UWorld>();
		if (!World)
		{
			return nullptr;
		}

		World->SetWorldType(EWorldType::Game);
		World->InitWorld();
		return World;
	}

	UActorSequenceComponent* FindSequenceComponent(AActor* Actor)
	{
		return Actor ? Actor->GetComponentByClass<UActorSequenceComponent>() : nullptr;
	}

	AActor* FindActorWithSequence(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		for (AActor* Actor : World->GetActors())
		{
			if (FindSequenceComponent(Actor))
			{
				return Actor;
			}
		}
		return nullptr;
	}

	USceneComponent* FindComponentByGuid(AActor* Actor, const FString& ComponentGuid)
	{
		if (!Actor || ComponentGuid.empty())
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				if (SceneComponent->GetPersistentGuid() == ComponentGuid)
				{
					return SceneComponent;
				}
			}
		}
		return nullptr;
	}

	bool ConfigureSingleLocationXTrack(
		UActorSequenceComponent* SequenceComponent,
		const FString& TargetComponentGuid)
	{
		if (!SequenceComponent || TargetComponentGuid.empty())
		{
			return false;
		}

		if (!SequenceComponent->AddFloatTrack(
			TargetComponentGuid,
			"Location",
			"X",
			0.0f,
			1.0f,
			FString()))
		{
			return false;
		}

		UActorSequence* Sequence = SequenceComponent->GetSequence();
		if (!Sequence || Sequence->GetBindings().empty())
		{
			return false;
		}

		FActorSequenceBinding& Binding = Sequence->GetBindings().front();
		if (Binding.Tracks.empty() || Binding.Tracks.front().Sections.empty()
			|| Binding.Tracks.front().Sections.front().Channels.empty())
		{
			return false;
		}

		FActorSequenceChannel& Channel = Binding.Tracks.front().Sections.front().Channels.front();
		if (!Channel.Playback.Curve)
		{
			Channel.Playback.Curve = Sequence->CreateInlineCurve();
		}
		if (!Channel.Playback.Curve)
		{
			return false;
		}

		FFloatCurve& Curve = Channel.Playback.Curve->GetCurve();
		Curve.Reset();
		Curve.AddKey(0.0f, 0.0f);
		Curve.AddKey(1.0f, ExpectedEndX);
		Curve.SortKeys();
		Curve.AutoSetTangents();

		SequenceComponent->CommitSequenceEditsForSerialization();
		return true;
	}

	bool ValidateSequenceShape(
		FActorSequenceSelfTestContext& Context,
		AActor* Actor,
		const FString& ExpectedComponentGuid,
		const char* Label)
	{
		UActorSequenceComponent* SequenceComponent = FindSequenceComponent(Actor);
		Context.Check(SequenceComponent != nullptr, Label);
		if (!SequenceComponent)
		{
			return false;
		}

		UActorSequence* Sequence = SequenceComponent->GetSequence();
		Context.Check(Sequence != nullptr, "Actor Sequence object should exist after round-trip.");
		if (!Sequence)
		{
			return false;
		}

		const TArray<FActorSequenceBinding>& Bindings = Sequence->GetBindings();
		Context.Check(!Bindings.empty(), "Actor Sequence binding list should survive round-trip.");
		if (Bindings.empty())
		{
			return false;
		}

		const FActorSequenceBinding& Binding = Bindings.front();
		Context.Check(
			Binding.Binding.TargetType == EActorSequenceBindingTarget::Component,
			"Actor Sequence binding should still target an actor-local component.");
		Context.Check(
			Binding.Binding.TargetComponentGuid == ExpectedComponentGuid,
			"Actor Sequence binding should preserve the target component persistent guid.");
		Context.Check(!Binding.Tracks.empty(), "Actor Sequence track list should survive round-trip.");
		if (Binding.Tracks.empty())
		{
			return false;
		}

		const FActorSequenceTrack& Track = Binding.Tracks.front();
		Context.Check(Track.PropertyName == "RelativeTransform.Location", "Actor Sequence track should preserve the reflected location property path.");
		Context.Check(!Track.Sections.empty(), "Actor Sequence section list should survive round-trip.");
		if (Track.Sections.empty())
		{
			return false;
		}

		const FActorSequenceSection& Section = Track.Sections.front();
		Context.Check(!Section.Channels.empty(), "Actor Sequence channel list should survive round-trip.");
		if (Section.Channels.empty())
		{
			return false;
		}

		const FActorSequenceChannel& Channel = Section.Channels.front();
		Context.Check(Channel.ChannelName == "X", "Actor Sequence channel name should survive round-trip.");
		Context.Check(Channel.Playback.Curve != nullptr, "Actor Sequence inline curve object should survive round-trip.");
		if (!Channel.Playback.Curve)
		{
			return false;
		}

		const FFloatCurve& Curve = Channel.Playback.Curve->GetCurve();
		Context.Check(Curve.Keys.size() == 2, "Actor Sequence inline curve key count should survive round-trip.");
		if (Curve.Keys.size() >= 2)
		{
			Context.Check(
				std::fabs(Curve.Keys.back().Value - ExpectedEndX) <= FloatTolerance,
				"Actor Sequence inline curve key value should survive round-trip.");
		}

		return true;
	}

	void ValidatePlaybackAndRestore(
		FActorSequenceSelfTestContext& Context,
		AActor* Actor,
		const FString& TargetComponentGuid,
		const char* Label)
	{
		UActorSequenceComponent* SequenceComponent = FindSequenceComponent(Actor);
		USceneComponent* TargetComponent = FindComponentByGuid(Actor, TargetComponentGuid);
		Context.Check(TargetComponent != nullptr, Label);
		if (!SequenceComponent || !TargetComponent)
		{
			return;
		}

		TargetComponent->SetRelativeLocation(FVector(0.0f, 3.0f, 0.0f));
		SequenceComponent->Play();
		if (UActorSequencePlayer* Player = SequenceComponent->GetSequencePlayer())
		{
			Player->SetCurrentTime(1.0f);
		}

		Context.Check(
			std::fabs(TargetComponent->GetRelativeLocation().X - ExpectedEndX) <= FloatTolerance,
			"Actor Sequence playback should apply the restored inline curve value.");

		SequenceComponent->Stop();
		Context.Check(
			std::fabs(TargetComponent->GetRelativeLocation().X) <= FloatTolerance,
			"Actor Sequence stop should restore the cached base location value.");
	}

	void ValidateGuidFallbackAfterComponentRename(
		FActorSequenceSelfTestContext& Context,
		AActor* Actor,
		const FString& TargetComponentGuid)
	{
		UActorSequenceComponent* SequenceComponent = FindSequenceComponent(Actor);
		USceneComponent* TargetComponent = FindComponentByGuid(Actor, TargetComponentGuid);
		Context.Check(SequenceComponent != nullptr, "Rename hostile test should find an ActorSequenceComponent.");
		Context.Check(TargetComponent != nullptr, "Rename hostile test should find the original target component by guid.");
		if (!SequenceComponent || !TargetComponent)
		{
			return;
		}

		const FString RenamedComponentName = "SequencedRoot_RenamedForGuidFallback";
		TargetComponent->SetFName(FName(RenamedComponentName));
		ValidatePlaybackAndRestore(
			Context,
			Actor,
			TargetComponentGuid,
			"Renamed component should still resolve through the persistent guid fallback.");

		SequenceComponent->CommitSequenceEditsForSerialization();
		UActorSequence* Sequence = SequenceComponent->GetSequence();
		Context.Check(Sequence != nullptr, "Rename hostile test should keep the sequence object.");
		if (!Sequence || Sequence->GetBindings().empty())
		{
			return;
		}

		const FSequenceObjectBinding& Binding = Sequence->GetBindings().front().Binding;
		Context.Check(
			Binding.TargetComponentGuid == TargetComponentGuid,
			"Rename hostile test should preserve the component persistent guid after cache refresh.");
		Context.Check(
			Binding.TargetObjectName == RenamedComponentName,
			"Rename hostile test should refresh the binding display name after component rename.");
	}

	void ValidateDuplicateKeepsLocalBinding(
		FActorSequenceSelfTestContext& Context,
		AActor* SourceActor,
		const FString& TargetComponentGuid)
	{
		Context.Check(SourceActor != nullptr, "Duplicate hostile test should have a source actor.");
		if (!SourceActor)
		{
			return;
		}

		UWorld* World = SourceActor->GetWorld();
		Context.Check(World != nullptr, "Duplicate hostile test should duplicate into a valid world.");
		if (!World)
		{
			return;
		}

		AActor* DuplicatedActor = Cast<AActor>(SourceActor->Duplicate(World));
		Context.Check(DuplicatedActor != nullptr, "Actor Sequence source actor should duplicate successfully.");
		Context.Check(DuplicatedActor != SourceActor, "Actor Sequence duplicate should be a different actor instance.");
		if (!DuplicatedActor)
		{
			return;
		}

		USceneComponent* SourceTarget = FindComponentByGuid(SourceActor, TargetComponentGuid);
		USceneComponent* DuplicateTarget = FindComponentByGuid(DuplicatedActor, TargetComponentGuid);
		Context.Check(DuplicateTarget != nullptr, "Actor Sequence duplicate should keep a local target component with the same persistent guid.");
		Context.Check(DuplicateTarget != SourceTarget, "Actor Sequence duplicate should resolve to the duplicate component, not the source component.");
		Context.Check(
			DuplicateTarget == nullptr || DuplicateTarget->GetOwner() == DuplicatedActor,
			"Actor Sequence duplicate target component should be owned by the duplicated actor.");

		if (DuplicatedActor && ValidateSequenceShape(
			Context,
			DuplicatedActor,
			TargetComponentGuid,
			"Actor duplicate should keep an ActorSequenceComponent."))
		{
			ValidatePlaybackAndRestore(
				Context,
				DuplicatedActor,
				TargetComponentGuid,
				"Actor duplicate should preserve the target scene component.");
		}
	}

	void ValidateMissingTargetIsNonFatal(
		FActorSequenceSelfTestContext& Context,
		AActor* SourceActor,
		const FString& TargetComponentGuid)
	{
		if (!SourceActor || !SourceActor->GetWorld())
		{
			Context.Check(false, "Missing-target hostile test should have a duplicable source actor.");
			return;
		}

		AActor* HostileActor = Cast<AActor>(SourceActor->Duplicate(SourceActor->GetWorld()));
		Context.Check(HostileActor != nullptr, "Missing-target hostile test should duplicate the source actor.");
		if (!HostileActor)
		{
			return;
		}

		UActorSequenceComponent* SequenceComponent = FindSequenceComponent(HostileActor);
		USceneComponent* TargetComponent = FindComponentByGuid(HostileActor, TargetComponentGuid);
		Context.Check(SequenceComponent != nullptr, "Missing-target hostile test should keep the ActorSequenceComponent.");
		Context.Check(TargetComponent != nullptr, "Missing-target hostile test should start with a removable target component.");
		if (!SequenceComponent || !TargetComponent)
		{
			return;
		}

		HostileActor->RemoveComponent(TargetComponent);
		Context.Check(
			FindComponentByGuid(HostileActor, TargetComponentGuid) == nullptr,
			"Missing-target hostile test should remove the sequenced target component.");

		SequenceComponent->Play();
		if (UActorSequencePlayer* Player = SequenceComponent->GetSequencePlayer())
		{
			Player->SetCurrentTime(1.0f);
		}
		SequenceComponent->Stop();
		Context.Check(true, "Missing-target hostile test should play/stop without crashing when the target is absent.");
	}

	void DeleteSelfTestPrefab()
	{
		const std::filesystem::path PrefabPath =
			(std::filesystem::path(FPaths::RootDir()) / FPaths::ToWide(SelfTestPrefabPath)).lexically_normal();
		const std::filesystem::path ProjectRoot = std::filesystem::path(FPaths::RootDir()).lexically_normal();
		const std::filesystem::path Relative = PrefabPath.lexically_relative(ProjectRoot);
		if (Relative.empty() || Relative.is_absolute())
		{
			return;
		}

		for (const std::filesystem::path& Part : Relative)
		{
			if (Part == L"..")
			{
				return;
			}
		}

		std::error_code Error;
		std::filesystem::remove(PrefabPath, Error);
	}
}

FActorSequenceRoundTripSelfTestResult FActorSequenceDiagnostics::RunRoundTripSelfTest()
{
	FScopedGarbageCollectionBlocker GCBlocker;
	FActorSequenceSelfTestContext Context;
	Context.Result.bPassed = true;

	UWorld* SourceWorld = CreateDiagnosticsWorld();
	Context.Check(SourceWorld != nullptr, "Actor Sequence self-test should create a source world.");
	if (!SourceWorld)
	{
		return Context.Result;
	}

	AActor* SourceActor = SourceWorld->SpawnActor<AActor>();
	Context.Check(SourceActor != nullptr, "Actor Sequence self-test should spawn a source actor.");
	if (!SourceActor)
	{
		return Context.Result;
	}

	SourceActor->SetFName(FName("ActorSequenceSelfTest_Source"));
	USceneComponent* Root = SourceActor->AddComponent<USceneComponent>();
	Context.Check(Root != nullptr, "Actor Sequence self-test should add a scene root component.");
	if (!Root)
	{
		return Context.Result;
	}

	Root->SetFName(FName("SequencedRoot"));
	SourceActor->SetRootComponent(Root);
	Root->SetRelativeLocation(FVector(0.0f, 3.0f, 0.0f));
	const FString RootGuid = Root->EnsurePersistentGuid();
	Context.Check(!RootGuid.empty(), "Actor Sequence self-test should assign a persistent root component guid.");

	UActorSequenceComponent* SequenceComponent = SourceActor->AddComponent<UActorSequenceComponent>();
	Context.Check(SequenceComponent != nullptr, "Actor Sequence self-test should add an ActorSequenceComponent.");
	if (!SequenceComponent)
	{
		return Context.Result;
	}

	SequenceComponent->SetFName(FName("IntroSequence"));
	Context.Check(
		ConfigureSingleLocationXTrack(SequenceComponent, RootGuid),
		"Actor Sequence self-test should configure a component-bound inline curve track.");

	json::JSON ActorJson = FSceneSaveManager::SerializeActorForPrefab(SourceActor);
	Context.Check(ActorJson.hasKey("ClassName"), "Actor Sequence self-test should serialize the source actor JSON.");

	UWorld* JsonWorld = CreateDiagnosticsWorld();
	Context.Check(JsonWorld != nullptr, "Actor Sequence self-test should create a JSON round-trip world.");
	if (JsonWorld)
	{
		AActor* JsonActor = FSceneSaveManager::SpawnActorFromSerializedActor(JsonWorld, ActorJson, true);
		Context.Check(JsonActor != nullptr, "Actor Sequence actor JSON should spawn after round-trip.");
		if (JsonActor && ValidateSequenceShape(Context, JsonActor, RootGuid, "JSON round-trip should keep an ActorSequenceComponent."))
		{
			ValidatePlaybackAndRestore(Context, JsonActor, RootGuid, "JSON round-trip should preserve the target scene component.");
		}
	}

	FWorldContext SourceContext;
	SourceContext.WorldType = EWorldType::Game;
	SourceContext.World = SourceWorld;
	SourceContext.ContextName = "Actor Sequence Diagnostics Source";
	SourceContext.ContextHandle = FName("ActorSequenceDiagnosticsSource");
	const FString SceneSnapshot = FSceneSaveManager::SaveToString(SourceContext);
	Context.Check(!SceneSnapshot.empty(), "Actor Sequence scene snapshot should serialize to a non-empty string.");
	if (!SceneSnapshot.empty())
	{
		FWorldContext SceneRoundTripContext;
		FPerspectiveCameraData CameraData;
		const EWorldType LoadWorldType = EWorldType::Game;
		FSceneSaveManager::LoadFromString(SceneSnapshot, SceneRoundTripContext, CameraData, &LoadWorldType);
		Context.Check(SceneRoundTripContext.World != nullptr, "Actor Sequence full scene snapshot should load a world.");
		if (SceneRoundTripContext.World)
		{
			AActor* SceneActor = FindActorWithSequence(SceneRoundTripContext.World);
			Context.Check(SceneActor != nullptr, "Actor Sequence full scene round-trip should keep an ActorSequenceComponent.");
			if (SceneActor && ValidateSequenceShape(Context, SceneActor, RootGuid, "Scene round-trip should keep an ActorSequenceComponent."))
			{
				ValidatePlaybackAndRestore(Context, SceneActor, RootGuid, "Scene round-trip should preserve the target scene component.");
			}
		}
	}

	DeleteSelfTestPrefab();
	const bool bSavedPrefab = FPrefabManager::SaveActorPrefab(SourceActor, SelfTestPrefabPath);
	Context.Check(bSavedPrefab, "Actor Sequence prefab should save inside the diagnostics folder.");
	if (bSavedPrefab)
	{
		UWorld* PrefabWorld = CreateDiagnosticsWorld();
		Context.Check(PrefabWorld != nullptr, "Actor Sequence self-test should create a prefab round-trip world.");
		if (PrefabWorld)
		{
			AActor* PrefabActor = FPrefabManager::SpawnActorFromPrefab(PrefabWorld, SelfTestPrefabPath);
			Context.Check(PrefabActor != nullptr, "Actor Sequence prefab should spawn after round-trip.");
			if (PrefabActor && ValidateSequenceShape(Context, PrefabActor, RootGuid, "Prefab round-trip should keep an ActorSequenceComponent."))
			{
				ValidatePlaybackAndRestore(Context, PrefabActor, RootGuid, "Prefab round-trip should preserve the target scene component.");
			}
		}
	}
	DeleteSelfTestPrefab();

	ValidateDuplicateKeepsLocalBinding(Context, SourceActor, RootGuid);
	ValidateGuidFallbackAfterComponentRename(Context, SourceActor, RootGuid);
	ValidateMissingTargetIsNonFatal(Context, SourceActor, RootGuid);

	if (Context.Result.bPassed && Context.Result.Message.empty())
	{
		Context.Result.Message = "Actor Sequence JSON, full scene, prefab, rename, duplicate, and missing-target self-test passed.";
	}
	return Context.Result;
}
