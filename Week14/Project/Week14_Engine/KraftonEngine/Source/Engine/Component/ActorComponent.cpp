#include "ActorComponent.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"
#include "GameFramework/AActor.h"
#include "Object/GarbageCollection.h"

#include <algorithm>
#include <cctype>

HIDE_FROM_COMPONENT_LIST(UActorComponent)

namespace
{
	FString TrimTagString(const FString& Value)
	{
		size_t Begin = 0;
		size_t End = Value.size();
		while (Begin < End && std::isspace(static_cast<unsigned char>(Value[Begin]))) ++Begin;
		while (End > Begin && std::isspace(static_cast<unsigned char>(Value[End - 1]))) --End;
		return Value.substr(Begin, End - Begin);
	}

	FString JoinTagsCommaSep(const TArray<FName>& Tags)
	{
		FString Result;
		for (size_t Index = 0; Index < Tags.size(); ++Index)
		{
			if (Index > 0) Result += ",";
			Result += Tags[Index].ToString();
		}
		return Result;
	}

	TArray<FName> SplitTagsCommaSep(const FString& In)
	{
		TArray<FName> Out;
		size_t Start = 0;
		while (Start <= In.size())
		{
			size_t End = In.find(',', Start);
			if (End == FString::npos) End = In.size();

			const FString Token = TrimTagString(In.substr(Start, End - Start));
			if (!Token.empty())
			{
				const FName Tag(Token);
				if (std::find(Out.begin(), Out.end(), Tag) == Out.end())
				{
					Out.push_back(Tag);
				}
			}

			if (End == In.size()) break;
			Start = End + 1;
		}
		return Out;
	}
}

void UActorComponent::BeginPlay()
{
	if (bAutoActivate)
	{
		Activate();
	}
}

void UActorComponent::Activate()
{
	bIsActive = true;
	PrimaryComponentTick.SetTickEnabled(bTickEnable);
}

void UActorComponent::Deactivate()
{
	bIsActive = false;
	PrimaryComponentTick.SetTickEnabled(false);
}


UWorld* UActorComponent::GetWorld() const
{
	AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetWorld() : nullptr;
}

UWorld* UActorComponent::GetWorldEvenIfPendingKill() const
{
	AActor* OwnerActor = GetOwnerEvenIfPendingKill();
	return OwnerActor ? OwnerActor->GetWorldEvenIfPendingKill() : nullptr;
}

void UActorComponent::OnPreSave(FArchive& /*Ar*/)
{
	EnsurePersistentGuid();
	PendingTagsString = JoinTagsCommaSep(Tags);
}

void UActorComponent::OnPostLoad(FArchive& /*Ar*/)
{
	EnsurePersistentGuid();
	SetTags(SplitTagsCommaSep(PendingTagsString));
}

const FString& UActorComponent::EnsurePersistentGuid()
{
	if (PersistentGuid.empty())
	{
		PersistentGuid = "Component_" + std::to_string(GetUUID());
	}
	return PersistentGuid;
}

bool UActorComponent::HasTag(const FName& Tag) const
{
	if (!Tag.IsValid())
	{
		return false;
	}

	return std::find(Tags.begin(), Tags.end(), Tag) != Tags.end();
}

void UActorComponent::AddTag(const FName& Tag)
{
	if (!Tag.IsValid() || HasTag(Tag))
	{
		return;
	}

	Tags.push_back(Tag);
	PendingTagsString = JoinTagsCommaSep(Tags);
}

void UActorComponent::RemoveTag(const FName& Tag)
{
	auto It = std::find(Tags.begin(), Tags.end(), Tag);
	if (It == Tags.end())
	{
		return;
	}

	Tags.erase(It);
	PendingTagsString = JoinTagsCommaSep(Tags);
}

void UActorComponent::SetTags(TArray<FName> InTags)
{
	Tags.clear();
	for (const FName& Tag : InTags)
	{
		if (Tag.IsValid() && std::find(Tags.begin(), Tags.end(), Tag) == Tags.end())
		{
			Tags.push_back(Tag);
		}
	}
	PendingTagsString = JoinTagsCommaSep(Tags);
}

void UActorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
}

void UActorComponent::SetActive(bool bNewActive)
{
	if (bNewActive == bIsActive)
	{
		return;
	}

	bIsActive = bNewActive;

	if (bIsActive)
	{
		Activate();
	}
	else
	{
		Deactivate();
	}
}

void UActorComponent::SetEditorOnly(bool bInEditorOnly)
{
	if (bEditorOnly == bInEditorOnly) return;
	bEditorOnly = bInEditorOnly;

	// 렌더 상태 재생성 — EditorOnly 변경 시 프록시 생성/파괴 판단이 달라짐
	DestroyRenderState();
	CreateRenderState();
}

void UActorComponent::SetOwner(AActor* Actor)
{
	Owner.Reset(Actor);
	PrimaryComponentTick.Target = this;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = bTickEnable;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UActorComponent::RouteComponentDestroyed()
{
	if (bComponentDestroyRouted)
	{
		return;
	}

	bComponentDestroyRouted = true;
	PrimaryComponentTick.UnRegisterTickFunction();
	DestroyRenderState();

	if (AActor* OwnerActor = GetOwnerEvenIfPendingKill())
	{
		OwnerActor->OnComponentBeingDestroyed(this);
	}

	Owner.Reset();
	SetOuter(nullptr);
}

void UActorComponent::BeginDestroy()
{
	if (HasAnyFlags(RF_BeginDestroy))
	{
		return;
	}

	EndPlay();
	RouteComponentDestroyed();
	UObject::BeginDestroy();
}

void UActorComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Owner is intentionally weak. Actor owns components, not the other way around.
	UObject::AddReferencedObjects(Collector);
}


void UActorComponent::PostEditProperty(const char* PropertyName)
{
	if (strcmp(PropertyName, "PendingTagsString") == 0 || strcmp(PropertyName, "Tags") == 0) {
		SetTags(SplitTagsCommaSep(PendingTagsString));
	}

	if (strcmp(PropertyName, "bTickEnable") == 0) {
		PrimaryComponentTick.SetTickEnabled(bTickEnable);
	}

	if (strcmp(PropertyName, "bEditorOnly") == 0) {
		// Property Editor가 bEditorOnly를 이미 직접 수정한 상태이므로
		// SetEditorOnly의 early-return 가드를 우회하여 렌더 상태를 직접 재생성한다.
		DestroyRenderState();
		CreateRenderState();
	}

	if (strcmp(PropertyName, "bIsActive") == 0) {
		if (bIsActive)
		{
			Activate();
		}
		else
		{
			Deactivate();
		}
	}
}
