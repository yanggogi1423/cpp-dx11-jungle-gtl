#include "Runtime/Script/ScriptManager.h"

#include "Animation/ActorSequence.h"
#include "Asset/CurveFloatAsset.h"
#include "Runtime/Script/ScriptComponent.h"
#include "Runtime/Script/ScriptUtils.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimStateMachineInstance.h"
#include "Animation/LuaAnimInstance.h"

namespace
{
    bool LuaAnimInstanceSetFloat(UAnimInstance& Self, const FString& Name, float Value)
    {
        if (UAnimStateMachineInstance* StateMachineInstance = Cast<UAnimStateMachineInstance>(&Self))
        {
            StateMachineInstance->SetFloat(FName(Name), Value);
            return true;
        }

        if (ULuaAnimInstance* LuaAnimInstance = Cast<ULuaAnimInstance>(&Self))
        {
            LuaAnimInstance->SetFloat(Name, Value);
            return true;
        }

        return false;
    }

    bool LuaAnimInstanceSetBool(UAnimInstance& Self, const FString& Name, bool Value)
    {
        if (UAnimStateMachineInstance* StateMachineInstance = Cast<UAnimStateMachineInstance>(&Self))
        {
            StateMachineInstance->SetBool(FName(Name), Value);
            return true;
        }

        if (ULuaAnimInstance* LuaAnimInstance = Cast<ULuaAnimInstance>(&Self))
        {
            LuaAnimInstance->SetBool(Name, Value);
            return true;
        }

        return false;
    }

    bool LuaAnimInstanceSetInt(UAnimInstance& Self, const FString& Name, int32 Value)
    {
        if (UAnimStateMachineInstance* StateMachineInstance = Cast<UAnimStateMachineInstance>(&Self))
        {
            StateMachineInstance->SetInt(FName(Name), Value);
            return true;
        }

        if (ULuaAnimInstance* LuaAnimInstance = Cast<ULuaAnimInstance>(&Self))
        {
            LuaAnimInstance->SetInt(Name, Value);
            return true;
        }

        return false;
    }
}

void FScriptManager::BindAnimationTypes()
{
    LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, UCurveFloatAsset, "CurveFloatAsset", UObject)
    LUA_METHOD(Evaluate, Evaluate);
    LUA_METHOD(GetAssetPath, GetAssetPath);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, UActorSequence, "ActorSequence", UObject)
    LUA_FIELD(StartTime, StartTime);
    LUA_FIELD(Duration, Duration);
    LUA_FIELD(Loop, bLoop);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, UActorSequencePlayer, "ActorSequencePlayer", UObject)
    LUA_METHOD(Play, Play);
    LUA_METHOD(Pause, Pause);
    LUA_METHOD(Stop, Stop);
    LUA_METHOD(SetCurrentTime, SetCurrentTime);
    LUA_METHOD(GetCurrentTime, GetCurrentTime);
    LUA_METHOD(IsPlaying, IsPlaying);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR(GLuaState, FLuaTimeline, "LuaTimeline")
    LUA_METHOD(Play, Play);
    LUA_METHOD(Pause, Pause);
    LUA_METHOD(Stop, Stop);
    LUA_METHOD(Tick, Tick);
    LUA_METHOD(SetPlayRate, SetPlayRate);
    LUA_METHOD(SetLoop, SetLoop);
    LUA_METHOD(SetCurrentTime, SetCurrentTime);
    LUA_METHOD(GetCurrentTime, GetCurrentTime);
    LUA_METHOD(AddFloatTrack, AddFloatTrack);
    LUA_METHOD(ClearTracks, ClearTracks);
    LUA_END_TYPE();

	LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, UAnimInstance, "AnimInstance", UObject)
    LUA_SET(SetFloat, &LuaAnimInstanceSetFloat);
    LUA_SET(SetBool, &LuaAnimInstanceSetBool);
    LUA_SET(SetInt, &LuaAnimInstanceSetInt);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, ULuaAnimInstance, "LuaAnimInstance", UAnimInstance, UObject)
    LUA_METHOD(SetFloat, SetFloat);
    LUA_METHOD(SetBool, SetBool);
    LUA_METHOD(SetInt, SetInt);
    LUA_METHOD(GetFloat, GetFloat);
    LUA_METHOD(GetBool, GetBool);
    LUA_METHOD(GetInt, GetInt);
    LUA_METHOD(GetCurrentState, GetCurrentState);
    LUA_METHOD(IsInTransition, IsInTransition);
    LUA_METHOD(GetTransitionFromState, GetTransitionFromState);
    LUA_METHOD(GetTransitionToState, GetTransitionToState);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, UAnimStateMachineInstance, "AnimStateMachineInstance", UAnimInstance, UObject)
    LUA_SET(SetFloat, [](UAnimStateMachineInstance& Self, const FString& Name, float Value)
            { Self.SetFloat(FName(Name), Value); });
    LUA_SET(SetBool, [](UAnimStateMachineInstance& Self, const FString& Name, bool Value)
            { Self.SetBool(FName(Name), Value); });
    LUA_SET(SetInt, [](UAnimStateMachineInstance& Self, const FString& Name, int32 Value)
            { Self.SetInt(FName(Name), Value); });
    LUA_METHOD(GetCurrentState, GetCurrentState);
    LUA_METHOD(IsInTransition, IsInTransition);
    LUA_METHOD(GetTransitionFromState, GetTransitionFromState);
    LUA_METHOD(GetTransitionToState, GetTransitionToState);
    LUA_END_TYPE();
}
