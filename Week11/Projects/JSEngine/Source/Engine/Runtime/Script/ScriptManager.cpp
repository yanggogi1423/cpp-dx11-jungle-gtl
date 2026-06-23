#include "ScriptManager.h"

#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "GameFramework/AActor.h"
#include "Runtime/Script/ScriptComponent.h"
#include "ThirdParty/sol/sol.hpp"
#include "ThirdParty/luajit/src/lauxlib.h"
#include "ThirdParty/luajit/src/luajit.h"

#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <sstream>

namespace fs = std::filesystem;

void Log(const std::string& Msg);

namespace
{
    fs::path WithLuaExtension(fs::path Path);
    bool TryResolveLuaScriptPath(const FString& ScriptName, fs::path& OutPath);

    FString MakeProjectRelativePath(const fs::path& Path)
    {
        const fs::path NormalizedPath = Path.lexically_normal();
        const fs::path RootPath = fs::path(FPaths::RootDir()).lexically_normal();
        const fs::path RelativePath = NormalizedPath.lexically_relative(RootPath);
        if (!RelativePath.empty() && RelativePath.native().find(L"..") != 0)
        {
            return FPaths::Normalize(FPaths::ToUtf8(RelativePath.generic_wstring()));
        }
        return FPaths::Normalize(FPaths::ToUtf8(NormalizedPath.generic_wstring()));
    }

    FString MakeLuaScriptReference(const FString& ScriptName)
    {
        if (ScriptName.empty())
        {
            return {};
        }

        fs::path ResolvedPath;
        if (TryResolveLuaScriptPath(ScriptName, ResolvedPath))
        {
            return MakeProjectRelativePath(ResolvedPath);
        }

        fs::path Requested = WithLuaExtension(fs::path(FPaths::ToWide(ScriptName)));
        if (Requested.is_absolute())
        {
            return MakeProjectRelativePath(Requested);
        }

        if (Requested.has_parent_path())
        {
            return FPaths::Normalize(FPaths::ToUtf8(Requested.generic_wstring()));
        }

        return FPaths::Normalize(FString("Asset/Script/") + FPaths::ToUtf8(Requested.filename().generic_wstring()));
    }

    FName MakeLuaScriptKey(const FString& ScriptName)
    {
        return FName(MakeLuaScriptReference(ScriptName));
    }

    FString NormalizeLuaPath(const FWString& Path)
    {
        FString Result = FPaths::ToUtf8(Path);
        std::replace(Result.begin(), Result.end(), '\\', '/');
        return Result;
    }

    fs::path WithLuaExtension(fs::path Path)
    {
        if (Path.extension() != ".lua")
        {
            Path += L".lua";
        }
        return Path;
    }

    TArray<fs::path> GetLuaScriptSearchDirs()
    {
        TArray<fs::path> Dirs;
        Dirs.push_back((fs::path(FPaths::RootDir()) / L"Asset" / L"Script").lexically_normal());
        return Dirs;
    }

    fs::path ResolveLuaScriptCreatePath(const FString& ScriptName)
    {
        fs::path Requested = WithLuaExtension(fs::path(FPaths::ToWide(ScriptName)));
        if (Requested.is_absolute())
        {
            return Requested.lexically_normal();
        }

        if (Requested.has_parent_path())
        {
            return (fs::path(FPaths::RootDir()) / Requested).lexically_normal();
        }

        return ((fs::path(FPaths::RootDir()) / L"Asset" / L"Script") / Requested.filename()).lexically_normal();
    }

    bool TryResolveLuaScriptPath(const FString& ScriptName, fs::path& OutPath)
    {
        if (ScriptName.empty())
        {
            return false;
        }

        fs::path Requested = WithLuaExtension(fs::path(FPaths::ToWide(ScriptName)));
        TArray<fs::path> Candidates;

        if (Requested.is_absolute())
        {
            Candidates.push_back(Requested.lexically_normal());
        }
        else
        {
            if (Requested.has_parent_path())
            {
                Candidates.push_back((fs::path(FPaths::RootDir()) / Requested).lexically_normal());
            }

            for (const fs::path& ScriptDir : GetLuaScriptSearchDirs())
            {
                Candidates.push_back((ScriptDir / Requested).lexically_normal());
                if (Requested.has_parent_path())
                {
                    Candidates.push_back((ScriptDir / Requested.filename()).lexically_normal());
                }
            }
        }

        for (const fs::path& Candidate : Candidates)
        {
            if (fs::exists(Candidate) && fs::is_regular_file(Candidate))
            {
                OutPath = Candidate;
                return true;
            }
        }

        if (!Candidates.empty())
        {
            OutPath = Candidates.front();
        }
        return false;
    }

    bool ReadLuaScriptFile(const FString& ScriptPath, FString& OutSource)
    {
        std::ifstream File(fs::path(FPaths::ToWide(ScriptPath)), std::ios::binary);
        if (!File.is_open())
        {
            return false;
        }

        std::ostringstream Stream;
        Stream << File.rdbuf();
        OutSource = Stream.str();
        return true;
    }

    int LuaWidePathRequireLoader(lua_State* L)
    {
        const char* ModuleNameRaw = luaL_checkstring(L, 1);
        FString ModulePath = ModuleNameRaw ? ModuleNameRaw : "";
        if (ModulePath.empty())
        {
            lua_pushliteral(L, "\n\tinvalid empty module name");
            return 1;
        }

        std::replace(ModulePath.begin(), ModulePath.end(), '.', '/');
        std::replace(ModulePath.begin(), ModulePath.end(), '\\', '/');

        fs::path Requested = fs::path(FPaths::ToWide(ModulePath + ".lua"));
        FString TriedPaths;

        TArray<fs::path> SearchRoots;
        SearchRoots.push_back(fs::path(FPaths::RootDir()).lexically_normal());
        for (const fs::path& ScriptDir : GetLuaScriptSearchDirs())
        {
            SearchRoots.push_back(ScriptDir);
        }

        for (const fs::path& ScriptDir : SearchRoots)
        {
            const fs::path Candidate = (ScriptDir / Requested).lexically_normal();
            TriedPaths += "\n\tno file '" + FPaths::ToUtf8(Candidate.generic_wstring()) + "'";

            if (!fs::exists(Candidate) || !fs::is_regular_file(Candidate))
            {
                continue;
            }

            std::ifstream File(Candidate, std::ios::binary);
            if (!File.is_open())
            {
                continue;
            }

            std::ostringstream Stream;
            Stream << File.rdbuf();
            const FString Source = Stream.str();
            const FString ChunkName = "@" + FPaths::ToUtf8(Candidate.generic_wstring());

            const int Status = luaL_loadbuffer(L, Source.data(), Source.size(), ChunkName.c_str());
            if (Status != 0)
            {
                return lua_error(L);
            }

            return 1;
        }

        lua_pushlstring(L, TriedPaths.data(), TriedPaths.size());
        return 1;
    }

    void ClearLuaRequireCache(sol::state& Lua)
    {
        sol::protected_function_result Result = Lua.safe_script(R"(
            for key, _ in pairs(package.loaded) do
                if type(key) == "string" and string.sub(key, 1, 5) == "Game." then
                    package.loaded[key] = nil
                end
            end
        )");

        if (!Result.valid())
        {
            sol::error Err = Result;
            UE_LOG_WARNING("[ScriptManager] Failed to clear Lua require cache: %s", Err.what());
        }
    }
}

void FScriptManager::initializeLuaState()
{
    if (GLuaState)
    {
        ShutdownLuaState();
    }

    GLuaState = std::make_unique<sol::state>();

    GLuaState->open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::coroutine,
        sol::lib::string,
        sol::lib::os,
        sol::lib::math,
        sol::lib::table,
        sol::lib::debug,
		sol::lib::jit
        //sol::lib::bit // 필요하면 bit32 대신 이것 사용
    );

    ConfigureLuaPackagePath();

    RefreshLuaScriptFiles();
    BindLuaState();

    sol::protected_function_result Result = GLuaState->safe_script(R"(
        if jit then
            jit.on()
            Log("[Lua] " .. _VERSION .. " / " .. jit.version)
        else
            Log("[Lua] " .. _VERSION .. " / LuaJIT not detected")
        end
    )");

    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG_ERROR("[ScriptManager] LuaJIT init check failed: %s", Err.what());
    }
}
void FScriptManager::ShutdownLuaState()
{
    if (!GLuaState)
    {
        return;
    }

    for (auto& Pair : ScriptArray)
    {
        FLuaScriptInfo& ScriptInfo = Pair.second;
        for (UScriptComponent* ScriptComponent : ScriptInfo.ScriptComponents)
        {
            if (ScriptComponent)
            {
                ScriptComponent->ReleaseLuaStateReferences();
            }
        }
        ScriptInfo.ScriptComponents.clear();
    }

    ScriptArray.clear();
    GLuaState.reset();
}

void FScriptManager::ResetLuaState()
{
    ShutdownLuaState();
    initializeLuaState();
}

void FScriptManager::ConfigureLuaPackagePath()
{
    if (!GLuaState)
    {
        return;
    }

    const FString AssetScriptDir = NormalizeLuaPath((fs::path(FPaths::RootDir()) / L"Asset" / L"Script" / L"").wstring());
    sol::table Package = (*GLuaState)["package"];
    const FString CurrentPath = Package["path"].get_or(FString());

    FString ExtraPath;
    ExtraPath += AssetScriptDir + "?.lua;";
    ExtraPath += AssetScriptDir + "?/init.lua;";

    if (CurrentPath.find(ExtraPath) == FString::npos)
    {
        Package["path"] = ExtraPath + CurrentPath;
    }

    GLuaState->set_function("__JSWidePathRequireLoader", &LuaWidePathRequireLoader);
    sol::protected_function_result LoaderResult = GLuaState->safe_script(R"(
        local loaders = package.searchers or package.loaders
        if loaders and not package.__js_wide_path_loader_installed then
            table.insert(loaders, 1, __JSWidePathRequireLoader)
            package.__js_wide_path_loader_installed = true
        end
    )");

    if (!LoaderResult.valid())
    {
        sol::error Err = LoaderResult;
        UE_LOG_WARNING("[ScriptManager] Failed to install wide-path Lua loader: %s", Err.what());
    }
}

void FScriptManager::BindLuaState()
{
    BindMathTypes();
    BindObjectTypes();
    BindComponentTypes();
    BindActorTypes();
    BindStaticMeshTypes();
    BindAnimationTypes();
    BindBillboardTypes();
    BindCameraTypes();
    BindPrimitiveTypes();
    BindDecalTypes();
    BindSkeletalMeshTypes();
    BindEngineAPI();

    GLuaState->set_function("Log", &Log);
}

void Log(const std::string& Msg)
{
    UE_LOG("[Lua] %s", Msg.c_str());
}

bool FScriptManager::CreateScript(const FName& LuaScriptName)
{
    FString ScriptName;
    if (!LuaScriptName.IsValid())
    {
        ScriptName = "Actor.lua";
    }
    else
    {
        ScriptName = LuaScriptName.ToString();

        if (!ScriptName.ends_with(".lua"))
        {
            ScriptName += ".lua";
        }
    }

    FWString TemplatePath = FPaths::LuaTemplatePath();
    fs::path ScriptPath = ResolveLuaScriptCreatePath(ScriptName);

    if (!fs::exists(TemplatePath))
    {
        MessageBoxW(nullptr, TemplatePath.c_str(), L"Path", MB_OK);
        UE_LOG_ERROR("[LuaManager] Template file does not exist: %s", TemplatePath.c_str());
        return false;
    }

    if (fs::exists(ScriptPath))
    {
        UE_LOG_WARNING("[LuaManager] Script file already exists: %s", ScriptPath.c_str());
        return false;
    }

    try
    {
        std::error_code CreateDirEc;
        fs::create_directories(ScriptPath.parent_path(), CreateDirEc);

        fs::copy_file(
            TemplatePath,
            ScriptPath,
            fs::copy_options::none);
    }
    catch (const fs::filesystem_error& e)
    {
        UE_LOG_ERROR("[LuaManager] Failed to copy script: %s", e.what());
        return false;
    }

    const FName ScriptKey = MakeLuaScriptKey(FPaths::ToUtf8(ScriptPath.wstring()));
    if (ScriptArray.find(ScriptKey) != ScriptArray.end())
    {
        ScriptArray[ScriptKey].ScriptPath = ScriptPath.wstring();
        ScriptArray[ScriptKey].LastWriteTime = fs::last_write_time(ScriptPath);
    }

    RefreshLuaScriptFiles();
    UE_LOG("[LuaManager] 스크립트 생성 완료: %s", FPaths::ToUtf8(ScriptPath.wstring()).c_str());
    return true;
}

bool FScriptManager::EditScript(const FName& LuaScriptName)
{
    if (!LuaScriptName.IsValid())
    {
        UE_LOG_WARNING("[LuaManager] No script selected.");
        return false;
    }

    FWString ScriptPath;
    const FName ScriptKey = MakeLuaScriptKey(LuaScriptName.ToString());
    if (ScriptArray.find(ScriptKey) != ScriptArray.end())
    {
        ScriptPath = ScriptArray[ScriptKey].ScriptPath;
    }
    else
    {
        FString ResolvedPath;
        if (!ResolveScriptPath(LuaScriptName.ToString(), ResolvedPath))
        {
            UE_LOG_WARNING("[LuaManager] Script file not found: %s", LuaScriptName.ToString().c_str());
            return false;
        }
        ScriptPath = FPaths::ToWide(ResolvedPath);
    }

    HINSTANCE InstanceHandle = ShellExecute(nullptr, L"open", ScriptPath.data(),
                                             nullptr, nullptr, SW_SHOWNORMAL);

    if ((INT_PTR)InstanceHandle <= 32)
    {
        MessageBoxW(NULL, ScriptPath.c_str(), L"Path", MB_OK | MB_ICONERROR);
        UE_LOG_ERROR("[LuaManager] Failed to open script file: %s", ScriptPath.c_str());
        return false;
    }
    return true;
}

bool FScriptManager::HasScript(const FName& name)
{
    auto It = ScriptArray.find(MakeLuaScriptKey(name.ToString()));

    if (It == ScriptArray.end())
    {
        return false;
    }
    return true;
}

bool FScriptManager::ResolveScriptPath(const FString& ScriptName, FString& OutPath)
{
    fs::path Path;
    if (!TryResolveLuaScriptPath(ScriptName, Path))
    {
        UE_LOG_WARNING("[ScriptManager] Script file not found: %s", FPaths::ToUtf8(Path.wstring()).c_str());
        return false;
    }

    OutPath = FPaths::ToUtf8(Path.wstring());
    return true;
}

void FScriptManager::HotReloadScripts()
{
    RefreshLuaScriptFiles();

    bool bAnyScriptChanged = false;
    for (auto& [ScriptKey, ScriptInfo] : ScriptArray)
    {
        if (!ScriptInfo.ScriptPath.empty() && fs::exists(ScriptInfo.ScriptPath))
        {
            auto LastWriteTime = fs::last_write_time(ScriptInfo.ScriptPath);
            if (LastWriteTime > ScriptInfo.LastWriteTime)
            {
                bAnyScriptChanged = true;
                ScriptInfo.LastWriteTime = LastWriteTime;
            }
        }
    }

    if (!bAnyScriptChanged)
    {
        return;
    }

    if (GLuaState)
    {
        ClearLuaRequireCache(*GLuaState);
    }

    TArray<UScriptComponent*> ReloadTargets;
    for (auto& [ScriptKey, ScriptInfo] : ScriptArray)
    {
        for (UScriptComponent* ScriptComponent : ScriptInfo.ScriptComponents)
        {
            if (!ScriptComponent)
            {
                continue;
            }

            if (std::find(ReloadTargets.begin(), ReloadTargets.end(), ScriptComponent) == ReloadTargets.end())
            {
                ReloadTargets.push_back(ScriptComponent);
            }
        }
    }

    UE_LOG("[ScriptManager] 핫 리로드 실행");
    for (UScriptComponent* ScriptComponent : ReloadTargets)
    {
        UE_LOG("[ScriptManager] 핫리로드 대상 %s", ScriptComponent->GetName().c_str());
        ScriptComponent->HotReloadScript();
    }
}

void FScriptManager::RefreshLuaScriptFiles()
{
    for (const fs::path& ScriptDir : GetLuaScriptSearchDirs())
    {
        if (!fs::exists(ScriptDir))
        {
            continue;
        }

        for (const auto& Entry : fs::recursive_directory_iterator(ScriptDir))
        {
            if (Entry.is_regular_file() && Entry.path().extension() == ".lua")
            {
                FString ScriptName = MakeProjectRelativePath(Entry.path());
                FName ScriptKey = MakeLuaScriptKey(ScriptName);

                FLuaScriptInfo& Info = ScriptArray[ScriptKey];
                Info.ScriptPath = Entry.path().wstring();
                if (Info.LastWriteTime == fs::file_time_type::min())
                {
                    Info.LastWriteTime = fs::last_write_time(Entry.path());
                }
            }
        }
    }
}

void FScriptManager::RegisterScriptComponents(const FString& name, UScriptComponent* ScriptComponent)
{
    if (!ScriptComponent)
    {
        return;
    }

    FName ScriptName = MakeLuaScriptKey(name);

    FLuaScriptInfo& ScriptInfo = ScriptArray[ScriptName];

    if (ScriptInfo.ScriptPath.empty())
    {
        fs::path Path;
        if (TryResolveLuaScriptPath(name, Path))
        {
            ScriptInfo.ScriptPath = Path.wstring();
        }
    }

    auto& Components = ScriptInfo.ScriptComponents;

    auto It = std::find(
        Components.begin(),
        Components.end(),
        ScriptComponent);

    if (It == Components.end())
    {
        Components.push_back(ScriptComponent);
    }
}

void FScriptManager::UnregisterScriptComponents(const FString& name, UScriptComponent* ScriptComponent)
{
    if (!ScriptComponent)
    {
        return;
    }

    FName ScriptName = MakeLuaScriptKey(name);
    auto It = ScriptArray.find(ScriptName);
    if (It == ScriptArray.end())
    {
        return;
    }

    auto& Components = It->second.ScriptComponents;
    Components.erase(
        std::remove(Components.begin(), Components.end(), ScriptComponent),
        Components.end());
}

void FScriptManager::UnregisterScriptComponentAll(UScriptComponent* ScriptComponent)
{
    if (!ScriptComponent)
    {
        return;
    }

    for (auto It = ScriptArray.begin(); It != ScriptArray.end();)
    {
        auto& Components = It->second.ScriptComponents;

        Components.erase(
            std::remove(Components.begin(), Components.end(), ScriptComponent),
            Components.end());

        ++It;
    }
}

std::optional<FLuaScriptLoadResult> FScriptManager::LoadScriptClass(
    UScriptComponent* Component,
    const FString& ScriptName)
{
    FString ScriptPath;
    if (!ResolveScriptPath(ScriptName, ScriptPath))
    {
        UE_LOG_WARNING("[ScriptManager] Script not found: %s", ScriptName.c_str());
        return std::nullopt;
    }

    ClearLuaRequireCache(*GLuaState);

    sol::environment Env(*GLuaState, sol::create, GLuaState->globals());

    Env["Component"] = Component;
    Env["Actor"] = Component ? Component->GetOwner() : nullptr;
    Env["Owner"] = Component ? Component->GetOwner() : nullptr;

    FString ScriptSource;
    if (!ReadLuaScriptFile(ScriptPath, ScriptSource))
    {
        UE_LOG_ERROR("[ScriptManager] Failed to read script file: %s", ScriptPath.c_str());
        return std::nullopt;
    }

    sol::protected_function_result Result =
        GLuaState->safe_script(ScriptSource, Env);

    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG_ERROR("[ScriptManager] Lua load error: %s", Err.what());
        return std::nullopt;
    }

    sol::object ReturnObj = Result;
    if (!ReturnObj.valid() || ReturnObj.get_type() != sol::type::table)
    {
        UE_LOG_ERROR("[ScriptManager] Script must return table: %s", ScriptName.c_str());
        return std::nullopt;
    }

    FLuaScriptLoadResult Loaded;
    Loaded.Env = std::move(Env);
    Loaded.ScriptClass = ReturnObj.as<sol::table>();
    return Loaded;
}

std::optional<sol::table> FScriptManager::LoadScriptClassForProperties(
    const FString& ScriptName)
{
    if (!GLuaState)
    {
        return std::nullopt;
    }

    FString ScriptPath;
    if (!ResolveScriptPath(ScriptName, ScriptPath))
    {
        return std::nullopt;
    }

    ClearLuaRequireCache(*GLuaState);

    sol::environment TempEnv(*GLuaState, sol::create, GLuaState->globals());

    FString ScriptSource;
    if (!ReadLuaScriptFile(ScriptPath, ScriptSource))
    {
        UE_LOG_WARNING("[ScriptManager] Failed to read script file for properties: %s", ScriptPath.c_str());
        return std::nullopt;
    }

    sol::protected_function_result Result =
        GLuaState->safe_script(ScriptSource, TempEnv);

    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG_WARNING("[ScriptManager] Lua property load error: %s", Err.what());
        return std::nullopt;
    }

    sol::object ReturnObj = Result;
    if (!ReturnObj.valid() || ReturnObj.get_type() != sol::type::table)
    {
        return std::nullopt;
    }

    return ReturnObj.as<sol::table>();
}

FWString FScriptManager::GetScriptPathByName(const FName& name)
{
    auto Info = GetScriptInfo(name);
    return Info ? Info->ScriptPath : FWString();
}

auto FScriptManager::GetScriptInfo(const FName& name) -> FLuaScriptInfo*
{
    auto It = ScriptArray.find(MakeLuaScriptKey(name.ToString()));

    if (It == ScriptArray.end())
    {
        return nullptr;
    }

    return &It->second;
}
