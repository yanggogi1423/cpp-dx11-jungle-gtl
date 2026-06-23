#pragma once

#include "Component/ActorComponent.h"
#include "Core/Delegate.h"
#include "Math/Vector.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Object/GarbageCollection.h"
#include "Source/Engine/Component/Script/LuaScriptComponent.generated.h"
#include <sol/sol.hpp>

class UPrimitiveComponent;
class USniperDamageReceiverComponent;
class USniperWeaponComponent;
struct FHitResult;
struct FSniperHitInfo;

UENUM()
enum class EGeneralManagerStartState : uint8
{
	Intro = 0,
	Main = 1,
	Loading = 2,
	PreInGame = 3,
	InGame = 4,
	Defeat1 = 5,
	Defeat2 = 6,
	Victory = 7
};

UCLASS()
class ULuaScriptComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	ULuaScriptComponent();
	~ULuaScriptComponent();

	bool InitializeLua();
	void ReleaseLuaRuntimeForShutdown();
	UFUNCTION(Callable, Exec, CallInEditor, Category="Script")
	bool ReloadScript();

	virtual void BeginPlay() override;
	virtual void EndPlay() override;
	void BeginDestroy() override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;


	void PreGetEditableProperties() override;
	bool ShouldExposeProperty(const FProperty& Property) const override;
	UFUNCTION(Pure, Category="Script")
	const FString& GetScriptFile() const { return ScriptFile; }
	UFUNCTION(Pure, Category="Script")
	FString GetScriptFileValue() const { return ScriptFile; }
	UFUNCTION(Callable, Category="Script")
	void SetScriptFile(const FString& InScriptFile);
	UFUNCTION(Pure, Category="General Manager")
	EGeneralManagerStartState GetInitialGameState() const { return InitialGameState; }
	UFUNCTION(Pure, Category="General Manager")
	FString GetInitialGameStateName() const;
	void DispatchOverlap(class AActor* OtherActor);

	// Lua script 의 환경(env)에서 인자 없는 전역 함수 하나를 호출. 함수가 없거나
	// nil 이면 조용히 false 반환 — 호출자는 lua 쪽 함수 정의 여부에 신경 쓸 필요 없음.
	UFUNCTION(Callable, Exec, Category="Script")
	bool CallFunction(const FString& FunctionName);

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void EnsureDefaultScriptFile();
	void UpdatePauseTickEligibility();
	void BindOwnerCollisionEvents();
	void BindOwnerSniperEvents();
	void ClearCollisionBindings();
	void ClearSniperBindings();
	void ClearLuaRuntime();
	void InvokeLuaEndPlay();
	void HandleDeferredLuaCleanup();
	void HandleBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);
	void HandleEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);
	void HandleHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& HitResult);
	void HandleEndHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp);
	void HandleSniperHit(const FSniperHitInfo& HitInfo);
	void HandleSniperDamaged(const FSniperHitInfo& HitInfo);
	void HandleSniperKilled(const FSniperHitInfo& HitInfo);

	UPROPERTY(Edit, Save, Category="Script", DisplayName="ScriptFile", AssetType="Script")
	FString ScriptFile;

	UPROPERTY(Edit, Save, Category="General Manager", DisplayName="Initial Game State", Type=Enum, Enum=EGeneralManagerStartState)
	EGeneralManagerStartState InitialGameState = EGeneralManagerStartState::InGame;
	
	sol::environment Env;
	sol::protected_function LuaBeginPlay;
	sol::protected_function LuaTick;
	sol::protected_function LuaEndPlay;
	sol::protected_function LuaOnOverlap;
	sol::protected_function LuaOnEndOverlap;
	sol::protected_function LuaOnHit;
	sol::protected_function LuaOnEndHit;
	sol::protected_function LuaOnSniperHit;
	sol::protected_function LuaOnSniperDamaged;
	sol::protected_function LuaOnSniperKilled;

	bool bEndPlayRouted = false;
	bool bHasCalledLuaEndPlay = false;
	bool bPendingLuaEndPlay = false;
	bool bPendingLuaCleanup = false;
	int32 LuaCallDepth = 0;

	struct FLuaCallScope
	{
		ULuaScriptComponent* Owner = nullptr;

		explicit FLuaCallScope(ULuaScriptComponent* InOwner)
			: Owner(InOwner)
		{
			if (Owner)
			{
				FGarbageCollector::Get().PushCollectionBlock();
				++Owner->LuaCallDepth;
			}
		}

		~FLuaCallScope()
		{
			if (!Owner)
			{
				return;
			}

			--Owner->LuaCallDepth;
			Owner->HandleDeferredLuaCleanup();
			FGarbageCollector::Get().PopCollectionBlock();
		}
	};

	TArray<TWeakObjectPtr<UPrimitiveComponent>> BoundOverlapComponents;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> BoundHitComponents;
	TArray<FDelegateHandle> BeginOverlapHandles;
	TArray<FDelegateHandle> EndOverlapHandles;
	TArray<FDelegateHandle> HitHandles;
	TArray<FDelegateHandle> EndHitHandles;
	TWeakObjectPtr<USniperWeaponComponent> BoundSniperWeaponComponent;
	TWeakObjectPtr<USniperDamageReceiverComponent> BoundSniperDamageReceiverComponent;
	FDelegateHandle SniperHitHandle;
	FDelegateHandle SniperDamagedHandle;
	FDelegateHandle SniperKilledHandle;
};
