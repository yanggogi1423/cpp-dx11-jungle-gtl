#include "Serialization/ActorSerialization.h"

#include "Component/ActorComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/SceneComponent.h"
#include "Core/PropertyTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Level.h"
#include "GameFramework/World.h"
#include "Object/FName.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

#include <algorithm>
#include <cctype>

namespace ActorJsonKeys
{
	static constexpr const char* Name = "Name";
	static constexpr const char* ClassName = "ClassName";
	static constexpr const char* Tags = "Tags";
	static constexpr const char* UUID = "UUID";
	static constexpr const char* Components = "Components";
	static constexpr const char* Visible = "Visible";
	static constexpr const char* RootComponent = "RootComponent";
	static constexpr const char* Type = "Type";
	static constexpr const char* ParentUUID = "ParentUUID";
	static constexpr const char* EditorOnly = "EditorOnly";
	static constexpr const char* PersistentGuid = "PersistentGuid";
	static constexpr const char* UpdatedComponentUUID = "UpdatedComponentUUID";
}

namespace
{
	FString GetNormalizedType(FString Type)
	{
		if (Type == "StaticMeshComp")
		{
			return "UStaticMeshComponent";
		}
		return Type;
	}

	FString GetJsonString(json::JSON& Object, const char* Key, const FString& DefaultValue = "")
	{
		return Object.hasKey(Key) ? Object[Key].ToString() : DefaultValue;
	}

	uint32 GetJsonUInt(json::JSON& Object, const char* Key, uint32 DefaultValue = 0)
	{
		return Object.hasKey(Key) ? static_cast<uint32>(Object[Key].ToInt()) : DefaultValue;
	}

	FString TrimName(FString Name)
	{
		const auto First = std::find_if_not(Name.begin(), Name.end(), [](unsigned char Ch) { return std::isspace(Ch) != 0; });
		const auto Last = std::find_if_not(Name.rbegin(), Name.rend(), [](unsigned char Ch) { return std::isspace(Ch) != 0; }).base();
		if (First >= Last)
		{
			return "";
		}
		return FString(First, Last);
	}

	bool ParseNameNumber(const FString& Text, int32& OutNumber)
	{
		if (Text.empty())
		{
			return false;
		}

		int32 Value = 0;
		for (char Ch : Text)
		{
			if (!std::isdigit(static_cast<unsigned char>(Ch)))
			{
				return false;
			}
			Value = Value * 10 + (Ch - '0');
		}

		OutNumber = Value;
		return true;
	}

	bool SplitGeneratedNameSuffix(const FString& Name, FString& OutBaseName, int32& OutNumber)
	{
		const FString TrimmedName = TrimName(Name);
		if (TrimmedName.empty())
		{
			return false;
		}

		if (TrimmedName.back() == ')')
		{
			const size_t OpenParen = TrimmedName.rfind(" (");
			if (OpenParen != FString::npos && OpenParen + 2 < TrimmedName.size() - 1)
			{
				const FString NumberText = TrimmedName.substr(OpenParen + 2, TrimmedName.size() - OpenParen - 3);
				if (ParseNameNumber(NumberText, OutNumber))
				{
					OutBaseName = TrimName(TrimmedName.substr(0, OpenParen));
					return !OutBaseName.empty();
				}
			}
		}

		size_t NumberBegin = TrimmedName.size();
		while (NumberBegin > 0 && std::isdigit(static_cast<unsigned char>(TrimmedName[NumberBegin - 1])) != 0)
		{
			--NumberBegin;
		}

		if (NumberBegin == TrimmedName.size() || NumberBegin == 0 || TrimmedName[NumberBegin - 1] != '_')
		{
			return false;
		}

		if (ParseNameNumber(TrimmedName.substr(NumberBegin), OutNumber))
		{
			OutBaseName = TrimName(TrimmedName.substr(0, NumberBegin - 1));
			return !OutBaseName.empty();
		}
		return false;
	}

	FString StripGeneratedNameSuffixes(const FString& Name)
	{
		FString BaseName = TrimName(Name);
		for (;;)
		{
			FString NextBaseName;
			int32 IgnoredNumber = 0;
			if (!SplitGeneratedNameSuffix(BaseName, NextBaseName, IgnoredNumber))
			{
				return BaseName;
			}
			BaseName = NextBaseName;
		}
	}

	bool IsActorNameTaken(UWorld* World, AActor* TargetActor, const FString& CandidateName)
	{
		if (!World || CandidateName.empty())
		{
			return false;
		}

		for (AActor* Actor : World->GetActors())
		{
			if (Actor && Actor != TargetActor && Actor->GetFName() == FName(CandidateName))
			{
				return true;
			}
		}
		return false;
	}

	FString MakeUniqueActorName(UWorld* World, AActor* TargetActor, const FString& RequestedName)
	{
		const FString RequestedCleanName = TrimName(RequestedName);
		FString BaseName = StripGeneratedNameSuffixes(RequestedName);
		if (BaseName.empty())
		{
			BaseName = TargetActor && TargetActor->GetClass() ? TargetActor->GetClass()->ClassName : "Actor";
		}

		if (!RequestedCleanName.empty() && !IsActorNameTaken(World, TargetActor, RequestedCleanName))
		{
			return RequestedCleanName;
		}

		int32 HighestSuffix = 0;
		if (World)
		{
			for (AActor* Actor : World->GetActors())
			{
				if (!Actor || Actor == TargetActor)
				{
					continue;
				}

				FString ExistingBaseName;
				int32 ExistingSuffix = 0;
				if (SplitGeneratedNameSuffix(Actor->GetFName().ToString(), ExistingBaseName, ExistingSuffix)
					&& StripGeneratedNameSuffixes(ExistingBaseName) == BaseName)
				{
					HighestSuffix = std::max(HighestSuffix, ExistingSuffix);
				}
			}
		}

		int32 Suffix = std::max(HighestSuffix + 1, 1);
		FString Candidate;
		do
		{
			Candidate = BaseName + "_" + std::to_string(Suffix++);
		}
		while (IsActorNameTaken(World, TargetActor, Candidate));

		return Candidate;
	}

	UActorComponent* FindComponentByUUID(AActor* Actor, uint32 UUID)
	{
		if (!Actor || UUID == 0)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && Component->GetUUID() == UUID)
			{
				return Component;
			}
		}
		return nullptr;
	}
}

namespace FActorSerialization
{
	json::JSON BuildComponentJson(UActorComponent* Component)
	{
		json::JSON ComponentJson = json::Object();
		if (!Component)
		{
			return ComponentJson;
		}

		FJsonWriter ComponentWriter(ComponentJson);
		Component->Serialize(ComponentWriter);

		ComponentJson[ActorJsonKeys::UUID] = static_cast<int32>(Component->GetUUID());
		ComponentJson[ActorJsonKeys::ClassName] = Component->GetClass()->ClassName;
		if (UMovementComponent* MovementComponent = Cast<UMovementComponent>(Component))
		{
			ComponentJson[ActorJsonKeys::UpdatedComponentUUID] = MovementComponent->GetUpdatedComponent()
				? static_cast<int32>(MovementComponent->GetUpdatedComponent()->GetUUID())
				: 0;
		}
		return ComponentJson;
	}

	json::JSON BuildActorJson(AActor* Actor)
	{
		json::JSON ActorJson = json::Object();
		if (!Actor)
		{
			return ActorJson;
		}

		ActorJson[ActorJsonKeys::UUID] = static_cast<int32>(Actor->GetUUID());
		ActorJson[ActorJsonKeys::ClassName] = Actor->GetClass()->ClassName;
		ActorJson[ActorJsonKeys::Name] = Actor->GetName();
		ActorJson[ActorJsonKeys::Visible] = Actor->IsVisible();
		ActorJson[ActorJsonKeys::EditorOnly] = Actor->ShouldTickInEditor();
		Actor->EnsurePersistentGuid();
		ActorJson[ActorJsonKeys::PersistentGuid] = Actor->GetPersistentGuid().ToString();
		ActorJson[ActorJsonKeys::RootComponent] = Actor->GetRootComponent()
			? static_cast<int32>(Actor->GetRootComponent()->GetUUID())
			: 0;

		FJsonWriter ActorWriter(ActorJson);
		TArray<FString> ActorTags = Actor->GetTags();
		ActorWriter << ActorJsonKeys::Tags << ActorTags;

		ActorJson[ActorJsonKeys::Components] = json::Array();
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component || Component->IsTransient())
			{
				continue;
			}

			ActorJson[ActorJsonKeys::Components].append(BuildComponentJson(Component));
		}

		return ActorJson;
	}

	bool ApplyComponentJson(UActorComponent* Component, json::JSON& ComponentData, bool bPreserveUUID)
	{
		if (!Component)
		{
			return false;
		}

		if (bPreserveUUID)
		{
			Component->SetUUID(GetJsonUInt(ComponentData, ActorJsonKeys::UUID, Component->GetUUID()));
		}

		FJsonReader ComponentReader(ComponentData);
		Component->Serialize(ComponentReader);
		Component->PostEditChangeProperty({ "Rotation", EPropertyChangeType::ValueSet });

		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			SceneComponent->MarkTransformDirty();
		}

		if (UMovementComponent* MovementComponent = Cast<UMovementComponent>(Component))
		{
			USceneComponent* UpdatedComponent = nullptr;
			if (AActor* Owner = MovementComponent->GetOwner())
			{
				UpdatedComponent = Cast<USceneComponent>(
					FindComponentByUUID(Owner, GetJsonUInt(ComponentData, ActorJsonKeys::UpdatedComponentUUID)));
			}
			MovementComponent->SetUpdatedComponent(UpdatedComponent);
		}
		return true;
	}

	UActorComponent* AddComponentFromJson(AActor* Owner, json::JSON& ComponentData, bool bPreserveUUID)
	{
		if (!Owner)
		{
			return nullptr;
		}

		const FString Type = GetNormalizedType(GetJsonString(ComponentData, ActorJsonKeys::ClassName, GetJsonString(ComponentData, ActorJsonKeys::Type)));
		if (Type.empty())
		{
			return nullptr;
		}

		UObject* NewObject = FObjectFactory::Get().Create(Type);
		UActorComponent* Component = Cast<UActorComponent>(NewObject);
		if (!Component)
		{
			if (NewObject)
			{
				UObjectManager::Get().DestroyObject(NewObject);
			}
			return nullptr;
		}

		Owner->RegisterComponent(Component);
		ApplyComponentJson(Component, ComponentData, bPreserveUUID);

		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			USceneComponent* ParentComponent = Cast<USceneComponent>(
				FindComponentByUUID(Owner, GetJsonUInt(ComponentData, ActorJsonKeys::ParentUUID)));
			if (ParentComponent)
			{
				SceneComponent->AttachToComponent(ParentComponent, SceneComponent->GetAttachSocketName());
			}
			else if (!Owner->GetRootComponent())
			{
				Owner->SetRootComponent(SceneComponent);
			}
		}

		if (UWorld* World = Owner->GetFocusedWorld())
		{
			World->SyncSpatialIndex();
		}
		return Component;
	}

	bool ApplyActorJson(AActor* Actor, json::JSON& ActorData, bool bPreserveUUID)
	{
		if (!Actor)
		{
			return false;
		}

		if (bPreserveUUID)
		{
			Actor->SetUUID(GetJsonUInt(ActorData, ActorJsonKeys::UUID, Actor->GetUUID()));
		}
		if (ActorData.hasKey(ActorJsonKeys::PersistentGuid))
		{
			Actor->SetPersistentGuid(FGuid::FromString(GetJsonString(ActorData, ActorJsonKeys::PersistentGuid)));
		}
		if (ActorData.hasKey(ActorJsonKeys::Name))
		{
			Actor->SetFName(FName(GetJsonString(ActorData, ActorJsonKeys::Name)));
		}
		if (ActorData.hasKey(ActorJsonKeys::Visible))
		{
			Actor->SetVisible(ActorData[ActorJsonKeys::Visible].ToBool());
		}
		if (ActorData.hasKey(ActorJsonKeys::EditorOnly))
		{
			Actor->SetTickInEditor(ActorData[ActorJsonKeys::EditorOnly].ToBool());
		}
		if (ActorData.hasKey(ActorJsonKeys::Tags))
		{
			TArray<FString> ActorTags;
			FJsonReader ActorReader(ActorData);
			ActorReader << ActorJsonKeys::Tags << ActorTags;
			Actor->ClearTags();
			for (const FString& Tag : ActorTags)
			{
				Actor->AddTag(Tag);
			}
		}

		if (ActorData.hasKey(ActorJsonKeys::Components))
		{
			json::JSON& ComponentsNode = ActorData[ActorJsonKeys::Components];
			for (int32 CompIndex = 0; CompIndex < static_cast<int32>(ComponentsNode.length()); ++CompIndex)
			{
				json::JSON& CompData = ComponentsNode.at(CompIndex);
				UActorComponent* Component = FindComponentByUUID(Actor, GetJsonUInt(CompData, ActorJsonKeys::UUID));
				if (Component)
				{
					ApplyComponentJson(Component, CompData, bPreserveUUID);
				}
			}
		}

		if (UWorld* World = Actor->GetFocusedWorld())
		{
			World->SyncSpatialIndex();
		}
		return true;
	}

	AActor* SpawnActorFromJson(UWorld* World, json::JSON& ActorData, const FActorLoadOptions& Options)
	{
		if (!World)
		{
			return nullptr;
		}

		const FString ActorClass = GetJsonString(ActorData, ActorJsonKeys::ClassName, "AActor");
		UObject* CreatedObject = FObjectFactory::Get().Create(ActorClass);
		AActor* NewActor = Cast<AActor>(CreatedObject);
		if (!NewActor)
		{
			if (CreatedObject)
			{
				UObjectManager::Get().DestroyObject(CreatedObject);
			}
			return nullptr;
		}

		NewActor->InitDefaultComponents();
		if (Options.bPreserveUUIDs)
		{
			NewActor->SetUUID(GetJsonUInt(ActorData, ActorJsonKeys::UUID, NewActor->GetUUID()));
		}
		if (ActorData.hasKey(ActorJsonKeys::PersistentGuid))
		{
			NewActor->SetPersistentGuid(FGuid::FromString(GetJsonString(ActorData, ActorJsonKeys::PersistentGuid)));
		}
		else
		{
			NewActor->EnsurePersistentGuid();
		}

		const FString ActorName = GetJsonString(ActorData, ActorJsonKeys::Name);
		if (Options.bPreserveName && !ActorName.empty())
		{
			const FString FinalName = Options.bMakeNameUnique
				? MakeUniqueActorName(World, NewActor, ActorName)
				: ActorName;
			NewActor->SetFName(FName(FinalName));
		}
		if (ActorData.hasKey(ActorJsonKeys::Visible))
		{
			NewActor->SetVisible(ActorData[ActorJsonKeys::Visible].ToBool());
		}
		if (ActorData.hasKey(ActorJsonKeys::EditorOnly))
		{
			NewActor->SetTickInEditor(ActorData[ActorJsonKeys::EditorOnly].ToBool());
		}
		if (ActorData.hasKey(ActorJsonKeys::Tags))
		{
			TArray<FString> ActorTags;
			FJsonReader ActorReader(ActorData);
			ActorReader << ActorJsonKeys::Tags << ActorTags;
			NewActor->ClearTags();
			for (const FString& Tag : ActorTags)
			{
				NewActor->AddTag(Tag);
			}
		}

		NewActor->SetWorld(World);
		if (ULevel* Level = World->GetPersistentLevel())
		{
			Level->AddActor(NewActor);
		}

		if (!ActorData.hasKey(ActorJsonKeys::Components))
		{
			World->SyncSpatialIndex();
			if (Options.bCallBeginPlayIfWorldBegunPlay && World->HasBegunPlay())
			{
				NewActor->BeginPlay();
			}
			return NewActor;
		}

		json::JSON& ComponentsNode = ActorData[ActorJsonKeys::Components];
		const uint32 RootUUID = GetJsonUInt(ActorData, ActorJsonKeys::RootComponent);
		TMap<uint32, UActorComponent*> UUIDToComp;
		TArray<UActorComponent*> UnusedDefaultComponents = NewActor->GetComponents();

		auto TakeDefaultComponent = [&](const FString& TypeName) -> UActorComponent*
		{
			for (auto It = UnusedDefaultComponents.begin(); It != UnusedDefaultComponents.end(); ++It)
			{
				UActorComponent* Candidate = *It;
				if (Candidate && GetNormalizedType(Candidate->GetClass()->ClassName) == TypeName)
				{
					UnusedDefaultComponents.erase(It);
					return Candidate;
				}
			}
			return nullptr;
		};

		auto MarkDefaultComponentUsed = [&](UActorComponent* Component)
		{
			auto It = std::find(UnusedDefaultComponents.begin(), UnusedDefaultComponents.end(), Component);
			if (It != UnusedDefaultComponents.end())
			{
				UnusedDefaultComponents.erase(It);
			}
		};

		for (int32 CompIndex = 0; CompIndex < static_cast<int32>(ComponentsNode.length()); ++CompIndex)
		{
			json::JSON& CompData = ComponentsNode.at(CompIndex);
			const uint32 SavedCompUUID = GetJsonUInt(CompData, ActorJsonKeys::UUID);
			const FString Type = GetNormalizedType(GetJsonString(CompData, ActorJsonKeys::ClassName, GetJsonString(CompData, ActorJsonKeys::Type)));
			if (SavedCompUUID == 0 || Type.empty())
			{
				continue;
			}

			UActorComponent* Component = nullptr;
			if (SavedCompUUID == RootUUID && NewActor->GetRootComponent()
				&& GetNormalizedType(NewActor->GetRootComponent()->GetClass()->ClassName) == Type)
			{
				Component = NewActor->GetRootComponent();
				MarkDefaultComponentUsed(Component);
			}
			if (!Component)
			{
				Component = TakeDefaultComponent(Type);
			}
			if (!Component)
			{
				UObject* NewObj = FObjectFactory::Get().Create(Type);
				Component = Cast<UActorComponent>(NewObj);
				if (!Component)
				{
					UObjectManager::Get().DestroyObject(NewObj);
					continue;
				}
				NewActor->RegisterComponent(Component);
			}

			if (Options.bPreserveUUIDs)
			{
				Component->SetUUID(SavedCompUUID);
			}
			UUIDToComp[SavedCompUUID] = Component;
			if (SavedCompUUID == RootUUID)
			{
				if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
				{
					NewActor->SetRootComponent(SceneComponent);
				}
			}
		}

		for (int32 CompIndex = 0; CompIndex < static_cast<int32>(ComponentsNode.length()); ++CompIndex)
		{
			json::JSON& CompData = ComponentsNode.at(CompIndex);
			const uint32 SavedCompUUID = GetJsonUInt(CompData, ActorJsonKeys::UUID);
			const uint32 SavedParentUUID = GetJsonUInt(CompData, ActorJsonKeys::ParentUUID);
			if (SavedParentUUID == 0)
			{
				continue;
			}

			USceneComponent* SceneComponent = Cast<USceneComponent>(UUIDToComp[SavedCompUUID]);
			USceneComponent* ParentComponent = Cast<USceneComponent>(UUIDToComp[SavedParentUUID]);
			if (SceneComponent && ParentComponent)
			{
				SceneComponent->AttachToComponent(ParentComponent);
			}
		}

		for (int32 CompIndex = 0; CompIndex < static_cast<int32>(ComponentsNode.length()); ++CompIndex)
		{
			json::JSON& CompData = ComponentsNode.at(CompIndex);
			const uint32 SavedCompUUID = GetJsonUInt(CompData, ActorJsonKeys::UUID);
			UActorComponent* Component = UUIDToComp[SavedCompUUID];
			if (!Component)
			{
				continue;
			}

			FJsonReader ComponentReader(CompData);
			Component->Serialize(ComponentReader);
			Component->PostEditChangeProperty({ "Rotation", EPropertyChangeType::ValueSet });
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				SceneComponent->MarkTransformDirty();
			}
		}

		World->SyncSpatialIndex();
		if (Options.bCallBeginPlayIfWorldBegunPlay && World->HasBegunPlay())
		{
			NewActor->BeginPlay();
		}
		return NewActor;
	}
}
