#include "LuaScriptComponent.h"

#include "Component/Gameplay/SniperDamageReceiverComponent.h"
#include "Component/Gameplay/SniperWeaponComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Level.h"
#include "Lua/LuaScriptManager.h"
#include "Lua/LuaDebugManager.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/GarbageCollection.h"
#include "Core/Types/PropertyTypes.h"
#include "Serialization/Archive.h"

#include <cstring>

ULuaScriptComponent::ULuaScriptComponent()
{
}

void ULuaScriptComponent::SetScriptFile(const FString& InScriptFile)
{
	ScriptFile = InScriptFile;
	UpdatePauseTickEligibility();
}

namespace
{
	bool IsGeneralManagerScriptFile(const FString& ScriptFile)
	{
		if (ScriptFile.empty())
		{
			return false;
		}

		size_t FileNameStart = ScriptFile.find_last_of("/\\");
		const FString FileName = FileNameStart == FString::npos
			? ScriptFile
			: ScriptFile.substr(FileNameStart + 1);
		return FileName == "GeneralManager.lua";
	}

	const char* ToGameStateName(EGeneralManagerStartState State)
	{
		switch (State)
		{
		case EGeneralManagerStartState::Intro: return "Intro";
		case EGeneralManagerStartState::Main: return "Main";
		case EGeneralManagerStartState::Loading: return "Loading";
		case EGeneralManagerStartState::PreInGame: return "Pre-InGame";
		case EGeneralManagerStartState::InGame: return "InGame";
		case EGeneralManagerStartState::Defeat1: return "Defeat1";
		case EGeneralManagerStartState::Defeat2: return "Defeat2";
		case EGeneralManagerStartState::Victory: return "Victory";
		default: return "Intro";
		}
	}
}

ULuaScriptComponent::~ULuaScriptComponent()
{
	if (FLuaScriptManager::IsInitialized())
	{
		ReleaseLuaRuntimeForShutdown();
	}
}


void ULuaScriptComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	UActorComponent::AddReferencedObjects(Collector);
}

void ULuaScriptComponent::ClearLuaRuntime()
{
	LuaBeginPlay = sol::nil;
	LuaTick = sol::nil;
	LuaEndPlay = sol::nil;
	LuaOnOverlap = sol::nil;
	LuaOnEndOverlap = sol::nil;
	LuaOnHit = sol::nil;
	LuaOnEndHit = sol::nil;
	LuaOnSniperHit = sol::nil;
	LuaOnSniperDamaged = sol::nil;
	LuaOnSniperKilled = sol::nil;
	Env = sol::environment();
	bHasCalledLuaEndPlay = false;
	bPendingLuaEndPlay = false;
	bPendingLuaCleanup = false;
}

void ULuaScriptComponent::ReleaseLuaRuntimeForShutdown()
{
	FLuaScriptManager::UnregisterComponent(this);
	ClearCollisionBindings();
	ClearSniperBindings();
	if (LuaCallDepth > 0)
	{
		bPendingLuaCleanup = true;
		return;
	}
	ClearLuaRuntime();
}

void ULuaScriptComponent::BeginDestroy()
{
	// Base BeginDestroy routes virtual EndPlay() first. Keep Lua state alive until
	// EndPlay has a chance to invoke Lua EndPlay exactly once.
	UActorComponent::BeginDestroy();
}

bool ULuaScriptComponent::InitializeLua()
{
	ClearLuaRuntime();
	bEndPlayRouted = false;
	bHasCalledLuaEndPlay = false;

	sol::state& Lua = FLuaScriptManager::GetState();

	Env = sol::environment(Lua, sol::create, Lua.globals());
	Env["obj"] = GetOwner();
	Env["this"] = this;

	const FString ResolvedPath = FLuaScriptManager::ResolveScriptPath(ScriptFile);
	// 한글 경로 호환 — safe_script_file 은 fopen(UTF-8) 경로라 ANSI 코드페이지에서 깨짐.
	// wide ifstream 으로 읽어 safe_script(string, env, ...) 로 우회.
	FString Content;
	if (!FLuaScriptManager::ReadScriptFileContent(ScriptFile, Content))
	{
		UE_LOG("Failed to read Lua script %s", ResolvedPath.c_str());
		ClearLuaRuntime();
		return false;
	}
	sol::protected_function_result Result = Lua.safe_script(Content, Env, sol::script_pass_on_error, ResolvedPath);

	if (!Result.valid())
	{
		sol::error Err = Result;
		UE_LOG("Failed to load Lua script %s: %s", ScriptFile.c_str(), Err.what());
		FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		ClearLuaRuntime();
		return false;
	}

	LuaBeginPlay = Env["BeginPlay"];
	LuaTick = Env["Tick"];
	LuaEndPlay = Env["EndPlay"];
	LuaOnOverlap = Env["OnOverlap"];
	LuaOnEndOverlap = Env["OnEndOverlap"];
	LuaOnHit = Env["OnHit"];
	LuaOnEndHit = Env["OnEndHit"];
	LuaOnSniperHit = Env["OnSniperHit"];
	LuaOnSniperDamaged = Env["OnSniperDamaged"];
	LuaOnSniperKilled = Env["OnSniperKilled"];
	return true;
}

bool ULuaScriptComponent::ReloadScript()
{
	ClearCollisionBindings();
	ClearSniperBindings();
	InvokeLuaEndPlay();
	if (!InitializeLua())
	{
		return false;
	}

	bool bReloaded = true;
	if (LuaBeginPlay)
	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaBeginPlay();
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua BeginPlay error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
			bReloaded = false;
		}
	}

	BindOwnerCollisionEvents();
	BindOwnerSniperEvents();
	return bReloaded;
}

void ULuaScriptComponent::BeginPlay()
{
	EnsureDefaultScriptFile();
	UpdatePauseTickEligibility();
	UActorComponent::BeginPlay();

	const bool bLuaLoaded = InitializeLua();
	FLuaScriptManager::RegisterComponent(this);

	if (bLuaLoaded && LuaBeginPlay)
	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaBeginPlay();
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua BeginPlay error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}

	BindOwnerCollisionEvents();
	BindOwnerSniperEvents();
}

void ULuaScriptComponent::EndPlay()
{
	if (bEndPlayRouted)
	{
		return;
	}
	bEndPlayRouted = true;

	UActorComponent::EndPlay();
	FLuaScriptManager::UnregisterComponent(this);
	ClearCollisionBindings();
	ClearSniperBindings();
	if (LuaCallDepth > 0)
	{
		bPendingLuaEndPlay = true;
		bPendingLuaCleanup = true;
		return;
	}
	InvokeLuaEndPlay();
	ClearLuaRuntime();
}

void ULuaScriptComponent::InvokeLuaEndPlay()
{
	if (bHasCalledLuaEndPlay || !LuaEndPlay)
	{
		return;
	}

	bHasCalledLuaEndPlay = true;
	FLuaCallScope Scope(this);
	sol::protected_function_result Result = LuaEndPlay();
	if (!Result.valid())
	{
		sol::error Err = Result;
		UE_LOG("Lua EndPlay error in %s: %s", ScriptFile.c_str(), Err.what());
		FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
	}
}

void ULuaScriptComponent::HandleDeferredLuaCleanup()
{
	if (LuaCallDepth != 0 || !bPendingLuaCleanup)
	{
		return;
	}

	const bool bShouldRunEndPlay = bPendingLuaEndPlay;
	bPendingLuaCleanup = false;
	bPendingLuaEndPlay = false;
	if (bShouldRunEndPlay)
	{
		InvokeLuaEndPlay();
	}
	ClearLuaRuntime();
}

void ULuaScriptComponent::BindOwnerCollisionEvents()
{
	ClearCollisionBindings();

	if (!LuaOnOverlap && !LuaOnEndOverlap && !LuaOnHit && !LuaOnEndHit)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	for (UPrimitiveComponent* PrimitiveComponent : OwnerActor->GetPrimitiveComponents())
	{
		if (!PrimitiveComponent)
		{
			continue;
		}

		if ((LuaOnOverlap || LuaOnEndOverlap) && PrimitiveComponent->GetGenerateOverlapEvents())
		{
			BoundOverlapComponents.push_back(PrimitiveComponent);
			BeginOverlapHandles.push_back(PrimitiveComponent->OnComponentBeginOverlap.AddWeakUObject(this, &ULuaScriptComponent::HandleBeginOverlap));
			EndOverlapHandles.push_back(PrimitiveComponent->OnComponentEndOverlap.AddWeakUObject(this, &ULuaScriptComponent::HandleEndOverlap));
		}

		if (LuaOnHit || LuaOnEndHit)
		{
			BoundHitComponents.push_back(PrimitiveComponent);
			HitHandles.push_back(LuaOnHit
				? PrimitiveComponent->OnComponentHit.AddWeakUObject(this, &ULuaScriptComponent::HandleHit)
				: FDelegateHandle());
			EndHitHandles.push_back(LuaOnEndHit
				? PrimitiveComponent->OnComponentEndHit.AddWeakUObject(this, &ULuaScriptComponent::HandleEndHit)
				: FDelegateHandle());
		}
	}
}

void ULuaScriptComponent::BindOwnerSniperEvents()
{
	ClearSniperBindings();

	if (!LuaOnSniperHit && !LuaOnSniperDamaged && !LuaOnSniperKilled)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	if (LuaOnSniperHit)
	{
		USniperWeaponComponent* WeaponComponent = OwnerActor->GetComponentByClass<USniperWeaponComponent>();
		if (WeaponComponent)
		{
			BoundSniperWeaponComponent = WeaponComponent;
			SniperHitHandle = WeaponComponent->OnSniperHit.AddWeakUObject(this, &ULuaScriptComponent::HandleSniperHit);
		}
	}

	if (LuaOnSniperDamaged || LuaOnSniperKilled)
	{
		USniperDamageReceiverComponent* DamageReceiver = OwnerActor->GetComponentByClass<USniperDamageReceiverComponent>();
		if (DamageReceiver)
		{
			BoundSniperDamageReceiverComponent = DamageReceiver;
			SniperDamagedHandle = LuaOnSniperDamaged
				? DamageReceiver->OnSniperDamaged.AddWeakUObject(this, &ULuaScriptComponent::HandleSniperDamaged)
				: FDelegateHandle();
			SniperKilledHandle = LuaOnSniperKilled
				? DamageReceiver->OnSniperKilled.AddWeakUObject(this, &ULuaScriptComponent::HandleSniperKilled)
				: FDelegateHandle();
		}
	}
}

void ULuaScriptComponent::ClearCollisionBindings()
{
	for (int32 Index = 0; Index < static_cast<int32>(BoundOverlapComponents.size()); ++Index)
	{
		UPrimitiveComponent* PrimitiveComponent = BoundOverlapComponents[Index];
		if (!PrimitiveComponent)
		{
			continue;
		}

		if (Index < static_cast<int32>(BeginOverlapHandles.size()) && BeginOverlapHandles[Index].IsValid())
		{
			PrimitiveComponent->OnComponentBeginOverlap.Remove(BeginOverlapHandles[Index]);
		}

		if (Index < static_cast<int32>(EndOverlapHandles.size()) && EndOverlapHandles[Index].IsValid())
		{
			PrimitiveComponent->OnComponentEndOverlap.Remove(EndOverlapHandles[Index]);
		}
	}

	BoundOverlapComponents.clear();
	BeginOverlapHandles.clear();
	EndOverlapHandles.clear();

	for (int32 Index = 0; Index < static_cast<int32>(BoundHitComponents.size()); ++Index)
	{
		UPrimitiveComponent* PrimitiveComponent = BoundHitComponents[Index];
		if (!PrimitiveComponent)
		{
			continue;
		}

		if (Index < static_cast<int32>(HitHandles.size()) && HitHandles[Index].IsValid())
		{
			PrimitiveComponent->OnComponentHit.Remove(HitHandles[Index]);
		}

		if (Index < static_cast<int32>(EndHitHandles.size()) && EndHitHandles[Index].IsValid())
		{
			PrimitiveComponent->OnComponentEndHit.Remove(EndHitHandles[Index]);
		}
	}

	BoundHitComponents.clear();
	HitHandles.clear();
	EndHitHandles.clear();
}

void ULuaScriptComponent::ClearSniperBindings()
{
	USniperWeaponComponent* WeaponComponent = BoundSniperWeaponComponent.Get();
	if (WeaponComponent && SniperHitHandle.IsValid())
	{
		WeaponComponent->OnSniperHit.Remove(SniperHitHandle);
	}

	USniperDamageReceiverComponent* DamageReceiver = BoundSniperDamageReceiverComponent.Get();
	if (DamageReceiver && SniperDamagedHandle.IsValid())
	{
		DamageReceiver->OnSniperDamaged.Remove(SniperDamagedHandle);
	}
	if (DamageReceiver && SniperKilledHandle.IsValid())
	{
		DamageReceiver->OnSniperKilled.Remove(SniperKilledHandle);
	}

	BoundSniperWeaponComponent = nullptr;
	BoundSniperDamageReceiverComponent = nullptr;
	SniperHitHandle = FDelegateHandle();
	SniperDamagedHandle = FDelegateHandle();
	SniperKilledHandle = FDelegateHandle();
}

void ULuaScriptComponent::HandleBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnOverlap)
	{
		return;
	}

	AActor* SafeOtherActor = IsValid(OtherActor) ? OtherActor : nullptr;
	UPrimitiveComponent* SafeOverlappedComponent = IsValid(OverlappedComponent) ? OverlappedComponent : nullptr;
	UPrimitiveComponent* SafeOtherComp = IsValid(OtherComp) ? OtherComp : nullptr;
	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaOnOverlap(SafeOtherActor, SafeOverlappedComponent, SafeOtherComp);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua OnOverlap error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}
}

void ULuaScriptComponent::HandleEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 /*OtherBodyIndex*/)
{
	if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnEndOverlap)
	{
		return;
	}

	AActor* SafeOtherActor = IsValid(OtherActor) ? OtherActor : nullptr;
	UPrimitiveComponent* SafeOverlappedComponent = IsValid(OverlappedComponent) ? OverlappedComponent : nullptr;
	UPrimitiveComponent* SafeOtherComp = IsValid(OtherComp) ? OtherComp : nullptr;
	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaOnEndOverlap(SafeOtherActor, SafeOverlappedComponent, SafeOtherComp);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua OnEndOverlap error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}
}

void ULuaScriptComponent::HandleHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& HitResult)
{
	if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnHit)
	{
		return;
	}

	AActor* SafeOtherActor = IsValid(OtherActor) ? OtherActor : nullptr;
	UPrimitiveComponent* SafeHitComponent = IsValid(HitComponent) ? HitComponent : nullptr;
	UPrimitiveComponent* SafeOtherComp = IsValid(OtherComp) ? OtherComp : nullptr;
	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaOnHit(SafeOtherActor, SafeHitComponent, SafeOtherComp, NormalImpulse, HitResult);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua OnHit error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}
}

void ULuaScriptComponent::HandleEndHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp)
{
	if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnEndHit)
	{
		return;
	}

	AActor* SafeOtherActor = IsValid(OtherActor) ? OtherActor : nullptr;
	UPrimitiveComponent* SafeHitComponent = IsValid(HitComponent) ? HitComponent : nullptr;
	UPrimitiveComponent* SafeOtherComp = IsValid(OtherComp) ? OtherComp : nullptr;
	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaOnEndHit(SafeOtherActor, SafeHitComponent, SafeOtherComp);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua OnEndHit error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}
}

void ULuaScriptComponent::HandleSniperHit(const FSniperHitInfo& HitInfo)
{
	if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnSniperHit)
	{
		return;
	}

	FSniperHitInfo SafeHitInfo = HitInfo;
	SafeHitInfo.HitActor = IsValid(SafeHitInfo.HitActor) ? SafeHitInfo.HitActor : nullptr;
	SafeHitInfo.Shooter = IsValid(SafeHitInfo.Shooter) ? SafeHitInfo.Shooter : nullptr;

	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaOnSniperHit(SafeHitInfo);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua OnSniperHit error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}
}

void ULuaScriptComponent::HandleSniperDamaged(const FSniperHitInfo& HitInfo)
{
	if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnSniperDamaged)
	{
		return;
	}

	FSniperHitInfo SafeHitInfo = HitInfo;
	SafeHitInfo.HitActor = IsValid(SafeHitInfo.HitActor) ? SafeHitInfo.HitActor : nullptr;
	SafeHitInfo.Shooter = IsValid(SafeHitInfo.Shooter) ? SafeHitInfo.Shooter : nullptr;

	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaOnSniperDamaged(SafeHitInfo);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua OnSniperDamaged error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}
}

void ULuaScriptComponent::HandleSniperKilled(const FSniperHitInfo& HitInfo)
{
	if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnSniperKilled)
	{
		return;
	}

	FSniperHitInfo SafeHitInfo = HitInfo;
	SafeHitInfo.HitActor = IsValid(SafeHitInfo.HitActor) ? SafeHitInfo.HitActor : nullptr;
	SafeHitInfo.Shooter = IsValid(SafeHitInfo.Shooter) ? SafeHitInfo.Shooter : nullptr;

	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaOnSniperKilled(SafeHitInfo);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua OnSniperKilled error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}
}

bool ULuaScriptComponent::CallFunction(const FString& FunctionName)
{
	if (!Env.valid())
	{
		return false;
	}

	sol::object Target = Env[FunctionName.c_str()];
	if (!Target.valid() || Target.get_type() != sol::type::function)
	{
		return false;
	}

	sol::protected_function Fn = Target;
	bool bOk = false;
	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = Fn();
		bOk = Result.valid();
		if (!bOk)
		{
			sol::error Err = Result;
			UE_LOG("Lua %s error in %s: %s", FunctionName.c_str(), ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}
	return bOk;
}

void ULuaScriptComponent::DispatchOverlap(AActor* OtherActor)
{
	if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnOverlap)
	{
		return;
	}

	AActor* SafeOtherActor = IsValid(OtherActor) ? OtherActor : nullptr;
	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaOnOverlap(SafeOtherActor);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua OnOverlap error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}
}

void ULuaScriptComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (LuaTick)
	{
		FLuaCallScope Scope(this);
		sol::protected_function_result Result = LuaTick(DeltaTime);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("Lua Tick error in %s: %s", ScriptFile.c_str(), Err.what());
			FLuaDebugManager::OnLuaError(ScriptFile, Err.what(), false);
		}
	}
}

void ULuaScriptComponent::PreGetEditableProperties()
{
	UActorComponent::PreGetEditableProperties();
	EnsureDefaultScriptFile();
	UpdatePauseTickEligibility();
}

bool ULuaScriptComponent::ShouldExposeProperty(const FProperty& Property) const
{
	if (!UActorComponent::ShouldExposeProperty(Property))
	{
		return false;
	}

	if (Property.Name && std::strcmp(Property.Name, "InitialGameState") == 0)
	{
		return IsGeneralManagerScriptFile(ScriptFile);
	}

	return true;
}

FString ULuaScriptComponent::GetInitialGameStateName() const
{
	return ToGameStateName(InitialGameState);
}

void ULuaScriptComponent::EnsureDefaultScriptFile()
{
	if (!ScriptFile.empty())
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->GetFName().IsValid())
	{
		return;
	}

	ULevel* Level = OwnerActor->GetLevel();
	if (!Level || !Level->GetFName().IsValid())
	{
		return;
	}

	ScriptFile = Level->GetFName().ToString() + "_" + OwnerActor->GetFName().ToString() + ".lua";
}

void ULuaScriptComponent::UpdatePauseTickEligibility()
{
	PrimaryComponentTick.bTickEvenWhenPaused = IsGeneralManagerScriptFile(ScriptFile);
}
