#include "LuaScriptManager.h"

#include "Asset/AssetRegistry.h"
#include "Audio/AudioManager.h"
#include "CameraShake/CameraShakeAsset.h"
#include "CameraShake/CameraShakeManager.h"
#include "Component/Camera/CameraComponent.h"
#include "Component/Primitive/BillboardComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Core/Logging/Log.h"
#include "Diagnostics/ActorSequenceDiagnostics.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Actor/SniperKillCamDirector.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "GameFramework/Camera/SequenceCameraShake.h"
#include "GameFramework/Camera/WaveOscillatorCameraShake.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/World.h"
#include "Input/InputKeyCodes.h"
#include "Input/InputSystem.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialManager.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Static/StaticMesh.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemManager.h"
#include "Profiling/Time/Timer.h"
#include "Runtime/Engine.h"
#include "SimpleJSON/json.hpp"
#include "Texture/Texture2D.h"
#include "Platform/Paths.h"
#include "Platform/WindowsWindow.h"
#include "UI/CursorSystem.h"
#include "UI/UIManager.h"
#include "Viewport/GameViewportClient.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <initializer_list>
#include <random>

namespace
{
    bool LuaReadNumber(const sol::object& Object, double& OutValue)
    {
        if (!Object.valid() || Object == sol::nil || Object.get_type() != sol::type::number)
        {
            return false;
        }

        OutValue = Object.as<double>();
        return true;
    }

    bool LuaReadFloatField(const sol::table& Table, const char* Name, int Index, float& OutValue)
    {
        double Number = 0.0;

        sol::object Named = Table[Name];
        if (LuaReadNumber(Named, Number))
        {
            OutValue = static_cast<float>(Number);
            return true;
        }

        sol::object Indexed = Table[Index];
        if (LuaReadNumber(Indexed, Number))
        {
            OutValue = static_cast<float>(Number);
            return true;
        }

        return false;
    }

    bool LuaObjectToVector(const sol::object& Object, FVector& OutVector)
    {
        if (!Object.valid() || Object == sol::nil)
        {
            return false;
        }

        if (Object.is<FVector>())
        {
            OutVector = Object.as<FVector>();
            return true;
        }

        if (Object.get_type() != sol::type::table)
        {
            return false;
        }

        sol::table Table = Object.as<sol::table>();
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        LuaReadFloatField(Table, "X", 1, X);
        LuaReadFloatField(Table, "Y", 2, Y);
        LuaReadFloatField(Table, "Z", 3, Z);
        OutVector = FVector(X, Y, Z);
        return true;
    }

    bool LuaObjectToVector4(const sol::object& Object, FVector4& OutVector)
    {
        if (!Object.valid() || Object == sol::nil)
        {
            return false;
        }

        if (Object.is<FVector4>())
        {
            OutVector = Object.as<FVector4>();
            return true;
        }

        if (Object.get_type() != sol::type::table)
        {
            return false;
        }

        sol::table Table = Object.as<sol::table>();
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float W = 0.0f;
        LuaReadFloatField(Table, "X", 1, X);
        LuaReadFloatField(Table, "Y", 2, Y);
        LuaReadFloatField(Table, "Z", 3, Z);
        if (!LuaReadFloatField(Table, "W", 4, W))
        {
            LuaReadFloatField(Table, "A", 4, W);
        }
        OutVector = FVector4(X, Y, Z, W);
        return true;
    }
}
namespace
{
    UGameViewportClient* GetLuaGameViewportClient()
    {
        return GEngine ? GEngine->GetGameViewportClient() : nullptr;
    }

    void AppendUtf8Codepoint(FString& Out, uint32_t Codepoint)
    {
        if (Codepoint <= 0x7F)
        {
            Out.push_back(static_cast<char>(Codepoint));
        }
        else if (Codepoint <= 0x7FF)
        {
            Out.push_back(static_cast<char>(0xC0 | ((Codepoint >> 6) & 0x1F)));
            Out.push_back(static_cast<char>(0x80 | (Codepoint & 0x3F)));
        }
        else if (Codepoint <= 0xFFFF)
        {
            Out.push_back(static_cast<char>(0xE0 | ((Codepoint >> 12) & 0x0F)));
            Out.push_back(static_cast<char>(0x80 | ((Codepoint >> 6) & 0x3F)));
            Out.push_back(static_cast<char>(0x80 | (Codepoint & 0x3F)));
        }
        else if (Codepoint <= 0x10FFFF)
        {
            Out.push_back(static_cast<char>(0xF0 | ((Codepoint >> 18) & 0x07)));
            Out.push_back(static_cast<char>(0x80 | ((Codepoint >> 12) & 0x3F)));
            Out.push_back(static_cast<char>(0x80 | ((Codepoint >> 6) & 0x3F)));
            Out.push_back(static_cast<char>(0x80 | (Codepoint & 0x3F)));
        }
    }

    FString CodepointsToUtf8(const TArray<uint32_t>& Codepoints)
    {
        FString Result;
        for (size_t Index = 0; Index < Codepoints.size(); ++Index)
        {
            uint32_t Codepoint = Codepoints[Index];
            if (Codepoint >= 0xD800 && Codepoint <= 0xDBFF && Index + 1 < Codepoints.size())
            {
                const uint32_t Low = Codepoints[Index + 1];
                if (Low >= 0xDC00 && Low <= 0xDFFF)
                {
                    Codepoint = 0x10000 + ((Codepoint - 0xD800) << 10) + (Low - 0xDC00);
                    ++Index;
                }
            }

            if (Codepoint >= 0xD800 && Codepoint <= 0xDFFF)
            {
                Codepoint = 0xFFFD;
            }
            AppendUtf8Codepoint(Result, Codepoint);
        }
        return Result;
    }

    bool CanLuaConsumeTextInput()
    {
        InputSystem& Input = InputSystem::Get();
        if (Input.IsGuiUsingTextInput())
        {
            return false;
        }

        if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
        {
            if (GameViewportClient->GetInputMode() == EGameInputMode::UIOnly)
            {
                return false;
            }
        }

        const FUIInputCaptureState UIState = UUIManager::Get().GetViewportInputCaptureState();
        return !UIState.bWantsTextInput &&
            !UIState.bBlocksGameInput &&
            !UIState.bBlocksGameKeyboard &&
            !UIState.bConsumedKeyboardThisFrame &&
            !UIState.bConsumedTextInputThisFrame;
    }

    FString ConsumeLuaTextInput()
    {
        InputSystem& Input = InputSystem::Get();
        if (!CanLuaConsumeTextInput())
        {
            Input.ConsumeScriptTextInput();
            return {};
        }

        return CodepointsToUtf8(Input.ConsumeScriptTextInput());
    }

    FString LuaArgsToMessage(sol::variadic_args Args)
    {
        FString Message;

        for (auto Arg : Args)
        {
            if (!Message.empty())
            {
                Message += "\t";
            }

            Message += Arg.as<FString>();
        }

        return Message;
    }

    void AddLuaTagObject(const sol::object& Value, TArray<FName>& OutTags)
    {
        if (!Value.valid() || Value == sol::nil || Value.get_type() != sol::type::string)
        {
            return;
        }

        const FName Tag(Value.as<FString>());
        if (!Tag.IsValid() || std::find(OutTags.begin(), OutTags.end(), Tag) != OutTags.end())
        {
            return;
        }

        OutTags.push_back(Tag);
    }

    TArray<FName> LuaTagsFromArgs(sol::variadic_args Args)
    {
        TArray<FName> Tags;
        if (Args.size() == 1)
        {
            const sol::object FirstArg = Args[0];
            if (FirstArg.valid() && FirstArg.get_type() == sol::type::table)
            {
                const sol::table Table = FirstArg.as<sol::table>();
                for (const auto& Entry : Table)
                {
                    AddLuaTagObject(Entry.second, Tags);
                }
                return Tags;
            }
        }

        for (auto Arg : Args)
        {
            const sol::object Value = Arg;
            AddLuaTagObject(Value, Tags);
        }
        return Tags;
    }

    FString LuaTableStringAny(const sol::table& Table, std::initializer_list<const char*> Keys, const FString& DefaultValue = FString())
    {
        for (const char* Key : Keys)
        {
            sol::object Value = Table.get<sol::object>(Key);
            if (Value.valid() && Value != sol::nil && Value.get_type() == sol::type::string)
            {
                return Value.as<FString>();
            }
        }
        return DefaultValue;
    }

    float LuaTableFloatAny(const sol::table& Table, std::initializer_list<const char*> Keys, float DefaultValue)
    {
        for (const char* Key : Keys)
        {
            sol::object Value = Table.get<sol::object>(Key);
            if (Value.valid() && Value != sol::nil && Value.get_type() == sol::type::number)
            {
                return Value.as<float>();
            }
        }
        return DefaultValue;
    }

    sol::table ComponentsToLuaTable(sol::this_state State, const TArray<UActorComponent*>& Components)
    {
        sol::state_view L(State);
        sol::table Result = L.create_table();
        int Index = 1;
        for (UActorComponent* Component : Components)
        {
            if (IsValid(Component))
            {
                Result[Index++] = Component;
            }
        }
        return Result;
    }

    const char* LuaWorldTypeName()
    {
        if (!GEngine || !GEngine->GetWorld())
        {
            return "None";
        }

        switch (GEngine->GetWorld()->GetWorldType())
        {
        case EWorldType::Editor:
            return "Editor";
        case EWorldType::EditorPreview:
            return "EditorPreview";
        case EWorldType::PIE:
            return "PIE";
        case EWorldType::Game:
            return "Game";
        default:
            return "Unknown";
        }
    }

    bool IsLuaArrayTable(const sol::table& Table, int32& OutMaxIndex)
    {
        int32 Count = 0;
        int32 MaxIndex = 0;

        for (const auto& Pair : Table)
        {
            const sol::object Key = Pair.first;
            if (!Key.is<int32>())
            {
                return false;
            }

            const int32 Index = Key.as<int32>();
            if (Index < 1)
            {
                return false;
            }

            ++Count;
            MaxIndex = (std::max)(MaxIndex, Index);
        }

        OutMaxIndex = MaxIndex;
        return Count == MaxIndex;
    }

    json::JSON LuaObjectToJson(const sol::object& Object, int32 Depth = 0)
    {
        if (Depth > 64 || !Object.valid() || Object == sol::nil)
        {
            return json::JSON();
        }

        switch (Object.get_type())
        {
        case sol::type::boolean:
            return json::JSON(Object.as<bool>());
        case sol::type::number:
        {
            const double Value = Object.as<double>();
            if (std::isfinite(Value) && std::floor(Value) == Value)
            {
                return json::JSON(static_cast<long>(Value));
            }
            return json::JSON(Value);
        }
        case sol::type::string:
            return json::JSON(Object.as<FString>());
        case sol::type::table:
        {
            const sol::table Table = Object.as<sol::table>();
            int32 MaxIndex = 0;
            if (IsLuaArrayTable(Table, MaxIndex))
            {
                json::JSON Array = json::Array();
                for (int32 Index = 1; Index <= MaxIndex; ++Index)
                {
                    Array.append(LuaObjectToJson(Table.get<sol::object>(Index), Depth + 1));
                }
                return Array;
            }

            json::JSON JsonObject = json::Object();
            for (const auto& Pair : Table)
            {
                const sol::object Key = Pair.first;
                if (Key.is<FString>())
                {
                    JsonObject[Key.as<FString>()] = LuaObjectToJson(Pair.second, Depth + 1);
                }
            }
            return JsonObject;
        }
        default:
            return json::JSON();
        }
    }

    sol::object JsonToLuaObject(sol::state_view Lua, const json::JSON& Json)
    {
        switch (Json.JSONType())
        {
        case json::JSON::Class::Object:
        {
            sol::table Table = Lua.create_table();
            for (const auto& Pair : Json.ObjectRange())
            {
                Table[Pair.first] = JsonToLuaObject(Lua, Pair.second);
            }
            return sol::make_object(Lua, Table);
        }
        case json::JSON::Class::Array:
        {
            sol::table Table = Lua.create_table();
            int32 Index = 1;
            for (const json::JSON& Value : Json.ArrayRange())
            {
                Table[Index++] = JsonToLuaObject(Lua, Value);
            }
            return sol::make_object(Lua, Table);
        }
        case json::JSON::Class::String:
            return sol::make_object(Lua, Json.ToString());
        case json::JSON::Class::Floating:
            return sol::make_object(Lua, Json.ToFloat());
        case json::JSON::Class::Integral:
            return sol::make_object(Lua, static_cast<int32>(Json.ToInt()));
        case json::JSON::Class::Boolean:
            return sol::make_object(Lua, Json.ToBool());
        case json::JSON::Class::Null:
        default:
            return sol::make_object(Lua, sol::nil);
        }
    }

    bool ResolveSafeLuaSavePath(const FString& RelativePath, std::filesystem::path& OutPath)
    {
        if (RelativePath.empty())
        {
            return false;
        }

        std::filesystem::path RawPath(FPaths::ToWide(RelativePath));
        if (RawPath.is_absolute())
        {
            return false;
        }

        std::filesystem::path CleanRelative;
        bool bSkippedLeadingSaves = false;
        for (const std::filesystem::path& Part : RawPath.lexically_normal())
        {
            const std::wstring PartString = Part.wstring();
            if (PartString.empty() || PartString == L".")
            {
                continue;
            }
            if (PartString == L"..")
            {
                return false;
            }
            if (!bSkippedLeadingSaves && CleanRelative.empty() && (PartString == L"Saves" || PartString == L"saves"))
            {
                bSkippedLeadingSaves = true;
                continue;
            }

            CleanRelative /= Part;
        }

        if (CleanRelative.empty())
        {
            return false;
        }

        OutPath = (std::filesystem::path(FPaths::SaveDir()) / CleanRelative).lexically_normal();
        return true;
    }

    bool WriteLuaSaveText(const FString& RelativePath, const FString& Text)
    {
        std::filesystem::path SavePath;
        if (!ResolveSafeLuaSavePath(RelativePath, SavePath))
        {
            UE_LOG("[Lua.Save] Invalid save path: %s", RelativePath.c_str());
            return false;
        }

        std::error_code EC;
        std::filesystem::create_directories(SavePath.parent_path(), EC);
        if (EC)
        {
            UE_LOG("[Lua.Save] Failed to create save directory: %s", FPaths::ToUtf8(SavePath.parent_path().wstring()).c_str());
            return false;
        }

        std::ofstream File(SavePath, std::ios::binary | std::ios::trunc);
        if (!File.is_open())
        {
            UE_LOG("[Lua.Save] Failed to open save file for write: %s", FPaths::ToUtf8(SavePath.wstring()).c_str());
            return false;
        }

        File << Text;
        return File.good();
    }

    bool ReadLuaSaveText(const FString& RelativePath, FString& OutText)
    {
        std::filesystem::path SavePath;
        if (!ResolveSafeLuaSavePath(RelativePath, SavePath))
        {
            UE_LOG("[Lua.Save] Invalid save path: %s", RelativePath.c_str());
            return false;
        }

        std::ifstream File(SavePath, std::ios::binary);
        if (!File.is_open())
        {
            return false;
        }

        OutText.assign((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
        return true;
    }

    std::mt19937& GetLuaRandomGenerator()
    {
        static std::mt19937 Generator{ std::random_device{}() };
        return Generator;
    }

    sol::table AssetItemsToLuaTable(sol::state_view Lua, const TArray<FAssetListItem>& Items)
    {
        sol::table Result = Lua.create_table();
        int32 Index = 1;
        for (const FAssetListItem& Item : Items)
        {
            sol::table Entry = Lua.create_table();
            Entry["DisplayName"] = Item.DisplayName;
            Entry["FullPath"] = Item.FullPath;
            Entry["Name"] = Item.DisplayName;
            Entry["Path"] = Item.FullPath;
            Result[Index++] = Entry;
        }
        return Result;
    }

    sol::table AssetPathsToLuaTable(sol::state_view Lua, const TArray<FAssetListItem>& Items)
    {
        sol::table Result = Lua.create_table();
        int32 Index = 1;
        for (const FAssetListItem& Item : Items)
        {
            Result[Index++] = Item.FullPath;
        }
        return Result;
    }

    const FAssetListItem* FindLuaAssetItem(const FString& TypeName, const FString& NameOrPath)
    {
        if (TypeName.empty() || NameOrPath.empty())
        {
            return nullptr;
        }

        const TArray<FAssetListItem>& Items = FAssetRegistry::ListByTypeName(TypeName.c_str());
        for (const FAssetListItem& Item : Items)
        {
            if (Item.FullPath == NameOrPath || Item.DisplayName == NameOrPath)
            {
                return &Item;
            }
        }
        return nullptr;
    }

    bool ParseLuaInputMode(const FString& ModeName, EGameInputMode& OutMode)
    {
        FString Normalized = ModeName;
        std::transform(
            Normalized.begin(),
            Normalized.end(),
            Normalized.begin(),
            [](unsigned char Ch)
            {
                return static_cast<char>(std::tolower(Ch));
            }
        );

        if (Normalized == "gameonly" || Normalized == "game")
        {
            OutMode = EGameInputMode::GameOnly;
            return true;
        }
        if (Normalized == "gameandui" || Normalized == "gameui")
        {
            OutMode = EGameInputMode::GameAndUI;
            return true;
        }
        if (Normalized == "uionly" || Normalized == "ui")
        {
            OutMode = EGameInputMode::UIOnly;
            return true;
        }

        return false;
    }

    const char* ToLuaInputModeName(EGameInputMode Mode)
    {
        switch (Mode)
        {
        case EGameInputMode::GameOnly:
            return "GameOnly";
        case EGameInputMode::GameAndUI:
            return "GameAndUI";
        case EGameInputMode::UIOnly:
            return "UIOnly";
        default:
            return "Unknown";
        }
    }
}

void FLuaScriptManager::RegisterLuaHelpers(sol::state& Lua)
{
    // ?쒓? 寃쎈줈 ?명솚 ??safe_script_file ? ?대??곸쑝濡?fopen(UTF-8) ???곕?濡?ANSI ?댁꽍?먯꽌
    // 源⑥쭊?? wide ifstream ?쇰줈 吏곸젒 ?쎌뼱 safe_script(string) ?쇰줈 ?ㅽ뻾.
    FString Content;
    if (!ReadScriptFileContent("CoroutineManager.lua", Content))
    {
        UE_LOG("[Lua] Failed to load CoroutineManager.lua");
        return;
    }
    const FString                  ChunkName = ResolveScriptPath("CoroutineManager.lua");
    sol::protected_function_result Result    = Lua.safe_script(Content, sol::script_pass_on_error, ChunkName);
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("[Lua] CoroutineManager.lua error: %s", Err.what());
    }
}

void FLuaScriptManager::RegisterCoreBindings(sol::state& Lua)
{
    Lua.set_function(
        "print",
        [](sol::variadic_args Args)
        {
            const FString Message = LuaArgsToMessage(Args);
            UE_LOG("[Lua] %s", Message.c_str());
        }
    );

    sol::table Input = Lua.create_named_table("Input");
    Input.set_function(
        "GetKeyDown",
        sol::overload(
            [](const FString& KeyName)
            {
                return GetLuaInputSnapshot().WasPressed(ResolveInputKeyCode(KeyName));
            },
            [](int VK)
            {
                return GetLuaInputSnapshot().WasPressed(VK);
            }
        )
    );
    Input.set_function(
        "GetKey",
        sol::overload(
            [](const FString& KeyName)
            {
                return GetLuaInputSnapshot().IsDown(ResolveInputKeyCode(KeyName));
            },
            [](int VK)
            {
                return GetLuaInputSnapshot().IsDown(VK);
            }
        )
    );
    Input.set_function(
        "GetKeyUp",
        sol::overload(
            [](const FString& KeyName)
            {
                return GetLuaInputSnapshot().WasReleased(ResolveInputKeyCode(KeyName));
            },
            [](int VK)
            {
                return GetLuaInputSnapshot().WasReleased(VK);
            }
        )
    );
    Input.set_function(
        "GetRawKeyDown",
        sol::overload(
            [](const FString& KeyName)
            {
                return InputSystem::Get().GetKeyDown(ResolveInputKeyCode(KeyName));
            },
            [](int VK)
            {
                return InputSystem::Get().GetKeyDown(VK);
            }
        )
    );
    Input.set_function(
        "GetRawKey",
        sol::overload(
            [](const FString& KeyName)
            {
                return InputSystem::Get().GetKey(ResolveInputKeyCode(KeyName));
            },
            [](int VK)
            {
                return InputSystem::Get().GetKey(VK);
            }
        )
    );
    Input.set_function(
        "GetRawKeyUp",
        sol::overload(
            [](const FString& KeyName)
            {
                return InputSystem::Get().GetKeyUp(ResolveInputKeyCode(KeyName));
            },
            [](int VK)
            {
                return InputSystem::Get().GetKeyUp(VK);
            }
        )
    );
    Input.set_function(
        "GetMouseDeltaX",
        []()
        {
            return GetLuaInputSnapshot().MouseDeltaX;
        }
    );
    Input.set_function(
        "GetMouseDeltaY",
        []()
        {
            return GetLuaInputSnapshot().MouseDeltaY;
        }
    );
    Input.set_function(
        "IsGamepadConnected",
        []()
        {
            return GetLuaInputSnapshot().bGamepadConnected;
        }
    );
    Input.set_function(
        "GetLastInputDevice",
        []()
        {
            return InputSystem::Get().IsLastInputDeviceGamepad()
                ? FString("Gamepad")
                : FString("KeyboardMouse");
        }
    );
    Input.set_function(
        "WasConfirmPressed",
        []()
        {
            const FInputSystemSnapshot& Snapshot = GetLuaInputSnapshot();
            const FInputSystemSnapshot RawSnapshot = InputSystem::Get().MakeSnapshot();
            return Snapshot.WasPressed(VK_SPACE) ||
                Snapshot.WasGamepadButtonPressed(EGamepadButton::FaceBottom) ||
                RawSnapshot.WasPressed(VK_SPACE) ||
                RawSnapshot.WasGamepadButtonPressed(EGamepadButton::FaceBottom);
        }
    );
    Input.set_function(
        "WasPausePressed",
        []()
        {
            const FInputSystemSnapshot& Snapshot = GetLuaInputSnapshot();
            return Snapshot.WasPressed('Q') ||
                Snapshot.WasPressed(81) ||
                Snapshot.WasGamepadButtonPressed(EGamepadButton::Start);
        }
    );
    Input.set_function(
        "GetConfirmPromptLabel",
        []()
        {
            return InputSystem::Get().IsLastInputDeviceGamepad()
                ? FString("A")
                : FString("Space");
        }
    );
    Input.set_function(
        "GetPausePromptLabel",
        []()
        {
            return InputSystem::Get().IsLastInputDeviceGamepad()
                ? FString("Menu")
                : FString("Q");
        }
    );
    Input.set_function(
        "GetConfirmPromptText",
        [](const FString& ActionText)
        {
            const FString Label = InputSystem::Get().IsLastInputDeviceGamepad()
                ? FString("A")
                : FString("Space");
            if (ActionText.empty())
            {
                return Label;
            }

            return FString("Press ") + Label + " to " + ActionText;
        }
    );
    Input.set_function(
        "ConsumeTextInput",
        []()
        {
            return ConsumeLuaTextInput();
        }
    );
    Input.set_function(
        "SetInputMode",
        [](const FString& ModeName)
        {
            UGameViewportClient* GameViewportClient = GetLuaGameViewportClient();
            if (!GameViewportClient)
            {
                return false;
            }

            EGameInputMode InputMode = EGameInputMode::GameOnly;
            if (!ParseLuaInputMode(ModeName, InputMode))
            {
                return false;
            }

            GameViewportClient->SetInputMode(InputMode);
            return true;
        }
    );
    Input.set_function(
        "GetInputMode",
        []()
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                return FString(ToLuaInputModeName(GameViewportClient->GetInputMode()));
            }

            return FString("None");
        }
    );
    Input.set_function(
        "SetInputModeGameOnly",
        []()
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                GameViewportClient->SetInputMode(EGameInputMode::GameOnly);
                return true;
            }

            return false;
        }
    );
    Input.set_function(
        "SetInputModeGameAndUI",
        []()
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                GameViewportClient->SetInputMode(EGameInputMode::GameAndUI);
                return true;
            }

            return false;
        }
    );
    Input.set_function(
        "SetInputModeUIOnly",
        []()
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                GameViewportClient->SetInputMode(EGameInputMode::UIOnly);
                return true;
            }

            return false;
        }
    );
    Input.set_function(
        "SetCursorVisible",
        [](bool bVisible)
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                GameViewportClient->SetCursorVisible(bVisible);
                return true;
            }

            return false;
        }
    );
    Input.set_function(
        "SetCursorImage",
        [](const FString& TexturePath, float Width, float Height, float HotSpotX, float HotSpotY)
        {
            if (!GEngine)
            {
                return false;
            }

            UGameViewportClient* GameViewportClient = GetLuaGameViewportClient();
            ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
            if (!GameViewportClient || !Device)
            {
                return false;
            }

            return FCursorSystem::Get().SetCursorImage(TexturePath, Width, Height, HotSpotX, HotSpotY);
        }
    );
    Input.set_function(
        "ClearCursorImage",
        []()
        {
            if (!GEngine || !GetLuaGameViewportClient())
            {
                return false;
            }

            FCursorSystem::Get().ClearCursorImage();
            return true;
        }
    );
    Input.set_function(
        "IsCursorVisible",
        []()
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                return GameViewportClient->IsCursorVisible();
            }

            return false;
        }
    );
    Input.set_function(
        "SetCursorLocked",
        [](bool bLocked)
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                GameViewportClient->SetCursorLocked(bLocked);
                return true;
            }

            return false;
        }
    );
    Input.set_function(
        "IsCursorLocked",
        []()
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                return GameViewportClient->IsCursorLocked();
            }

            return false;
        }
    );
    Input.set_function(
        "SetMouseCaptured",
        [](bool bCaptured)
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                GameViewportClient->SetMouseCaptured(bCaptured);
                return true;
            }

            return false;
        }
    );
    Input.set_function(
        "SetMouseCapture",
        [](bool bCaptured)
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                GameViewportClient->SetMouseCaptured(bCaptured);
                return true;
            }

            return false;
        }
    );
    Input.set_function(
        "IsMouseCaptured",
        []()
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                return GameViewportClient->IsMouseCaptured();
            }

            return false;
        }
    );
    Input.set_function(
        "ReleaseMouseCapture",
        []()
        {
            if (UGameViewportClient* GameViewportClient = GetLuaGameViewportClient())
            {
                GameViewportClient->ReleaseMouseCapture();
                return true;
            }

            return false;
        }
    );

    // Engine ??寃뚯엫 ?쇱떆?뺤? / 醫낅즺.
    sol::table Json = Lua.create_named_table("Json");
    Json.set_function(
        "Encode",
        [](sol::object Value) -> FString
        {
            return LuaObjectToJson(Value).dump();
        }
    );
    Json.set_function(
        "Decode",
        [](sol::this_state State, const FString& Text) -> sol::object
        {
            if (Text.empty())
            {
                return sol::make_object(State, sol::nil);
            }

            sol::state_view L(State);
            json::JSON Data = json::JSON::Load(Text);
            return JsonToLuaObject(L, Data);
        }
    );

    sol::table Save = Lua.create_named_table("Save");
    Save.set_function(
        "WriteText",
        [](const FString& RelativePath, const FString& Text) -> bool
        {
            return WriteLuaSaveText(RelativePath, Text);
        }
    );
    Save.set_function(
        "ReadText",
        [](sol::this_state State, const FString& RelativePath) -> sol::object
        {
            FString Text;
            if (!ReadLuaSaveText(RelativePath, Text))
            {
                return sol::make_object(State, sol::nil);
            }
            return sol::make_object(State, Text);
        }
    );
    Save.set_function(
        "WriteJson",
        [](const FString& RelativePath, sol::object Value) -> bool
        {
            return WriteLuaSaveText(RelativePath, LuaObjectToJson(Value).dump());
        }
    );
    Save.set_function(
        "ReadJson",
        [](sol::this_state State, const FString& RelativePath) -> sol::object
        {
            FString Text;
            if (!ReadLuaSaveText(RelativePath, Text) || Text.empty())
            {
                return sol::make_object(State, sol::nil);
            }

            sol::state_view L(State);
            json::JSON Data = json::JSON::Load(Text);
            return JsonToLuaObject(L, Data);
        }
    );
    Save.set_function(
        "Exists",
        [](const FString& RelativePath) -> bool
        {
            std::filesystem::path SavePath;
            return ResolveSafeLuaSavePath(RelativePath, SavePath) && std::filesystem::exists(SavePath);
        }
    );
    Save.set_function(
        "Delete",
        [](const FString& RelativePath) -> bool
        {
            std::filesystem::path SavePath;
            if (!ResolveSafeLuaSavePath(RelativePath, SavePath))
            {
                UE_LOG("[Lua.Save] Invalid save path: %s", RelativePath.c_str());
                return false;
            }

            std::error_code EC;
            const bool bRemoved = std::filesystem::remove(SavePath, EC);
            return !EC && bRemoved;
        }
    );

    sol::table Random = Lua.create_named_table("Random");
    Random.set_function(
        "SetSeed",
        [](uint32 Seed)
        {
            GetLuaRandomGenerator().seed(static_cast<std::mt19937::result_type>(Seed));
        }
    );
    Random.set_function(
        "RandomFloat01",
        []() -> float
        {
            return std::uniform_real_distribution<float>(0.0f, 1.0f)(GetLuaRandomGenerator());
        }
    );
    Random.set_function(
        "RandomFloat",
        [](float Min, float Max) -> float
        {
            if (Min > Max)
            {
                std::swap(Min, Max);
            }
            return std::uniform_real_distribution<float>(Min, Max)(GetLuaRandomGenerator());
        }
    );
    Random.set_function(
        "RandomInt",
        [](int32 Min, int32 Max) -> int32
        {
            if (Min > Max)
            {
                std::swap(Min, Max);
            }
            return std::uniform_int_distribution<int32>(Min, Max)(GetLuaRandomGenerator());
        }
    );
    Random.set_function(
        "RandomBool",
        [](sol::optional<float> Probability) -> bool
        {
            const float P = (std::max)(0.0f, (std::min)(Probability.value_or(0.5f), 1.0f));
            return std::bernoulli_distribution(P)(GetLuaRandomGenerator());
        }
    );

    sol::table Asset = Lua.create_named_table("Asset");
    Asset.set_function(
        "List",
        [](sol::this_state State, const FString& TypeName) -> sol::table
        {
            sol::state_view L(State);
            return AssetItemsToLuaTable(L, FAssetRegistry::ListByTypeName(TypeName.c_str()));
        }
    );
    Asset.set_function(
        "GetPaths",
        [](sol::this_state State, const FString& TypeName) -> sol::table
        {
            sol::state_view L(State);
            return AssetPathsToLuaTable(L, FAssetRegistry::ListByTypeName(TypeName.c_str()));
        }
    );
    Asset.set_function(
        "Find",
        [](sol::this_state State, const FString& TypeName, const FString& NameOrPath) -> sol::object
        {
            sol::state_view L(State);
            if (const FAssetListItem* Item = FindLuaAssetItem(TypeName, NameOrPath))
            {
                return sol::make_object(L, Item->FullPath);
            }
            return sol::make_object(L, sol::nil);
        }
    );
    Asset.set_function(
        "Exists",
        [](const FString& TypeName, const FString& NameOrPath) -> bool
        {
            return FindLuaAssetItem(TypeName, NameOrPath) != nullptr;
        }
    );
    Asset.set_function(
        "GetTexturePaths",
        [](sol::this_state State) -> sol::table
        {
            sol::state_view L(State);
            return AssetPathsToLuaTable(L, FAssetRegistry::ListByTypeName("Texture"));
        }
    );
    Asset.set_function(
        "GetStaticMeshPaths",
        [](sol::this_state State) -> sol::table
        {
            sol::state_view L(State);
            return AssetPathsToLuaTable(L, FAssetRegistry::ListByTypeName("StaticMesh"));
        }
    );
    Asset.set_function(
        "GetSkeletalMeshPaths",
        [](sol::this_state State) -> sol::table
        {
            sol::state_view L(State);
            return AssetPathsToLuaTable(L, FAssetRegistry::ListByTypeName("SkeletalMesh"));
        }
    );
    Asset.set_function(
        "GetMaterialPaths",
        [](sol::this_state State) -> sol::table
        {
            sol::state_view L(State);
            return AssetPathsToLuaTable(L, FAssetRegistry::ListByTypeName("Material"));
        }
    );
    Asset.set_function(
        "GetAnimationPaths",
        [](sol::this_state State) -> sol::table
        {
            sol::state_view L(State);
            return AssetPathsToLuaTable(L, FAssetRegistry::ListByTypeName("UAnimSequence"));
        }
    );
    Asset.set_function(
        "GetParticleSystemPaths",
        [](sol::this_state State) -> sol::table
        {
            sol::state_view L(State);
            return AssetPathsToLuaTable(L, FAssetRegistry::ListByTypeName("ParticleSystem"));
        }
    );
    Asset.set_function(
        "GetLuaScriptPaths",
        [](sol::this_state State) -> sol::table
        {
            sol::state_view L(State);
            return AssetPathsToLuaTable(L, FAssetRegistry::ListByTypeName("LuaScript"));
        }
    );
    Asset.set_function(
        "GetRmlDocumentPaths",
        [](sol::this_state State) -> sol::table
        {
            sol::state_view L(State);
            return AssetPathsToLuaTable(L, FAssetRegistry::ListByTypeName("RmlDocument"));
        }
    );
    Asset.set_function(
        "GetSoundPaths",
        [](sol::this_state State) -> sol::table
        {
            sol::state_view L(State);
            return AssetPathsToLuaTable(L, FAssetRegistry::ListByTypeName("Sound"));
        }
    );

    sol::table Scene = Lua.create_named_table("Scene");
    Scene.set_function(
        "Open",
        [](const FString& PathOrName)
        {
            if (!GEngine || PathOrName.empty())
            {
                return false;
            }

            if (!GEngine->DoesRuntimeSceneExist(PathOrName))
            {
                UE_LOG("[Lua.Scene] Open rejected: scene file not found input=%s resolved=%s",
                    PathOrName.c_str(), GEngine->ResolveRuntimeScenePath(PathOrName).c_str());
                return false;
            }

            GEngine->RequestTransitionToScene(PathOrName);
            return true;
        }
    );
    Scene.set_function(
        "Load",
        [](const FString& PathOrName)
        {
            if (!GEngine || PathOrName.empty())
            {
                return false;
            }

            if (!GEngine->DoesRuntimeSceneExist(PathOrName))
            {
                UE_LOG("[Lua.Scene] Load rejected: scene file not found input=%s resolved=%s",
                    PathOrName.c_str(), GEngine->ResolveRuntimeScenePath(PathOrName).c_str());
                return false;
            }

            GEngine->RequestTransitionToScene(PathOrName);
            return true;
        }
    );
    Scene.set_function(
        "TransitionTo",
        [](const FString& PathOrName)
        {
            if (!GEngine || PathOrName.empty())
            {
                return false;
            }

            if (!GEngine->DoesRuntimeSceneExist(PathOrName))
            {
                UE_LOG("[Lua.Scene] TransitionTo rejected: scene file not found input=%s resolved=%s",
                    PathOrName.c_str(), GEngine->ResolveRuntimeScenePath(PathOrName).c_str());
                return false;
            }

            GEngine->RequestTransitionToScene(PathOrName);
            return true;
        }
    );
    Scene.set_function(
        "Reload",
        []()
        {
            if (!GEngine)
            {
                return false;
            }

            const FString CurrentPath = GEngine->GetCurrentScenePath();
            if (CurrentPath.empty())
            {
                return false;
            }

            GEngine->RequestTransitionToScene(CurrentPath);
            return true;
        }
    );
    Scene.set_function(
        "IsOpenPending",
        []()
        {
            return GEngine && GEngine->IsSceneTransitionPending();
        }
    );
    Scene.set_function(
        "GetCurrentPath",
        []() -> FString
        {
            return GEngine ? GEngine->GetCurrentScenePath() : FString {};
        }
    );
    Scene.set_function(
        "GetPendingPath",
        []() -> FString
        {
            return GEngine ? GEngine->GetPendingScenePath() : FString {};
        }
    );
    Scene.set_function(
        "ResolvePath",
        [](const FString& PathOrName) -> FString
        {
            return GEngine ? GEngine->ResolveRuntimeScenePath(PathOrName) : FString {};
        }
    );
    Scene.set_function(
        "Exists",
        [](const FString& PathOrName)
        {
            return GEngine && GEngine->DoesRuntimeSceneExist(PathOrName);
        }
    );
    Scene.set_function(
        "IsCurrent",
        [](const FString& PathOrName)
        {
            if (!GEngine || PathOrName.empty())
            {
                return false;
            }

            const FString Current = GEngine->GetCurrentScenePath();
            const FString Target = GEngine->ResolveRuntimeScenePath(PathOrName);
            if (Current.empty() || Target.empty())
            {
                return false;
            }

            std::error_code Ec;
            const std::filesystem::path CurrentPath(FPaths::ToWide(Current));
            const std::filesystem::path TargetPath(FPaths::ToWide(Target));
            if (std::filesystem::exists(CurrentPath, Ec) && std::filesystem::exists(TargetPath, Ec))
            {
                return std::filesystem::equivalent(CurrentPath, TargetPath, Ec);
            }

            return CurrentPath.lexically_normal() == TargetPath.lexically_normal();
        }
    );

    sol::table Application = Lua.create_named_table("Application");
    Application.set_function(
        "QuitGame",
        []()
        {
            PostQuitMessage(0);
        }
    );
    Application.set_function(
        "Exit",
        []()
        {
            PostQuitMessage(0);
        }
    );
    Application.set_function(
        "GetWorldType",
        []()
        {
            return LuaWorldTypeName();
        }
    );
    Application.set_function(
        "IsGame",
        []()
        {
            return GEngine && GEngine->GetWorld() && GEngine->GetWorld()->GetWorldType() == EWorldType::Game;
        }
    );
    Application.set_function(
        "IsEditor",
        []()
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return false;
            }

            const EWorldType Type = GEngine->GetWorld()->GetWorldType();
            return Type == EWorldType::Editor || Type == EWorldType::EditorPreview || Type == EWorldType::PIE;
        }
    );
    Application.set_function(
        "GetViewportSize",
        []() -> sol::table
        {
            sol::table Result = FLuaScriptManager::GetState().create_table();
            Result["Width"] = 0.0f;
            Result["Height"] = 0.0f;

            if (GEngine)
            {
                if (FWindowsWindow* Window = GEngine->GetWindow())
                {
                    Result["Width"] = Window->GetWidth();
                    Result["Height"] = Window->GetHeight();
                }
            }

            return Result;
        }
    );

    sol::table Debug = Lua.create_named_table("Debug");
    Debug.set_function(
        "Log",
        [](sol::variadic_args Args)
        {
            const FString Message = LuaArgsToMessage(Args);
            UE_LOG("[Lua][Log] %s", Message.c_str());
        }
    );
    Debug.set_function(
        "Warn",
        [](sol::variadic_args Args)
        {
            const FString Message = LuaArgsToMessage(Args);
            UE_LOG("[Lua][Warn] %s", Message.c_str());
        }
    );
    Debug.set_function(
        "Error",
        [](sol::variadic_args Args)
        {
            const FString Message = LuaArgsToMessage(Args);
            UE_LOG("[Lua][Error] %s", Message.c_str());
        }
    );
    Debug.set_function(
        "Assert",
        [](bool bCondition, sol::optional<FString> Message)
        {
            if (!bCondition)
            {
                UE_LOG("[Lua][Assert] %s", Message.value_or("Assertion failed").c_str());
            }
            return bCondition;
        }
    );
    Debug.set_function(
        "RunActorSequenceRoundTripSelfTest",
        [](sol::this_state State) -> sol::table
        {
            FActorSequenceRoundTripSelfTestResult TestResult =
                FActorSequenceDiagnostics::RunRoundTripSelfTest();

            sol::state_view L(State);
            sol::table Result = L.create_table();
            Result["Passed"] = TestResult.bPassed;
            Result["ChecksRun"] = TestResult.ChecksRun;
            Result["Message"] = TestResult.Message;
            UE_LOG(
                "[ActorSequenceDiagnostics] %s (%d checks): %s",
                TestResult.bPassed ? "PASS" : "FAIL",
                TestResult.ChecksRun,
                TestResult.Message.c_str());
            return Result;
        }
    );

    sol::table Engine = Lua.create_named_table("Engine");
    Engine["Json"] = Json;
    Engine["Save"] = Save;
    Engine["Random"] = Random;
    Engine["Asset"] = Asset;
    Engine["Scene"] = Scene;
    Engine["Application"] = Application;
    Engine["Debug"] = Debug;
    Engine.set_function(
        "PauseGame",
        []()
        {
            if (GEngine)
            {
                if (UWorld* World = GEngine->GetWorld())
                {
                    World->SetPaused(true);
                }
            }
        }
    );
    Engine.set_function(
        "ResumeGame",
        []()
        {
            if (GEngine)
            {
                if (UWorld* World = GEngine->GetWorld())
                {
                    World->SetPaused(false);
                }
            }
        }
    );
    Engine.set_function(
        "IsPaused",
        []()
        {
            if (GEngine)
            {
                if (UWorld* World = GEngine->GetWorld())
                {
                    return World->IsPaused();
                }
            }
            return false;
        }
    );
    Engine.set_function(
        "GetViewportSize",
        []() -> sol::table
        {
            sol::table Result = FLuaScriptManager::GetState().create_table();
            Result["Width"]   = 0.0f;
            Result["Height"]  = 0.0f;

            if (GEngine)
            {
                if (FWindowsWindow* Window = GEngine->GetWindow())
                {
                    Result["Width"]  = Window->GetWidth();
                    Result["Height"] = Window->GetHeight();
                }
            }

            return Result;
        }
    );
    Engine.set_function(
        "GetBallisticWindEnabled",
        []()
        {
            return GEngine && GEngine->GetWorld()
                ? GEngine->GetWorld()->GetWorldSettings().bEnableBallisticWind
                : true;
        }
    );
    Engine.set_function(
        "SetBallisticWindEnabled",
        [](bool bEnabled)
        {
            if (GEngine && GEngine->GetWorld())
            {
                GEngine->GetWorld()->GetWorldSettings().bEnableBallisticWind = bEnabled;
                GEngine->GetWorld()->RefreshBallisticWindRuntimeState();
            }
        }
    );
    Engine.set_function(
        "GetBallisticWindAcceleration",
        []()
        {
            return GEngine && GEngine->GetWorld()
                ? GEngine->GetWorld()->GetWorldSettings().BallisticWindAcceleration
                : FVector(0.0f, 1.5f, 0.0f);
        }
    );
    Engine.set_function(
        "GetCurrentBallisticWindAcceleration",
        []()
        {
            return GEngine && GEngine->GetWorld()
                ? GEngine->GetWorld()->GetCurrentBallisticWindAcceleration()
                : FVector(0.0f, 1.5f, 0.0f);
        }
    );
    Engine.set_function(
        "SetBallisticWindAcceleration",
        [](const FVector& WindAcceleration)
        {
            if (GEngine && GEngine->GetWorld())
            {
                GEngine->GetWorld()->SetBallisticWindAcceleration(WindAcceleration);
            }
        }
    );
    Engine.set_function(
        "SetClothWorldWindVelocity",
        [](const FVector& WindVelocity)
        {
            if (GEngine && GEngine->GetWorld())
            {
                GEngine->GetWorld()->SetClothWorldWindVelocity(WindVelocity);
            }
        }
    );
    Engine.set_function(
        "SetClothWorldWindVelocityXYZ",
        [](float X, float Y, float Z)
        {
            if (GEngine && GEngine->GetWorld())
            {
                GEngine->GetWorld()->SetClothWorldWindVelocity(FVector(X, Y, Z));
            }
        }
    );
    Engine.set_function(
        "GetClothWorldWindVelocity",
        []()
        {
            return GEngine && GEngine->GetWorld()
                ? GEngine->GetWorld()->GetClothWorldWindVelocityValue()
                : FVector::ZeroVector;
        }
    );
    Engine.set_function(
        "ClearClothWorldWindVelocity",
        []()
        {
            if (GEngine && GEngine->GetWorld())
            {
                GEngine->GetWorld()->ClearClothWorldWindVelocity();
            }
        }
    );
    Engine.set_function(
        "Exit",
        []()
        {
            // WM_QUIT ??FEngineLoop::Run ??PumpMessages ?먯꽌 ?↔퀬 ?뺤긽 shutdown.
            PostQuitMessage(0);
        }
    );
    Engine.set_function(
        "SetOnEscape",
        [](sol::protected_function Callback)
        {
            FLuaScriptManager::SetOnEscapePressed(std::move(Callback));
        }
    );

    sol::table Key = Lua.create_named_table("Key");
    for (const FString& KeyName : GetKnownInputKeyNames())
    {
        Key[KeyName.c_str()] = ResolveInputKeyCode(KeyName);
    }
    Key.set_function(
        "Resolve",
        [](const FString& KeyName)
        {
            return ResolveInputKeyCode(KeyName);
        }
    );
    Key.set_function(
        "Name",
        [](int32 KeyCode)
        {
            return GetInputKeyName(KeyCode);
        }
    );

    sol::table CameraManager = Lua.create_named_table("CameraManager");
    CameraManager.set_function(
        "ToggleActorCamera",
        [](const FString& ActorName, sol::optional<float> BlendTime)
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return false;
            }

            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            return Manager ? Manager->ToggleActiveCameraForActor(ActorName, BlendTime.value_or(0.0f)) : false;
        }
    );
    CameraManager.set_function(
        "ToggleOwnerCamera",
        [](AActor* Actor, sol::optional<float> BlendTime)
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return false;
            }

            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            return Manager ? Manager->ToggleActiveCameraForActor(Actor, BlendTime.value_or(0.0f)) : false;
        }
    );
    CameraManager.set_function(
        "PossessCamera",
        [](UCameraComponent* Camera)
        {
            if (!GEngine || !GEngine->GetWorld() || !IsValid(Camera))
            {
                return false;
            }

            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (!Manager)
            {
                return false;
            }

            Manager->SetActiveCamera(Camera);
            Manager->Possess(Camera);
            return true;
        }
    );
    CameraManager.set_function(
        "GetActiveCameraOwner",
        []() -> AActor*
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return nullptr;
            }
            APlayerController*    PC           = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager      = PC ? PC->GetPlayerCameraManager() : nullptr;
            UCameraComponent*     ActiveCamera = Manager ? Manager->GetActiveCamera() : nullptr;
            if (!IsValid(ActiveCamera)) return nullptr;
            AActor* Owner = ActiveCamera->GetOwner();
            return IsValid(Owner) ? Owner : nullptr;
        }
    );
    CameraManager.set_function(
        "GetActiveCamera",
        []() -> UCameraComponent*
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return nullptr;
            }
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            return Manager ? Manager->GetActiveCamera() : nullptr;
        }
    );
    CameraManager.set_function(
        "GetPossessedCamera",
        []() -> UCameraComponent*
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return nullptr;
            }
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            return Manager ? Manager->GetPossessedCamera() : nullptr;
        }
    );
    CameraManager.set_function(
        "GetPossessedCameraOwner",
        []() -> AActor*
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return nullptr;
            }
            APlayerController*    PC              = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager         = PC ? PC->GetPlayerCameraManager() : nullptr;
            UCameraComponent*     PossessedCamera = Manager ? Manager->GetPossessedCamera() : nullptr;
            if (!IsValid(PossessedCamera)) return nullptr;
            AActor* Owner = PossessedCamera->GetOwner();
            return IsValid(Owner) ? Owner : nullptr;
        }
    );
    CameraManager.set_function(
        "FadeOut",
        [](float Duration)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->StartCameraFade(0.0f, 1.0f, Duration, FLinearColor::Black(), false, true);
            }
        }
    );
    CameraManager.set_function(
        "FadeIn",
        [](float Duration)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->StartCameraFade(1.0f, 0.0f, Duration, FLinearColor::Black(), false, true);
            }
        }
    );
    CameraManager.set_function(
        "SetVignette",
        [](float Intensity, float Radius, float Softness)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetCameraVignette(Intensity, Radius, Softness, FLinearColor::Black());
            }
        }
    );
    CameraManager.set_function(
        "ClearVignette",
        []()
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->ClearCameraVignette();
            }
        }
    );
    CameraManager.set_function(
        "AddShockWave",
        [](float X, float Y, float Z, sol::optional<float> Duration, sol::optional<float> Radius, sol::optional<float> Strength)
        {
            if (!GEngine || !GEngine->GetWorld()) return 0;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            return Manager
                ? Manager->AddWorldShockWave(
                    FVector(X, Y, Z),
                    FVector::ForwardVector,
                    Duration.value_or(0.35f),
                    Radius.value_or(0.12f),
                    0.035f,
                    Strength.value_or(0.02f),
                    1.5f,
                    0.0f)
                : 0;
        }
    );
    CameraManager.set_function(
        "AddDirectedShockWave",
        [](
            float X,
            float Y,
            float Z,
            float DirX,
            float DirY,
            float DirZ,
            sol::optional<float> Duration,
            sol::optional<float> Radius,
            sol::optional<float> Width,
            sol::optional<float> Strength,
            sol::optional<float> Falloff,
            sol::optional<float> DirectionalStretch)
        {
            if (!GEngine || !GEngine->GetWorld()) return 0;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            FVector Direction(DirX, DirY, DirZ);
            if (Direction.IsNearlyZero())
            {
                Direction = FVector::ForwardVector;
            }
            else
            {
                Direction.Normalize();
            }
            return Manager
                ? Manager->AddWorldShockWave(
                    FVector(X, Y, Z),
                    Direction,
                    Duration.value_or(0.35f),
                    Radius.value_or(0.12f),
                    Width.value_or(0.035f),
                    Strength.value_or(0.02f),
                    Falloff.value_or(1.5f),
                    DirectionalStretch.value_or(0.0f))
                : 0;
        }
    );
    CameraManager.set_function(
        "UpdateShockWave",
        [](
            int32 Handle,
            float X,
            float Y,
            float Z,
            float DirX,
            float DirY,
            float DirZ,
            float Radius,
            float Width,
            float Strength,
            sol::optional<float> Falloff,
            sol::optional<float> DirectionalStretch)
        {
            if (!GEngine || !GEngine->GetWorld()) return false;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            FVector Direction(DirX, DirY, DirZ);
            if (Direction.IsNearlyZero())
            {
                Direction = FVector::ForwardVector;
            }
            else
            {
                Direction.Normalize();
            }
            return Manager
                ? Manager->UpdateWorldShockWave(
                    Handle,
                    FVector(X, Y, Z),
                    Direction,
                    Radius,
                    Width,
                    Strength,
                    Falloff.value_or(1.5f),
                    DirectionalStretch.value_or(0.0f))
                : false;
        }
    );
    CameraManager.set_function(
        "ClearShockWave",
        [](int32 Handle)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->ClearWorldShockWave(Handle);
            }
        }
    );
    CameraManager.set_function(
        "ClearShockWaves",
        []()
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->ClearAllWorldShockWaves();
            }
        }
    );
    CameraManager.set_function(
        "SetViewTargetWithBlend",
        [](AActor* Target, float BlendTime)
        {
            if (!GEngine || !GEngine->GetWorld() || !IsValid(Target)) return;

            APlayerController* PC = GEngine->GetWorld()->GetFirstPlayerController();
            if (PC)
            {
                PC->SetViewTargetWithBlend(Target, BlendTime);
            }
        }
    );
    // ActiveCamera 而댄룷?뚰듃 ?⑥쐞 blend ??媛숈? ?≫꽣 ??1?몄묶/3?몄묶 媛숈? 蹂꾧컻 移대찓??
    // 而댄룷?뚰듃 ?ъ씠 遺?쒕읇寃??꾪솚. BlendTime 誘몄?????0 (利됱떆 swap).
    CameraManager.set_function(
        "SetActiveCameraWithBlend",
        [](UCameraComponent* NewCamera, sol::optional<float> BlendTime)
        {
            if (!GEngine || !GEngine->GetWorld() || !IsValid(NewCamera)) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetActiveCameraWithBlend(NewCamera, BlendTime.value_or(0.0f));
            }
        }
    );
    // Sample wave-oscillator shake ??Lua console / ?ㅽ겕由쏀듃?먯꽌 利됱떆 ?붾뱾湲??뚯뒪?몄슜.
    // ?몄텧 ?? CameraManager.StartWaveShake(1.0)
    CameraManager.set_function(
        "StartWaveShake",
        [](sol::optional<float> Scale)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->StartCameraShake<UWaveOscillatorCameraShake>(Scale.value_or(1.0f));
            }
        }
    );
    CameraManager.set_function(
        "StartSequenceShake",
        [](sol::optional<float> Scale)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->StartCameraShake<USequenceCameraShake>(Scale.value_or(1.0f));
            }
        }
    );
    CameraManager.set_function(
        "StartCameraShakeAsset",
        [](const FString& AssetPath, sol::optional<float> Scale)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->StartCameraShakeAsset(AssetPath, Scale.value_or(1.0f));
            }
        }
    );
    CameraManager.set_function(
        "SetDepthOfField",
        [](float FocusDistance, float FocusRange, float MaxBlurRadius)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetDepthOfField(FocusDistance, FocusRange, MaxBlurRadius);
            }
        }
    );
    CameraManager.set_function(
        "SetBokeh",
        [](float RadiusThreshold, float LumaThreshold, float Intensity)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetBokeh(RadiusThreshold, LumaThreshold, Intensity);
            }
        }
    );
    CameraManager.set_function(
        "ClearDepthOfField",
        []()
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->ClearDepthOfField();
            }
        }
    );
    CameraManager.set_function(
        "SetScopeLens",
        [](float Radius, float OuterBlurRadius, float ZoomFOV, sol::optional<float> Feather, sol::optional<float> EdgeBlurRadius, sol::optional<float> Intensity, sol::optional<float> LookSensitivityScale, sol::optional<float> BlendTime, sol::optional<float> CenterX, sol::optional<float> CenterY, sol::optional<float> CenterOffsetX, sol::optional<float> CenterOffsetY)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetScopeLens(
                    Radius,
                    OuterBlurRadius,
                    ZoomFOV,
                    Feather.value_or(0.08f),
                    EdgeBlurRadius.value_or(1.25f),
                    Intensity.value_or(1.0f),
                    LookSensitivityScale.value_or(0.275f),
                    BlendTime.value_or(0.08f),
                    CenterX.value_or(0.5f),
                    CenterY.value_or(0.5f),
                    CenterOffsetX.value_or(0.0f),
                    CenterOffsetY.value_or(0.0f));
            }
        }
    );
    CameraManager.set_function(
        "SetScopeLensProfile",
        [](float Radius, float OuterBlurRadius, float ZoomFOV, sol::optional<float> Feather, sol::optional<float> EdgeBlurRadius, sol::optional<float> Intensity, sol::optional<float> LookSensitivityScale, sol::optional<float> BlendTime, sol::optional<float> CenterX, sol::optional<float> CenterY, sol::optional<float> CenterOffsetX, sol::optional<float> CenterOffsetY)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetScopeLensProfile(
                    Radius,
                    OuterBlurRadius,
                    ZoomFOV,
                    Feather.value_or(0.08f),
                    EdgeBlurRadius.value_or(1.25f),
                    Intensity.value_or(1.0f),
                    LookSensitivityScale.value_or(0.275f),
                    BlendTime.value_or(0.08f),
                    CenterX.value_or(0.5f),
                    CenterY.value_or(0.5f),
                    CenterOffsetX.value_or(0.0f),
                    CenterOffsetY.value_or(0.0f));
            }
        }
    );
    CameraManager.set_function(
        "SetScopeZoomEnabled",
        [](bool bEnabled)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetScopeZoomEnabled(bEnabled);
            }
        }
    );
    CameraManager.set_function(
        "ToggleScopeZoom",
        []()
        {
            if (!GEngine || !GEngine->GetWorld()) return false;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (!Manager) return false;
            const bool bEnabled = !Manager->IsScopeZoomEnabled();
            Manager->SetScopeZoomEnabled(bEnabled);
            return bEnabled;
        }
    );
    CameraManager.set_function(
        "IsScopeZoomEnabled",
        []()
        {
            if (!GEngine || !GEngine->GetWorld()) return false;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            return Manager ? Manager->IsScopeZoomEnabled() : false;
        }
    );
    CameraManager.set_function(
        "ClearScopeLens",
        []()
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->ClearScopeLens();
            }
        }
    );

    sol::table SniperKillCam = Lua.create_named_table("SniperKillCam");
    auto MakeSniperKillCamSnapshotObject = [](const FBulletCinematicSnapshot& Snapshot) -> sol::object
    {
        sol::state_view LuaState(FLuaScriptManager::GetState());
        sol::table Result = LuaState.create_table();
        Result["BulletId"] = Snapshot.BulletId;
        Result["Position"] = Snapshot.Position;
        Result["PreviousPosition"] = Snapshot.PreviousPosition;
        Result["Velocity"] = Snapshot.Velocity;
        Result["TraveledDistance"] = Snapshot.TraveledDistance;
        Result["TravelDistance"] = Snapshot.TraveledDistance;
        Result["LifeTime"] = Snapshot.LifeTime;
        Result["AmmoType"] = Snapshot.AmmoType;
        Result["Owner"] = Snapshot.Owner;
        Result["bIsAlive"] = Snapshot.bIsAlive;
        Result["bWasScopedShot"] = Snapshot.bWasScopedShot;
        return sol::make_object(LuaState, Result);
    };
    SniperKillCam.set_function(
        "ConsumePendingBulletId",
        []()
        {
            return ASniperKillCamDirector::ConsumePendingBulletId();
        }
    );
    SniperKillCam.set_function(
        "ClearPendingBullets",
        []()
        {
            ASniperKillCamDirector::ClearPendingBullets();
        }
    );
    SniperKillCam.set_function(
        "GetHitSnapshot",
        [MakeSniperKillCamSnapshotObject](int32 BulletId) -> sol::object
        {
            sol::state_view LuaState(FLuaScriptManager::GetState());
            FBulletCinematicSnapshot Snapshot;
            if (!ASniperKillCamDirector::GetHitSnapshotForBulletId(BulletId, Snapshot))
            {
                return sol::make_object(LuaState, sol::nil);
            }
            return MakeSniperKillCamSnapshotObject(Snapshot);
        }
    );
    SniperKillCam.set_function(
        "ConsumeFloorHit",
        [MakeSniperKillCamSnapshotObject](int32 BulletId) -> sol::object
        {
            FBulletCinematicSnapshot Snapshot;
            if (!ASniperKillCamDirector::ConsumeFloorHitForBulletId(BulletId, Snapshot))
            {
                return sol::make_object(FLuaScriptManager::GetState(), sol::nil);
            }
            return MakeSniperKillCamSnapshotObject(Snapshot);
        }
    );
    SniperKillCam.set_function(
        "CheckFloorHit",
        [MakeSniperKillCamSnapshotObject](int32 BulletId) -> sol::object
        {
            FBulletCinematicSnapshot Snapshot;
            if (!GEngine || !ASniperKillCamDirector::CheckFloorHitInWorld(GEngine->GetWorld(), BulletId, Snapshot))
            {
                return sol::make_object(FLuaScriptManager::GetState(), sol::nil);
            }
            return MakeSniperKillCamSnapshotObject(Snapshot);
        }
    );
    SniperKillCam.set_function(
        "GetHitLocation",
        [](int32 BulletId) -> sol::object
        {
            FBulletCinematicSnapshot Snapshot;
            if (!ASniperKillCamDirector::GetHitSnapshotForBulletId(BulletId, Snapshot))
            {
                return sol::make_object(FLuaScriptManager::GetState(), sol::nil);
            }
            return sol::make_object(FLuaScriptManager::GetState(), Snapshot.Position);
        }
    );
    SniperKillCam.set_function(
        "Start",
        [](int32 BulletId, sol::optional<float> Duration, sol::optional<int32> CameraMode)
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return false;
            }
            return ASniperKillCamDirector::StartForBulletIdInWorld(
                GEngine->GetWorld(),
                BulletId,
                Duration.value_or(10.0f),
                CameraMode.value_or(0));
        }
    );
    SniperKillCam.set_function(
        "Stop",
        []()
        {
            if (GEngine && GEngine->GetWorld())
            {
                ASniperKillCamDirector::StopInWorld(GEngine->GetWorld());
            }
        }
    );
    SniperKillCam.set_function(
        "IsPlaying",
        []()
        {
            return GEngine && GEngine->GetWorld()
                ? ASniperKillCamDirector::IsPlayingInWorld(GEngine->GetWorld())
                : false;
        }
    );
    auto SetSniperKillCamRigScalar = [](const FString& PropertyName, float Value)
    {
        return GEngine && GEngine->GetWorld()
            ? ASniperKillCamDirector::SetRailRigScalarInWorld(GEngine->GetWorld(), PropertyName, Value)
            : false;
    };
    auto GetSniperKillCamRigScalar = [](const FString& PropertyName, float Fallback)
    {
        return GEngine && GEngine->GetWorld()
            ? ASniperKillCamDirector::GetRailRigScalarInWorld(GEngine->GetWorld(), PropertyName, Fallback)
            : Fallback;
    };
    auto ApplyOptionalRigScalar = [SetSniperKillCamRigScalar](const sol::table& Options, const char* LuaName, const char* RigName)
    {
        const sol::object Value = Options.get<sol::object>(LuaName);
        if (!Value.valid() || Value.get_type() == sol::type::nil)
        {
            return false;
        }
        return SetSniperKillCamRigScalar(RigName, Value.as<float>());
    };
    auto ApplyOptionalRigBool = [SetSniperKillCamRigScalar](const sol::table& Options, const char* LuaName, const char* RigName)
    {
        const sol::object Value = Options.get<sol::object>(LuaName);
        if (!Value.valid() || Value.get_type() == sol::type::nil)
        {
            return false;
        }
        return SetSniperKillCamRigScalar(RigName, Value.as<bool>() ? 1.0f : 0.0f);
    };
    SniperKillCam.set_function(
        "SetRigScalar",
        [SetSniperKillCamRigScalar](const FString& PropertyName, float Value)
        {
            return SetSniperKillCamRigScalar(PropertyName, Value);
        }
    );
    SniperKillCam.set_function(
        "GetRigScalar",
        [GetSniperKillCamRigScalar](const FString& PropertyName, sol::optional<float> DefaultValue)
        {
            const float Fallback = DefaultValue.value_or(0.0f);
            return GetSniperKillCamRigScalar(PropertyName, Fallback);
        }
    );
    SniperKillCam.set_function(
        "SetRigScalars",
        [ApplyOptionalRigScalar](const sol::table& Values)
        {
            bool bAnyApplied = false;
            for (const auto& Pair : Values)
            {
                if (Pair.first.get_type() != sol::type::string || Pair.second.get_type() == sol::type::nil)
                {
                    continue;
                }
                const std::string Name = Pair.first.as<std::string>();
                if (!Name.empty())
                {
                    bAnyApplied = ApplyOptionalRigScalar(Values, Name.c_str(), Name.c_str()) || bAnyApplied;
                }
            }
            return bAnyApplied;
        }
    );
    SniperKillCam.set_function(
        "SetBulletSpin",
        [SetSniperKillCamRigScalar](float Revolutions, sol::optional<float> Phase)
        {
            bool bOk = SetSniperKillCamRigScalar("BulletSpinRevolutions", Revolutions);
            if (Phase.has_value())
            {
                bOk = SetSniperKillCamRigScalar("BulletSpinPhase", Phase.value()) && bOk;
            }
            return bOk;
        }
    );
    SniperKillCam.set_function(
        "ConfigureBullet",
        [ApplyOptionalRigScalar](const sol::table& Options)
        {
            bool bAnyApplied = false;
            bAnyApplied = ApplyOptionalRigScalar(Options, "forwardOffset", "BulletForwardOffset") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "sideOffset", "BulletSideOffset") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "upOffset", "BulletUpOffset") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "scale", "BulletScaleMultiplier") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "scaleX", "BulletScaleXMultiplier") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "scaleY", "BulletScaleYMultiplier") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "scaleZ", "BulletScaleZMultiplier") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "pitchOffset", "BulletPitchOffset") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "yawOffset", "BulletYawOffset") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "rollOffset", "BulletRollOffset") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "spinRevolutions", "BulletSpinRevolutions") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "spinPhase", "BulletSpinPhase") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "railAlphaOverride", "BulletRailAlphaOverride") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "railAlphaScale", "BulletRailAlphaScale") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "railAlphaOffset", "BulletRailAlphaOffset") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "railAlphaEase", "BulletRailAlphaEase") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "railAlphaPower", "BulletRailAlphaPower") || bAnyApplied;
            return bAnyApplied;
        }
    );
    SniperKillCam.set_function(
        "EnableShockWave",
        [SetSniperKillCamRigScalar](bool bEnabled)
        {
            return SetSniperKillCamRigScalar("bEnableShockWave", bEnabled ? 1.0f : 0.0f);
        }
    );
    SniperKillCam.set_function(
        "ConfigureShockWave",
        [ApplyOptionalRigScalar, ApplyOptionalRigBool](const sol::table& Options)
        {
            bool bAnyApplied = false;
            bAnyApplied = ApplyOptionalRigBool(Options, "enabled", "bEnableShockWave") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "forwardOffset", "ShockWaveForwardOffset") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "sideOffset", "ShockWaveSideOffset") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "upOffset", "ShockWaveUpOffset") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "radius", "ShockWaveRadius") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "startRadiusBoost", "ShockWaveStartRadiusBoost") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "width", "ShockWaveWidth") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "strength", "ShockWaveStrength") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "startStrengthBoost", "ShockWaveStartStrengthBoost") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "falloff", "ShockWaveFalloff") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "directionalStretch", "ShockWaveDirectionalStretch") || bAnyApplied;
            bAnyApplied = ApplyOptionalRigScalar(Options, "decay", "ShockWaveDecay") || bAnyApplied;
            return bAnyApplied;
        }
    );
    SniperKillCam.set_function(
        "SetScalar",
        [](const FString& PropertyName, float Value)
        {
            return GEngine && GEngine->GetWorld()
                ? ASniperKillCamDirector::SetKillCamScalarInWorld(GEngine->GetWorld(), PropertyName, Value)
                : false;
        }
    );
    SniperKillCam.set_function(
        "GetScalar",
        [](const FString& PropertyName, sol::optional<float> DefaultValue)
        {
            const float Fallback = DefaultValue.value_or(0.0f);
            return GEngine && GEngine->GetWorld()
                ? ASniperKillCamDirector::GetKillCamScalarInWorld(GEngine->GetWorld(), PropertyName, Fallback)
                : Fallback;
        }
    );
    SniperKillCam.set_function(
        "SetString",
        [](const FString& PropertyName, const FString& Value)
        {
            return GEngine && GEngine->GetWorld()
                ? ASniperKillCamDirector::SetKillCamStringInWorld(GEngine->GetWorld(), PropertyName, Value)
                : false;
        }
    );
    SniperKillCam.set_function(
        "GetString",
        [](const FString& PropertyName, sol::optional<FString> DefaultValue)
        {
            const FString Fallback = DefaultValue.value_or("");
            return GEngine && GEngine->GetWorld()
                ? ASniperKillCamDirector::GetKillCamStringInWorld(GEngine->GetWorld(), PropertyName, Fallback)
                : Fallback;
        }
    );
    SniperKillCam.set_function(
        "SetVector",
        [](const FString& PropertyName, const FVector& Value)
        {
            return GEngine && GEngine->GetWorld()
                ? ASniperKillCamDirector::SetKillCamVectorInWorld(GEngine->GetWorld(), PropertyName, Value)
                : false;
        }
    );
    SniperKillCam.set_function(
        "GetVector",
        [](const FString& PropertyName, sol::optional<FVector> DefaultValue)
        {
            const FVector Fallback = DefaultValue.value_or(FVector::ZeroVector);
            return GEngine && GEngine->GetWorld()
                ? ASniperKillCamDirector::GetKillCamVectorInWorld(GEngine->GetWorld(), PropertyName, Fallback)
                : Fallback;
        }
    );
    SniperKillCam.set_function(
        "SetRotator",
        [](const FString& PropertyName, const FVector& PitchYawRoll)
        {
            return GEngine && GEngine->GetWorld()
                ? ASniperKillCamDirector::SetKillCamRotatorInWorld(
                    GEngine->GetWorld(),
                    PropertyName,
                    FRotator(PitchYawRoll.X, PitchYawRoll.Y, PitchYawRoll.Z))
                : false;
        }
    );
    SniperKillCam.set_function(
        "GetRotator",
        [](const FString& PropertyName, sol::optional<FVector> DefaultValue)
        {
            const FVector Fallback = DefaultValue.value_or(FVector::ZeroVector);
            if (!GEngine || !GEngine->GetWorld())
            {
                return Fallback;
            }
            const FRotator Value = ASniperKillCamDirector::GetKillCamRotatorInWorld(
                GEngine->GetWorld(),
                PropertyName,
                FRotator(Fallback.X, Fallback.Y, Fallback.Z));
            return FVector(Value.Pitch, Value.Yaw, Value.Roll);
        }
    );

    sol::table AudioManager = Lua.create_named_table("AudioManager");
    AudioManager.set_function(
        "Load",
        [](const FString& SoundName, const FString& Path, sol::optional<bool> bLoop)
        {
            return FAudioManager::Get().LoadAudio(SoundName, Path, bLoop.value_or(false));
        }
    );
    AudioManager.set_function(
        "Play",
        [](const FString& SoundName, float Volume)
        {
            FAudioManager::Get().PlayAudio(SoundName, Volume);
        }
    );
    AudioManager.set_function(
        "PlaySFX",
        [](const FString& PathOrKey, sol::optional<float> VolumeScale)
        {
            return FAudioManager::Get().PlaySFX(PathOrKey, VolumeScale.value_or(1.0f));
        }
    );
    AudioManager.set_function(
        "PlaySFXHandle",
        [](const FString& PathOrKey, sol::optional<float> VolumeScale)
        {
            return FAudioManager::Get().PlaySFXHandle(PathOrKey, VolumeScale.value_or(1.0f));
        }
    );
    AudioManager.set_function(
        "FadeInSFX",
        [](FAudioHandle Handle, float DurationSeconds, sol::optional<float> TargetVolume)
        {
            return FAudioManager::Get().FadeInSFX(Handle, DurationSeconds, TargetVolume.value_or(1.0f));
        }
    );
    AudioManager.set_function(
        "FadeOutSFX",
        [](FAudioHandle Handle, float DurationSeconds)
        {
            return FAudioManager::Get().FadeOutSFX(Handle, DurationSeconds);
        }
    );
    AudioManager.set_function(
        "PlaySFX3D",
        [](const FString& PathOrKey, const FVector& Position, sol::optional<float> VolumeScale, sol::optional<float> MinDistance, sol::optional<float> MaxDistance)
        {
            return FAudioManager::Get().PlaySFX3D(
                PathOrKey,
                Position,
                VolumeScale.value_or(1.0f),
                MinDistance.value_or(1.0f),
                MaxDistance.value_or(10000.0f));
        }
    );
    AudioManager.set_function(
        "PlayBGM",
        [](const FString& SoundName, float Volume)
        {
            FAudioManager::Get().PlayBGM(SoundName, Volume);
        }
    );
    AudioManager.set_function(
        "StopBGM",
        []()
        {
            FAudioManager::Get().StopBGM();
        }
    );
    AudioManager.set_function(
        "FadeInBGM",
        [](float DurationSeconds, sol::optional<float> TargetVolume)
        {
            return FAudioManager::Get().FadeInBGM(DurationSeconds, TargetVolume.value_or(1.0f));
        }
    );
    AudioManager.set_function(
        "FadeOutBGM",
        [](float DurationSeconds)
        {
            return FAudioManager::Get().FadeOutBGM(DurationSeconds);
        }
    );
    AudioManager.set_function(
        "PlayLoop",
        [](const FString& SoundName, const FString& LoopName, sol::optional<float> Volume, sol::optional<float> Pitch)
        {
            FAudioManager::Get().PlayLoop(SoundName, LoopName, Volume.value_or(1.0f), Pitch.value_or(1.0f));
        }
    );
    AudioManager.set_function(
        "StopLoop",
        [](const FString& LoopName)
        {
            FAudioManager::Get().StopLoop(LoopName);
        }
    );
    AudioManager.set_function(
        "StopAllLoops",
        []()
        {
            FAudioManager::Get().StopAllLoops();
        }
    );
    AudioManager.set_function(
        "SetLoopVolume",
        [](const FString& LoopName, float Volume)
        {
            FAudioManager::Get().SetLoopVolume(LoopName, Volume);
        }
    );
    AudioManager.set_function(
        "SetLoopPitch",
        [](const FString& LoopName, float Pitch)
        {
            FAudioManager::Get().SetLoopPitch(LoopName, Pitch);
        }
    );
    AudioManager.set_function(
        "IsLoopPlaying",
        [](const FString& LoopName)
        {
            return FAudioManager::Get().IsLoopPlaying(LoopName);
        }
    );
    AudioManager.set_function(
        "SetMasterVolume",
        [](float Volume)
        {
            FAudioManager::Get().SetMasterVolume(Volume);
        }
    );
    AudioManager.set_function(
        "GetMasterVolume",
        []()
        {
            return FAudioManager::Get().GetMasterVolume();
        }
    );
    AudioManager.set_function(
        "SetBGMVolume",
        [](float Volume)
        {
            FAudioManager::Get().SetBGMVolume(Volume);
        }
    );
    AudioManager.set_function(
        "GetBGMVolume",
        []()
        {
            return FAudioManager::Get().GetBGMVolume();
        }
    );
    AudioManager.set_function(
        "SetSFXVolume",
        [](float Volume)
        {
            FAudioManager::Get().SetSFXVolume(Volume);
        }
    );
    AudioManager.set_function(
        "GetSFXVolume",
        []()
        {
            return FAudioManager::Get().GetSFXVolume();
        }
    );
    AudioManager.set_function(
        "SetListener",
        [](const FVector& Position, sol::optional<FVector> Forward, sol::optional<FVector> Up)
        {
            FAudioManager::Get().SetListener(
                Position,
                Forward.value_or(FVector::ForwardVector),
                Up.value_or(FVector::UpVector));
        }
    );
    AudioManager.set_function(
        "StopSound",
        [](FAudioHandle Handle)
        {
            FAudioManager::Get().StopSound(Handle);
        }
    );
    AudioManager.set_function(
        "StopAllSounds",
        []()
        {
            FAudioManager::Get().StopAllSounds();
        }
    );
    AudioManager.set_function(
        "IsSoundPlaying",
        [](FAudioHandle Handle)
        {
            return FAudioManager::Get().IsSoundPlaying(Handle);
        }
    );
    AudioManager.set_function(
        "SetSoundVolume",
        [](FAudioHandle Handle, float Volume)
        {
            FAudioManager::Get().SetSoundVolume(Handle, Volume);
        }
    );
    AudioManager.set_function(
        "SetSoundPitch",
        [](FAudioHandle Handle, float Pitch)
        {
            FAudioManager::Get().SetSoundPitch(Handle, Pitch);
        }
    );
    AudioManager.set_function(
        "SetSoundPosition",
        [](FAudioHandle Handle, const FVector& Position)
        {
            FAudioManager::Get().SetSoundPosition(Handle, Position);
        }
    );
    AudioManager.set_function(
        "FadeInSound",
        [](FAudioHandle Handle, float DurationSeconds, sol::optional<float> TargetVolume)
        {
            return FAudioManager::Get().FadeInSound(Handle, DurationSeconds, TargetVolume.value_or(1.0f));
        }
    );
    AudioManager.set_function(
        "FadeOutSound",
        [](FAudioHandle Handle, float DurationSeconds)
        {
            return FAudioManager::Get().FadeOutSound(Handle, DurationSeconds);
        }
    );
    AudioManager.set_function(
        "SetSFXPolicy",
        [](const FString& PathOrKey, sol::optional<int32> MaxConcurrent, sol::optional<float> CooldownSeconds, sol::optional<int32> Priority, sol::optional<bool> bStopOldest)
        {
            FAudioManager::Get().SetSFXPlaybackPolicy(
                PathOrKey,
                MaxConcurrent.value_or(0),
                CooldownSeconds.value_or(0.0f),
                Priority.value_or(0),
                bStopOldest.value_or(true));
        }
    );
    AudioManager.set_function(
        "ClearSFXPolicy",
        [](const FString& PathOrKey)
        {
            FAudioManager::Get().ClearSFXPlaybackPolicy(PathOrKey);
        }
    );
    AudioManager.set_function(
        "ClearAllSFXPolicies",
        []()
        {
            FAudioManager::Get().ClearAllSFXPlaybackPolicies();
        }
    );
    AudioManager.set_function(
        "GetActiveSoundCount",
        [](sol::optional<FString> PathOrKey)
        {
            return FAudioManager::Get().GetActiveSoundCount(PathOrKey.value_or(FString()));
        }
    );

    // Short alias for gameplay scripts.
    Lua["Audio"] = AudioManager;

    sol::table Time = Lua.create_named_table("Time");
    Time.set_function(
        "DeltaTime",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetDeltaTime() : 0.0f;
        }
    );
    Time.set_function(
        "RawDeltaTime",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetRawDeltaTime() : 0.0f;
        }
    );
    Time.set_function(
        "TotalTime",
        []() -> double
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetTotalTime() : 0.0;
        }
    );
    Time.set_function(
        "FPS",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetFPS() : 0.0f;
        }
    );
    Time.set_function(
        "DisplayFPS",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetDisplayFPS() : 0.0f;
        }
    );
    Time.set_function(
        "FrameTimeMs",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetFrameTimeMs() : 0.0f;
        }
    );
    Time.set_function(
        "GetTimeDilation",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetTimeDilation() : 1.0f;
        }
    );
    Time.set_function(
        "SetTimeDilation",
        [](float Dilation)
        {
            if (FTimer* T = GEngine ? GEngine->GetTimer() : nullptr) T->SetTimeDilation(Dilation);
        }
    );
    Time.set_function(
        "GetMaxFPS",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetMaxFPS() : 0.0f;
        }
    );
    Time.set_function(
        "SetMaxFPS",
        [](float FPS)
        {
            if (FTimer* T = GEngine ? GEngine->GetTimer() : nullptr) T->SetMaxFPS(FPS);
        }
    );


    sol::table Texture = Lua.create_named_table("Texture");
    Texture.set_function(
        "Load",
        [](const FString& Path, sol::optional<bool> bSRGB) -> UTexture2D*
        {
            return UTexture2D::LoadFromCached(Path, bSRGB.value_or(true) ? ETextureColorSpace::SRGB : ETextureColorSpace::Linear);
        }
    );

    sol::table StaticMeshLibrary = Lua.create_named_table("StaticMeshLibrary");
    Lua["StaticMeshes"] = StaticMeshLibrary;
    StaticMeshLibrary.set_function(
        "Load",
        [](const FString& Path) -> UStaticMesh*
        {
            if (!GEngine || Path.empty() || Path == "None") return nullptr;
            ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
            return FMeshManager::LoadStaticMesh(Path, Device);
        }
    );

    sol::table Material = Lua.create_named_table("Material");
    Lua["MaterialLibrary"] = Material;
    Lua["Materials"] = Material;
    Material.set_function(
        "Load",
        [](const FString& Path) -> UMaterial*
        {
            return FMaterialManager::Get().GetOrCreateMaterial(Path);
        }
    );
    Material.set_function(
        "GetOrCreate",
        [](const FString& Path) -> UMaterial*
        {
            return FMaterialManager::Get().GetOrCreateMaterial(Path);
        }
    );
    Material.set_function(
        "Create",
        [](const FString& Path) -> UMaterial*
        {
            return FMaterialManager::Get().CreateMaterialAsset(Path);
        }
    );
    Material.set_function(
        "CreateGraph",
        [](const FString& Path) -> UMaterial*
        {
            return FMaterialManager::Get().CreateGraphMaterialAsset(Path);
        }
    );
    Material.set_function(
        "GetComponentMaterial",
        [](UPrimitiveComponent* Component, int32 ElementIndex) -> UMaterial*
        {
            if (!IsValid(Component)) return nullptr;
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
            {
                return StaticMeshComponent->GetMaterial(ElementIndex);
            }
            if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
            {
                return SkinnedMeshComponent->GetMaterial(ElementIndex);
            }
            if (UBillboardComponent* BillboardComponent = Cast<UBillboardComponent>(Component))
            {
                return BillboardComponent->GetMaterial();
            }
            return nullptr;
        }
    );
    Material.set_function(
        "SetComponentMaterial",
        [](UPrimitiveComponent* Component, int32 ElementIndex, UMaterial* InMaterial) -> bool
        {
            if (!IsValid(Component) || !IsValid(InMaterial)) return false;
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
            {
                StaticMeshComponent->SetMaterial(ElementIndex, InMaterial);
                return true;
            }
            if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
            {
                SkinnedMeshComponent->SetMaterial(ElementIndex, InMaterial);
                return true;
            }
            if (UBillboardComponent* BillboardComponent = Cast<UBillboardComponent>(Component))
            {
                BillboardComponent->SetMaterial(InMaterial);
                return true;
            }
            return false;
        }
    );
    Material.set_function(
        "SetComponentMaterialByPath",
        [](UPrimitiveComponent* Component, int32 ElementIndex, const FString& MaterialPath) -> bool
        {
            if (!IsValid(Component)) return false;
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
            {
                return StaticMeshComponent->SetMaterialByPath(ElementIndex, MaterialPath);
            }
            if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
            {
                return SkinnedMeshComponent->SetMaterialByPath(ElementIndex, MaterialPath);
            }
            if (UBillboardComponent* BillboardComponent = Cast<UBillboardComponent>(Component))
            {
                BillboardComponent->SetMaterial(FMaterialManager::Get().GetOrCreateMaterial(MaterialPath));
                return true;
            }
            return false;
        }
    );
    Material.set_function(
        "CreateDynamicInstance",
        [](UMaterial* Parent, UObject* Owner, sol::optional<FString> DebugName) -> UMaterialInstanceDynamic*
        {
            return IsValid(Parent) ? UMaterialInstanceDynamic::Create(Parent, Owner, DebugName.value_or(FString())) : nullptr;
        }
    );
    Material.set_function(
        "CreateDynamicInstanceForComponent",
        [](UPrimitiveComponent* Component, int32 ElementIndex, sol::optional<FString> DebugName) -> UMaterialInstanceDynamic*
        {
            if (!IsValid(Component)) return nullptr;

            UMaterial* ParentMaterial = nullptr;
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
            {
                ParentMaterial = StaticMeshComponent->GetMaterial(ElementIndex);
                if (UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(ParentMaterial, Component, DebugName.value_or(FString())))
                {
                    StaticMeshComponent->SetMaterial(ElementIndex, Instance);
                    return Instance;
                }
                return nullptr;
            }
            if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
            {
                ParentMaterial = SkinnedMeshComponent->GetMaterial(ElementIndex);
                if (UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(ParentMaterial, Component, DebugName.value_or(FString())))
                {
                    SkinnedMeshComponent->SetMaterial(ElementIndex, Instance);
                    return Instance;
                }
                return nullptr;
            }
            return nullptr;
        }
    );
    Material.set_function(
        "Save",
        [](UMaterial* Mat, const FString& Path)
        {
            return IsValid(Mat) && FMaterialManager::Get().SaveMaterial(Mat, Path);
        }
    );
    Material.set_function(
        "SetShader",
        [](UMaterial* Mat, const FString& ShaderPath)
        {
            return IsValid(Mat) && FMaterialManager::Get().SetMaterialShader(Mat, ShaderPath);
        }
    );
    Material.set_function(
        "SetScalarParameter",
        [](UMaterial* Mat, const FString& ParamName, float Value) -> bool
        {
            return IsValid(Mat) && Mat->SetScalarParameter(ParamName, Value);
        }
    );
    Material.set_function(
        "SetVectorParameter",
        [](UMaterial* Mat, const FString& ParamName, const sol::object& Value) -> bool
        {
            if (!IsValid(Mat)) return false;
            FVector VectorValue;
            if (!LuaObjectToVector(Value, VectorValue)) return false;
            return Mat->SetVector3Parameter(ParamName, VectorValue);
        }
    );
    Material.set_function(
        "SetColorParameter",
        [](UMaterial* Mat, const FString& ParamName, const sol::object& Value) -> bool
        {
            if (!IsValid(Mat)) return false;
            FVector4 ColorValue;
            if (!LuaObjectToVector4(Value, ColorValue)) return false;
            return Mat->SetVector4Parameter(ParamName, ColorValue);
        }
    );
    Material.set_function(
        "SetTextureParameter",
        [](UMaterial* Mat, const FString& ParamName, UTexture2D* Texture) -> bool
        {
            return IsValid(Mat) && IsValid(Texture) && Mat->SetTextureParameter(ParamName, Texture);
        }
    );

    sol::table CameraShake = Lua.create_named_table("CameraShake");
    CameraShake.set_function(
        "Load",
        [](const FString& Path) -> UCameraShakeAsset*
        {
            return FCameraShakeManager::Get().Load(Path);
        }
    );
    CameraShake.set_function(
        "Find",
        [](const FString& Path) -> UCameraShakeAsset*
        {
            return FCameraShakeManager::Get().Find(Path);
        }
    );
    CameraShake.set_function(
        "Save",
        [](UCameraShakeAsset* Asset)
        {
            return IsValid(Asset) && FCameraShakeManager::Get().Save(Asset);
        }
    );

    Lua.set_function(
        "LoadAudio",
        [](const FString& SoundName, const FString& Path, sol::optional<bool> bLoop)
        {
            return FAudioManager::Get().LoadAudio(SoundName, Path, bLoop.value_or(false));
        }
    );
}

