#include "Animation/AnimNotify.h"

#include "Component/ActorComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "GameFramework/AActor.h"
#include "Runtime/Engine.h"
#include "Runtime/Script/ScriptComponent.h"

#include <filesystem>

namespace
{
	FString GetLuaNotifyEventName(const FAnimNotifyStateEvent& Event)
	{
		if (!Event.LuaEventName.empty())
		{
			return Event.LuaEventName;
		}
		if (Event.NotifyName.IsValid())
		{
			return Event.NotifyName.ToString();
		}
		return "AnimNotify";
	}

	bool MatchesTargetScript(UScriptComponent* ScriptComponent, const FString& TargetScript)
	{
		if (!ScriptComponent)
		{
			return false;
		}
		if (TargetScript.empty())
		{
			return true;
		}

		const FString& ScriptName = ScriptComponent->GetScriptName();
		if (ScriptName == TargetScript)
		{
			return true;
		}

		const std::filesystem::path ScriptPath(FPaths::ToWide(ScriptName));
		const std::filesystem::path TargetPath(FPaths::ToWide(TargetScript));
		return FPaths::ToUtf8(ScriptPath.filename().wstring()) == TargetScript
			|| FPaths::ToUtf8(ScriptPath.stem().wstring()) == TargetScript
			|| FPaths::ToUtf8(TargetPath.filename().wstring()) == ScriptName
			|| FPaths::ToUtf8(TargetPath.stem().wstring()) == ScriptName;
	}

	template <typename Callback>
	void DispatchLuaNotifyToTargets(USkeletalMeshComponent* MeshComponent, FAnimNotifyStateEvent Event, Callback&& Invoke)
	{
		if (!MeshComponent)
		{
			return;
		}

		AActor* Owner = MeshComponent->GetOwner();
		if (!Owner)
		{
			return;
		}

		Event.LuaEventName = GetLuaNotifyEventName(Event);
		const EAnimNotifyLuaTargetPolicy TargetPolicy = static_cast<EAnimNotifyLuaTargetPolicy>(Event.LuaTargetPolicy);
		for (UActorComponent* Component : Owner->GetComponents())
		{
			UScriptComponent* ScriptComponent = Cast<UScriptComponent>(Component);
			if (!ScriptComponent)
			{
				continue;
			}

			if (TargetPolicy == EAnimNotifyLuaTargetPolicy::NamedScript &&
				!MatchesTargetScript(ScriptComponent, Event.LuaTargetScript))
			{
				continue;
			}

			Invoke(ScriptComponent, Event);
			if (TargetPolicy != EAnimNotifyLuaTargetPolicy::AllOwnerScripts)
			{
				return;
			}
		}
	}
}

void UAnimNotify_LogEvent::Notify(USkeletalMeshComponent* MeshComponent, const FAnimNotifyStateEvent& Event)
{
	UE_LOG("[AnimNotify_LogEvent] Notify Name=%s Class=%s Time=%.3f Mesh=%p",
		Event.NotifyName.ToString().c_str(),
		Event.NotifyClassName.c_str(),
		Event.TriggerTime,
		MeshComponent);
}

void UAnimNotify_LogEvent::NotifyBegin(USkeletalMeshComponent* MeshComponent, const FAnimNotifyStateEvent& Event)
{
	UE_LOG("[AnimNotify_LogEvent] Begin Name=%s Class=%s Start=%.3f Duration=%.3f Mesh=%p",
		Event.NotifyName.ToString().c_str(),
		Event.NotifyClassName.c_str(),
		Event.TriggerTime,
		Event.Duration,
		MeshComponent);
}

void UAnimNotify_LogEvent::NotifyTick(USkeletalMeshComponent* MeshComponent, const FAnimNotifyStateEvent& Event, float DeltaTime)
{
	UE_LOG("[AnimNotify_LogEvent] Tick Name=%s Class=%s Delta=%.3f Mesh=%p",
		Event.NotifyName.ToString().c_str(),
		Event.NotifyClassName.c_str(),
		DeltaTime,
		MeshComponent);
}

void UAnimNotify_LogEvent::NotifyEnd(USkeletalMeshComponent* MeshComponent, const FAnimNotifyStateEvent& Event)
{
	UE_LOG("[AnimNotify_LogEvent] End Name=%s Class=%s End=%.3f Mesh=%p",
		Event.NotifyName.ToString().c_str(),
		Event.NotifyClassName.c_str(),
		Event.GetEndTime(),
		MeshComponent);
}

void UAnimNotify_FootstepSound::Notify(USkeletalMeshComponent* MeshComponent, const FAnimNotifyStateEvent& Event)
{
	if (!GEngine)
	{
		return;
	}

	constexpr const char* FootstepSoundPath = "Asset/Sound/FootStep.mp3";
	const FAudioHandle Handle = GEngine->GetAudioSystem().PlaySFX(FootstepSoundPath, 1.0f);
}

void UAnimNotify_LuaEvent::Notify(USkeletalMeshComponent* MeshComponent, const FAnimNotifyStateEvent& Event)
{
	DispatchLuaNotifyToTargets(MeshComponent, Event,
		[MeshComponent](UScriptComponent* ScriptComponent, const FAnimNotifyStateEvent& DispatchEvent)
		{
			ScriptComponent->DispatchLuaAnimNotify(MeshComponent, DispatchEvent);
		});
}

void UAnimNotifyState_LuaEvent::Notify(USkeletalMeshComponent* MeshComponent, const FAnimNotifyStateEvent& Event)
{
	DispatchLuaNotifyToTargets(MeshComponent, Event,
		[MeshComponent](UScriptComponent* ScriptComponent, const FAnimNotifyStateEvent& DispatchEvent)
		{
			ScriptComponent->DispatchLuaAnimNotify(MeshComponent, DispatchEvent);
		});
}

void UAnimNotifyState_LuaEvent::NotifyBegin(USkeletalMeshComponent* MeshComponent, const FAnimNotifyStateEvent& Event)
{
	DispatchLuaNotifyToTargets(MeshComponent, Event,
		[MeshComponent](UScriptComponent* ScriptComponent, const FAnimNotifyStateEvent& DispatchEvent)
		{
			ScriptComponent->DispatchLuaAnimNotifyBegin(MeshComponent, DispatchEvent);
		});
}

void UAnimNotifyState_LuaEvent::NotifyTick(USkeletalMeshComponent* MeshComponent, const FAnimNotifyStateEvent& Event, float DeltaTime)
{
	DispatchLuaNotifyToTargets(MeshComponent, Event,
		[MeshComponent, DeltaTime](UScriptComponent* ScriptComponent, const FAnimNotifyStateEvent& DispatchEvent)
		{
			ScriptComponent->DispatchLuaAnimNotifyTick(MeshComponent, DispatchEvent, DeltaTime);
		});
}

void UAnimNotifyState_LuaEvent::NotifyEnd(USkeletalMeshComponent* MeshComponent, const FAnimNotifyStateEvent& Event)
{
	DispatchLuaNotifyToTargets(MeshComponent, Event,
		[MeshComponent](UScriptComponent* ScriptComponent, const FAnimNotifyStateEvent& DispatchEvent)
		{
			ScriptComponent->DispatchLuaAnimNotifyEnd(MeshComponent, DispatchEvent);
		});
}
