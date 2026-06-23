#include "Game/Lua/GameLuaBindings.h"

#include "sol/sol.hpp"

#include "Component/Gameplay/CombatCoverAgentComponent.h"
#include "Component/Gameplay/CombatFlowManagerComponent.h"
#include "Engine/Runtime/Engine.h"
#include "Engine/Runtime/EngineInitHooks.h"
#include "GameFramework/AActor.h"
#include "Lua/LuaScriptManager.h"
#include "Object/Object.h"

// ============================================================
// 게임-특화 Lua 바인딩 등록 위치 — 현재는 비어 있음.
//
// Engine 의 FLuaScriptManager 가 등록하는 일반 binding (AActor / APawn / FVector /
// UWorld / Anim 등) 만으로 동작하지 않는 game-specific usertype (ACarPawn /
// AGameStateXxx / 전용 enum 등) 이 도입되면 여기에 new_usertype 으로 추가한다.
//
// 호출 시점: UEngine::Init() 이 FLuaScriptManager::Initialize() 를 끝낸 직후.
// 등록은 EngineInitHooks 에 자동으로 걸려 GameEngine / EditorEngine 두 엔트리 모두
// 같은 바인딩이 적용된다 (PIE 호환).
// ============================================================
void RegisterGameLuaBindings(sol::state& Lua)
{
	Lua.new_usertype<UCombatCoverAgentComponent>(
		"CombatCoverAgentComponent",
		sol::base_classes,
		sol::bases<UActorComponent, UObject>(),
		"GetTeamTag",
		&UCombatCoverAgentComponent::GetTeamTag,
		"SetTeamTag",
		&UCombatCoverAgentComponent::SetTeamTag,
		"GetDisplayName",
		&UCombatCoverAgentComponent::GetDisplayName,
		"SetDisplayName",
		&UCombatCoverAgentComponent::SetDisplayName,
		"GetStateName",
		&UCombatCoverAgentComponent::GetStateName,
		"GetHealth",
		&UCombatCoverAgentComponent::GetHealth,
		"SetHealth",
		&UCombatCoverAgentComponent::SetHealth,
		"GetMaxHealth",
		&UCombatCoverAgentComponent::GetMaxHealth,
		"SetMaxHealth",
		&UCombatCoverAgentComponent::SetMaxHealth,
		"GetHealthRatio",
		&UCombatCoverAgentComponent::GetHealthRatio,
		"GetCurrentNodeId",
		&UCombatCoverAgentComponent::GetCurrentNodeId,
		"GetCurrentSlotId",
		&UCombatCoverAgentComponent::GetCurrentSlotId,
		"GetTargetNodeId",
		&UCombatCoverAgentComponent::GetTargetNodeId,
		"GetTargetSlotId",
		&UCombatCoverAgentComponent::GetTargetSlotId,
		"GetFireRange",
		&UCombatCoverAgentComponent::GetFireRange,
		"GetMovingFireRange",
		&UCombatCoverAgentComponent::GetMovingFireRange,
		"GetEffectiveFireRange",
		&UCombatCoverAgentComponent::GetEffectiveFireRange,
		"GetAttackDamage",
		&UCombatCoverAgentComponent::GetAttackDamage,
		"GetCurrentTarget",
		&UCombatCoverAgentComponent::GetCurrentTarget,
		"GetIncomingFireCount",
		&UCombatCoverAgentComponent::GetIncomingFireCount,
		"GetIncomingAttackDamage",
		&UCombatCoverAgentComponent::GetIncomingAttackDamage,
		"GetSuppressionTimeRemaining",
		&UCombatCoverAgentComponent::GetSuppressionTimeRemaining,
		"IsAlive",
		&UCombatCoverAgentComponent::IsAlive,
		"IsEngaging",
		&UCombatCoverAgentComponent::IsEngaging,
		"IsMovingForCombatRange",
		&UCombatCoverAgentComponent::IsMovingForCombatRange,
		"IsInCover",
		&UCombatCoverAgentComponent::IsInCover,
		"IsSuppressed",
		&UCombatCoverAgentComponent::IsSuppressed,
		"CanFireWhileMoving",
		&UCombatCoverAgentComponent::CanFireWhileMoving,
		"GetCombatAnimationMoveState",
		&UCombatCoverAgentComponent::GetCombatAnimationMoveState,
		"ShouldRunDuringCombatMovement",
		&UCombatCoverAgentComponent::ShouldRunDuringCombatMovement,
		"IsInStandingCombatSlot",
		&UCombatCoverAgentComponent::IsInStandingCombatSlot,
		"ShouldUseStandingFire",
		&UCombatCoverAgentComponent::ShouldUseStandingFire,
		"ConsumeHitReaction",
		&UCombatCoverAgentComponent::ConsumeHitReaction,
		"GetCombatRoleName",
		&UCombatCoverAgentComponent::GetCombatRoleName,
		"GetResolvedCombatRoleName",
		&UCombatCoverAgentComponent::GetResolvedCombatRoleName,
		"GetCurrentCombatMoveSpeed",
		&UCombatCoverAgentComponent::GetCurrentCombatMoveSpeed,
		"HasLastSniperHit",
		&UCombatCoverAgentComponent::HasLastSniperHit,
		"GetLastHitBoneName",
		&UCombatCoverAgentComponent::GetLastHitBoneName,
		"GetLastHitBodyName",
		&UCombatCoverAgentComponent::GetLastHitBodyName,
		"GetLastHitRegionName",
		&UCombatCoverAgentComponent::GetLastHitRegionName,
		"GetLastHitRegionDisplayName",
		&UCombatCoverAgentComponent::GetLastHitRegionDisplayName,
		"GetLastHitDamage",
		&UCombatCoverAgentComponent::GetLastHitDamage,
		"GetLastHitScoreMultiplier",
		&UCombatCoverAgentComponent::GetLastHitScoreMultiplier,
		"GetLastHitScoreValue",
		&UCombatCoverAgentComponent::GetLastHitScoreValue,
		"WasLastHitKilled",
		&UCombatCoverAgentComponent::WasLastHitKilled,
		"MarkDead",
		&UCombatCoverAgentComponent::MarkDead
	);

	Lua.new_usertype<UCombatFlowManagerComponent>(
		"CombatFlowManagerComponent",
		sol::base_classes,
		sol::bases<UActorComponent, UObject>(),
		"RefreshRegistry",
		&UCombatFlowManagerComponent::RefreshRegistry,
		"ResetRuntimeState",
		&UCombatFlowManagerComponent::ResetRuntimeState,
		"UpdateCombatSimulation",
		&UCombatFlowManagerComponent::UpdateCombatSimulation,
		"GetAgents",
		[](UCombatFlowManagerComponent& Manager, sol::this_state State)
		{
			sol::state_view L(State);
			sol::table Result = L.create_table();
			int Index = 1;
			for (UCombatCoverAgentComponent* Agent : Manager.GetAgents())
			{
				if (IsValid(Agent))
				{
					Result[Index++] = Agent;
				}
			}
			return Result;
		}
	);

	sol::object ExistingCombat = Lua["Combat"];
	sol::table Combat = (ExistingCombat.valid() && ExistingCombat.get_type() == sol::type::table)
		? ExistingCombat.as<sol::table>()
		: Lua.create_named_table("Combat");
	Combat.set_function(
		"FindFlowManager",
		[]() -> UCombatFlowManagerComponent*
		{
			return GEngine ? UCombatFlowManagerComponent::FindInWorld(GEngine->GetWorld()) : nullptr;
		}
	);
	Combat.set_function(
		"GetAgents",
		[](sol::this_state State)
		{
			sol::state_view L(State);
			sol::table Result = L.create_table();
			UCombatFlowManagerComponent* Manager = GEngine
				? UCombatFlowManagerComponent::FindInWorld(GEngine->GetWorld())
				: nullptr;
			if (!IsValid(Manager))
			{
				return Result;
			}

			int Index = 1;
			for (UCombatCoverAgentComponent* Agent : Manager->GetAgents())
			{
				if (IsValid(Agent))
				{
					Result[Index++] = Agent;
				}
			}
			return Result;
		}
	);

	sol::table ActorType = Lua["Actor"];
	if (ActorType.valid())
	{
		ActorType.set_function(
			"GetCombatCoverAgentComponent",
			[](AActor& Actor) -> UCombatCoverAgentComponent*
			{
				return Actor.GetComponentByClass<UCombatCoverAgentComponent>();
			}
		);
	}
}

// 자기-등록 — Editor / Game 측이 RegisterGameLuaBindings 함수명을 모르고도
// FEngineInitHooks::RunAll() 한 번이면 호출되도록 static initializer 로 등록.
namespace
{
	void RunRegisterGameLuaBindings()
	{
		RegisterGameLuaBindings(FLuaScriptManager::GetState());
	}

	struct GameLuaBindingsAutoReg
	{
		GameLuaBindingsAutoReg() { FEngineInitHooks::Register(&RunRegisterGameLuaBindings); }
	};

	static GameLuaBindingsAutoReg gAutoReg;
}
