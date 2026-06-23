#include "LuaScriptManager.h"

#include "Lua/LuaDebugManager.h"

#include "Animation/Instance/LuaAnimInstance.h"
#include "Component/Script/LuaBlueprintComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Core/Logging/Log.h"
#include "Core/Logging/Notification.h"
#include "Object/Object.h"
#include "Platform/Paths.h"

#include <algorithm>
#include <filesystem>

std::unique_ptr<sol::state>                 FLuaScriptManager::Lua;
sol::protected_function                     FLuaScriptManager::OnEscapePressedCallback;
std::mutex                                  FLuaScriptManager::ComponentMutex;
TArray<TWeakObjectPtr<ULuaScriptComponent>> FLuaScriptManager::RegisteredComponents;
TArray<TWeakObjectPtr<ULuaAnimInstance>>    FLuaScriptManager::RegisteredAnimInstances;
FSubscriptionID                             FLuaScriptManager::WatchSub = 0;

void FLuaScriptManager::Initialize()
{
    Lua = std::make_unique<sol::state>();
    Lua->open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::coroutine, sol::lib::debug);
    (*Lua)["package"]["path"] = FPaths::ToUtf8(FPaths::Combine(FPaths::ScriptDir(), L"?.lua").c_str());

    // 한글 경로 호환을 위해 require 의 파일 검색을 wide-aware 로 교체.
    // Lua 5.2+ 는 package.searchers, Lua 5.1/LuaJIT 은 package.loaders 를 사용한다.
    sol::table  Package       = (*Lua)["package"];
    sol::object Searchers     = Package["searchers"];
    sol::table  ModuleLoaders = Searchers.valid() && Searchers.get_type() == sol::type::table
            ? Searchers.as<sol::table>()
            : Package["loaders"].get<sol::table>();
    ModuleLoaders[2] = [](sol::this_state ts, const std::string& ModName) -> sol::object
    {
        sol::state_view    L(ts);
        const std::wstring WidePath = FPaths::Combine(FPaths::ScriptDir(), FPaths::ToWide(ModName + ".lua"));
        std::error_code    EC;
        if (!std::filesystem::exists(WidePath, EC))
        {
            return sol::make_object(L, std::string("\n\tno file '") + FPaths::ToUtf8(WidePath) + "'");
        }

        FString Content;
        if (!ReadScriptFileContent(ModName + ".lua", Content))
        {
            return sol::make_object(L, std::string("\n\tcannot read '") + FPaths::ToUtf8(WidePath) + "'");
        }

        const FString    ChunkName = FPaths::ToUtf8(WidePath);
        sol::load_result LR        = L.load(Content, ChunkName);
        if (!LR.valid())
        {
            sol::error Err = LR;
            return sol::make_object(L, std::string("\n\t") + Err.what());
        }
        return LR.get<sol::object>();
    };

    // 모든 sol::protected_function 호출의 default error handler 를 debug.traceback 으로 설정.
    // RegisterBindings 안에서 helper Lua 파일을 로드하기 전에 먼저 걸어야 helper load error 도 stacktrace 를 갖는다.
    if (sol::object DebugObject = (*Lua)["debug"]; DebugObject.valid() && DebugObject.get_type() == sol::type::table)
    {
        sol::table  DebugTable      = DebugObject.as<sol::table>();
        sol::object TracebackObject = DebugTable["traceback"];
        if (TracebackObject.valid() && TracebackObject.get_type() == sol::type::function)
        {
            sol::protected_function::set_default_handler(TracebackObject.as<sol::function>());
        }
    }

    FLuaDebugManager::Initialize(Lua->lua_state());
    FLuaDebugManager::RegisterLuaBindings(*Lua);
    RegisterBindings(*Lua);

    FWatchID WatchID = FDirectoryWatcher::Get().Watch(FPaths::ScriptDir(), "");
    if (WatchID != 0)
    {
        WatchSub = FDirectoryWatcher::Get().Subscribe(
            WatchID,
            [](const TSet<FString>& Files)
            {
                FLuaScriptManager::OnScriptsChanged(Files);
            }
        );
    }
}

void FLuaScriptManager::Shutdown()
{
    if (WatchSub != 0)
    {
        FDirectoryWatcher::Get().Unsubscribe(WatchSub);
        WatchSub = 0;
    }

    TArray<TWeakObjectPtr<ULuaScriptComponent>> ComponentsToRelease;
    TArray<TWeakObjectPtr<ULuaAnimInstance>>    AnimInstancesToRelease;
    {
        std::lock_guard<std::mutex> Lock(ComponentMutex);
        ComponentsToRelease    = RegisteredComponents;
        AnimInstancesToRelease = RegisteredAnimInstances;
    }

    // lua_State 가 살아있는 동안 런타임 객체들이 들고 있는 sol reference 를 먼저 해제한다.
    // 이 작업을 Lua.reset() 뒤로 미루면 GC/dtor 단계에서 sol::basic_reference::~basic_reference 가
    // 닫힌 lua_State 에 luaL_unref 를 호출하며 lua51.dll 내부에서 크래시난다.
    for (const TWeakObjectPtr<ULuaScriptComponent>& ComponentPtr : ComponentsToRelease)
    {
        if (ULuaScriptComponent* Component = ComponentPtr.GetEvenIfPendingKill())
        {
            if (IsAliveObject(Component))
            {
                Component->ReleaseLuaRuntimeForShutdown();
            }
        }
    }
    for (const TWeakObjectPtr<ULuaAnimInstance>& InstancePtr : AnimInstancesToRelease)
    {
        if (ULuaAnimInstance* Instance = InstancePtr.GetEvenIfPendingKill())
        {
            if (IsAliveObject(Instance))
            {
                Instance->ReleaseLuaRuntimeForShutdown();
            }
        }
    }

    // ULuaBlueprintComponent 는 ULuaScriptComponent/ULuaAnimInstance 와 달리 별도 레지스트리에
    // 등록되지 않으므로, 전역 객체 배열을 훑어 lua_State 가 살아있는 동안 sol 핸들을 해제한다.
    // (누락 시 FEngineLoop::Shutdown 의 최종 GC sweep → BeginDestroy → ClearLuaRuntime 이
    //  이미 닫힌 lua_State 에 luaL_unref 를 호출하며 lua51.dll 에서 크래시)
    for (UObject* Obj : GUObjectArray)
    {
        if (!IsAliveObject(Obj) || !Obj->IsA<ULuaBlueprintComponent>())
        {
            continue;
        }
        static_cast<ULuaBlueprintComponent*>(Obj)->ReleaseLuaRuntimeForShutdown();
    }

    {
        std::lock_guard<std::mutex> Lock(ComponentMutex);
        RegisteredComponents.clear();
        RegisteredAnimInstances.clear();
    }

    // 등록된 Lua 콜백 (sol::protected_function 들) 을 lua_State 가 살아있는 동안 먼저 release.
    // static 멤버라 프로그램 종료 시점까지 살아있는데, 그때 destructor 가 luaL_unref 를
    // 호출하면서 이미 reset 된 lua_State 를 만지면 크래시. 빈 함수로 덮어써 deref 를 지금
    // (Lua 가 valid 한 동안) 일으킨다.
    OnEscapePressedCallback = sol::protected_function();
    ClearReflectedEventOverrides();

    FLuaDebugManager::Shutdown();
    Lua.reset();
}

void FLuaScriptManager::OnScriptsChanged(const TSet<FString>& ChangedFiles)
{
    TSet<ULuaScriptComponent*> Targets;

    InvalidateChangedModules(ChangedFiles);

    {
        std::lock_guard<std::mutex> Lock(ComponentMutex);
        RegisteredComponents.erase(
            std::remove_if(
                RegisteredComponents.begin(),
                RegisteredComponents.end(),
                [](const TWeakObjectPtr<ULuaScriptComponent>& Component)
                {
                    return !Component.IsValid();
                }
            ),
            RegisteredComponents.end()
        );
        for (const TWeakObjectPtr<ULuaScriptComponent>& ComponentPtr : RegisteredComponents)
        {
            ULuaScriptComponent* Component = ComponentPtr.Get();
            if (!IsValid(Component)) continue;

            const FString& ScriptFile = Component->GetScriptFile();
            if (ScriptFile.empty()) continue;

            for (const FString& File : ChangedFiles)
            {
                if (File == ScriptFile)
                {
                    Targets.insert(Component);
                    break;
                }
            }
        }
    }

    for (ULuaScriptComponent* Component : Targets)
    {
        if (!Component) continue;

        UE_LOG("[LuaHotReload] Reloading: %s", Component->GetScriptFile().c_str());
        FNotificationManager::Get().AddNotification("Lua Reloaded: " + Component->GetScriptFile(), ENotificationType::Success, 3.0f);
        Component->ReloadScript();
    }

    TSet<ULuaAnimInstance*> AnimTargets;
    {
        std::lock_guard<std::mutex> Lock(ComponentMutex);
        RegisteredAnimInstances.erase(
            std::remove_if(
                RegisteredAnimInstances.begin(),
                RegisteredAnimInstances.end(),
                [](const TWeakObjectPtr<ULuaAnimInstance>& Instance)
                {
                    return !Instance.IsValid();
                }
            ),
            RegisteredAnimInstances.end()
        );
        for (const TWeakObjectPtr<ULuaAnimInstance>& InstPtr : RegisteredAnimInstances)
        {
            ULuaAnimInstance* Inst = InstPtr.Get();
            if (!IsValid(Inst)) continue;
            const FString& AnimScript = Inst->ScriptFile;
            if (AnimScript.empty()) continue;
            for (const FString& File : ChangedFiles)
            {
                if (File == AnimScript)
                {
                    AnimTargets.insert(Inst);
                    break;
                }
            }
        }
    }
    for (ULuaAnimInstance* Inst : AnimTargets)
    {
        if (!Inst) continue;
        UE_LOG("[LuaHotReload] Reloading Anim: %s", Inst->ScriptFile.c_str());
        FNotificationManager::Get().AddNotification("Anim Reloaded: " + Inst->ScriptFile, ENotificationType::Success, 3.0f);
        Inst->ReloadScript();
    }
}

void FLuaScriptManager::InvalidateChangedModules(const TSet<FString>& ChangedFiles)
{
    if (!Lua) return;

    sol::table Loaded = (*Lua)["package"]["loaded"];
    if (!Loaded.valid()) return;

    for (const FString& File : ChangedFiles)
    {
        FString ModuleName = GetModuleNameFromPath(File);
        if (ModuleName.empty()) continue;

        Loaded[ModuleName] = sol::nil;
        UE_LOG("[LuaHotReload] Invalidated module: %s", ModuleName.c_str());

        FString SlashModuleName = ModuleName;
        for (char& Ch : SlashModuleName)
        {
            if (Ch == '.')
            {
                Ch = '/';
            }
        }
        if (SlashModuleName != ModuleName)
        {
            Loaded[SlashModuleName] = sol::nil;
            UE_LOG("[LuaHotReload] Invalidated module: %s", SlashModuleName.c_str());
        }
    }
}

FString FLuaScriptManager::GetModuleNameFromPath(const FString& ScriptPath)
{
    if (ScriptPath.empty())
    {
        return {};
    }

    FString Normalized = ScriptPath;
    for (char& Ch : Normalized)
    {
        if (Ch == '\\')
        {
            Ch = '/';
        }
    }

    constexpr const char* LuaExt = ".lua";
    if (Normalized.size() <= 4 || Normalized.substr(Normalized.size() - 4) != LuaExt)
    {
        return {};
    }

    Normalized.erase(Normalized.size() - 4);
    for (char& Ch : Normalized)
    {
        if (Ch == '/')
        {
            Ch = '.';
        }
    }

    return Normalized;
}

void FLuaScriptManager::RegisterBindings(sol::state& Lua)
{
    RegisterLuaHelpers(Lua);
    RegisterCoreBindings(Lua);
    RegisterMathBindings(Lua);
    RegisterReflectionBindings(Lua);
    RegisterActorBindings(Lua);
    RegisterUIBindings(Lua);
}
