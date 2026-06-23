#include "Runtime/Script/API/LuaEngineAPIBindings.h"

#include "Engine/Runtime/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    FString ToPx(float Value)
    {
        return std::to_string(Value) + "px";
    }

    FString ToCssColor(float R, float G, float B, float A)
    {
        const int32 RI = static_cast<int32>(std::clamp(R, 0.0f, 1.0f) * 255.0f);
        const int32 GI = static_cast<int32>(std::clamp(G, 0.0f, 1.0f) * 255.0f);
        const int32 BI = static_cast<int32>(std::clamp(B, 0.0f, 1.0f) * 255.0f);
        const float Alpha = std::clamp(A, 0.0f, 1.0f);

        char Buffer[96] = {};
        std::snprintf(Buffer, sizeof(Buffer), "rgba(%d,%d,%d,%.3f)", RI, GI, BI, Alpha);
        return FString(Buffer);
    }

    bool SetStyle(const FString& ElementId, const FString& Name, const FString& Value)
    {
        return GEngine ? GEngine->GetRmlUiSystem().SetElementStyle(ElementId, Name, Value) : false;
    }

    bool SetAttribute(const FString& ElementId, const FString& Name, const FString& Value)
    {
        return GEngine ? GEngine->GetRmlUiSystem().SetElementAttribute(ElementId, Name, Value) : false;
    }

    bool SetTransformStyles(const FString& ElementId, float X, float Y, float W, float H)
    {
        if (!GEngine)
        {
            return false;
        }

        bool bResult = GEngine->GetRmlUiSystem().SetElementStyle(ElementId, "position", "absolute");
        bResult = GEngine->GetRmlUiSystem().SetElementStyle(ElementId, "left", ToPx(X)) || bResult;
        bResult = GEngine->GetRmlUiSystem().SetElementStyle(ElementId, "top", ToPx(Y)) || bResult;
        bResult = GEngine->GetRmlUiSystem().SetElementStyle(ElementId, "width", ToPx(W)) || bResult;
        bResult = GEngine->GetRmlUiSystem().SetElementStyle(ElementId, "height", ToPx(H)) || bResult;
        return bResult;
    }
}

namespace FLuaEngineAPI
{
    void BindUI(sol::state& Lua, sol::table& API)
    {
        sol::table UI = Lua.create_table();

        UI["LoadDocument"] = [](const FString& ScreenId, const FString& Path) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().LoadDocument(ScreenId, Path) : false;
        };

        UI["UnloadDocument"] = [](const FString& ScreenId) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().UnloadDocument(ScreenId) : false;
        };

        UI["ReloadDocument"] = [](const FString& ScreenId) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().ReloadDocument(ScreenId) : false;
        };

        UI["ShowDocument"] = [](const FString& ScreenId) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().ShowScreen(ScreenId) : false;
        };

        UI["HideDocument"] = [](const FString& ScreenId) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().HideScreen(ScreenId) : false;
        };

        UI["SetElementText"] = [](const FString& ElementId, const FString& Text) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().SetElementText(ElementId, Text) : false;
        };

        UI["GetElementText"] = [](const FString& ElementId) -> FString
        {
            return GEngine ? GEngine->GetRmlUiSystem().GetElementText(ElementId) : "";
        };

        UI["GetElementValue"] = [](const FString& ElementId) -> FString
        {
            return GEngine ? GEngine->GetRmlUiSystem().GetElementValue(ElementId) : "";
        };

        UI["SetElementValue"] = [](const FString& ElementId, const FString& Value) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().SetElementValue(ElementId, Value) : false;
        };

        UI["HasElement"] = [](const FString& ElementId) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().HasElement(ElementId) : false;
        };

        UI["SetElementVisible"] = [](const FString& ElementId, bool bVisible) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().SetElementVisible(ElementId, bVisible) : false;
        };

        UI["SetElementEnabled"] = [](const FString& ElementId, bool bEnabled) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().SetElementEnabled(ElementId, bEnabled) : false;
        };

        UI["SetElementClass"] = [](const FString& ElementId, const FString& ClassName, bool bEnabled) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().SetElementClass(ElementId, ClassName, bEnabled) : false;
        };

        UI["HasElementClass"] = [](const FString& ElementId, const FString& ClassName) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().HasElementClass(ElementId, ClassName) : false;
        };

        UI["GetElementClassNames"] = [](const FString& ElementId) -> FString
        {
            return GEngine ? GEngine->GetRmlUiSystem().GetElementClassNames(ElementId) : "";
        };

        UI["SetElementClassNames"] = [](const FString& ElementId, const FString& ClassNames) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().SetElementClassNames(ElementId, ClassNames) : false;
        };

        UI["HasElementAttribute"] = [](const FString& ElementId, const FString& Name) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().HasElementAttribute(ElementId, Name) : false;
        };

        UI["GetElementAttribute"] = [](const FString& ElementId, const FString& Name) -> FString
        {
            return GEngine ? GEngine->GetRmlUiSystem().GetElementAttribute(ElementId, Name) : "";
        };

        UI["SetElementAttribute"] = [](const FString& ElementId, const FString& Name, const FString& Value) -> bool
        {
            return SetAttribute(ElementId, Name, Value);
        };

        UI["RemoveElementAttribute"] = [](const FString& ElementId, const FString& Name) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().RemoveElementAttribute(ElementId, Name) : false;
        };

        UI["GetElementStyle"] = [](const FString& ElementId, const FString& Name) -> FString
        {
            return GEngine ? GEngine->GetRmlUiSystem().GetElementStyle(ElementId, Name) : "";
        };

        UI["SetElementStyle"] = [](const FString& ElementId, const FString& Name, const FString& Value) -> bool
        {
            return SetStyle(ElementId, Name, Value);
        };

        UI["RemoveElementStyle"] = [](const FString& ElementId, const FString& Name) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().RemoveElementStyle(ElementId, Name) : false;
        };

        UI["FocusElement"] = sol::overload(
            [](const FString& ElementId) -> bool
            {
                return GEngine ? GEngine->GetRmlUiSystem().FocusElement(ElementId, false) : false;
            },
            [](const FString& ElementId, bool bFocusVisible) -> bool
            {
                return GEngine ? GEngine->GetRmlUiSystem().FocusElement(ElementId, bFocusVisible) : false;
            });

        UI["IsElementFocused"] = [](const FString& ElementId) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().IsElementFocused(ElementId) : false;
        };

        UI["BlurElement"] = [](const FString& ElementId) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().BlurElement(ElementId) : false;
        };

        UI["ClickElement"] = [](const FString& ElementId) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().ClickElement(ElementId) : false;
        };

        UI["SetText"] = [](const FString& ElementId, const FString& Text) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().SetElementText(ElementId, Text) : false;
        };

        UI["GetValue"] = [](const FString& ElementId) -> FString
        {
            return GEngine ? GEngine->GetRmlUiSystem().GetElementValue(ElementId) : "";
        };

        UI["SetValue"] = [](const FString& ElementId, const FString& Value) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().SetElementValue(ElementId, Value) : false;
        };

        UI["SetImage"] = [](const FString& ElementId, const FString& ImagePath) -> bool
        {
            bool bResult = SetAttribute(ElementId, "src", ImagePath);
            bResult = SetAttribute(ElementId, "data-ui-image", ImagePath) || bResult;
            return bResult;
        };

        UI["SetProgress"] = [](const FString& ElementId, float Value) -> bool
        {
            return SetAttribute(ElementId, "value", std::to_string(Value));
        };

        UI["SetVisible"] = [](const FString& ElementId, bool bVisible) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().SetElementVisible(ElementId, bVisible) : false;
        };

        UI["SetEnabled"] = [](const FString& ElementId, bool bEnabled) -> bool
        {
            return GEngine ? GEngine->GetRmlUiSystem().SetElementEnabled(ElementId, bEnabled) : false;
        };

        UI["SetActionEvent"] = [](const FString& ElementId, const FString& EventName) -> bool
        {
            return SetAttribute(ElementId, "data-action", EventName);
        };

        UI["RemoveElement"] = [](const FString& ElementId) -> bool
        {
            bool bResult = GEngine ? GEngine->GetRmlUiSystem().SetElementVisible(ElementId, false) : false;
            bResult = SetAttribute(ElementId, "disabled", "true") || bResult;
            return bResult;
        };

        UI["SetZOrder"] = [](const FString& ElementId, int32 ZOrder) -> bool
        {
            return SetStyle(ElementId, "z-index", std::to_string(ZOrder));
        };

        UI["SetTint"] = [](const FString& ElementId, float R, float G, float B, float A) -> bool
        {
            const FString Color = ToCssColor(R, G, B, A);
            bool bResult = SetStyle(ElementId, "color", Color);
            bResult = SetStyle(ElementId, "image-color", Color) || bResult;
            return bResult;
        };

        UI["SetImageTint"] = [](const FString& ElementId, float R, float G, float B, float A) -> bool
        {
            return SetStyle(ElementId, "image-color", ToCssColor(R, G, B, A));
        };

        UI["SetImageFit"] = [](const FString& ElementId, const FString& Fit) -> bool
        {
            FString ObjectFit;
            if (Fit == "stretch" || Fit == "Stretch" || Fit == "fill" || Fit == "Fill")
            {
                ObjectFit = "fill";
            }
            else if (Fit == "contain" || Fit == "Contain")
            {
                ObjectFit = "contain";
            }
            else if (Fit == "cover" || Fit == "Cover")
            {
                ObjectFit = "cover";
            }
            else
            {
                return false;
            }

            bool bResult = SetAttribute(ElementId, "data-ui-fit", Fit);
            bResult = SetStyle(ElementId, "object-fit", ObjectFit) || bResult;
            return bResult;
        };

        UI["SetBackgroundColor"] = [](const FString& ElementId, float R, float G, float B, float A) -> bool
        {
            return SetStyle(ElementId, "background-color", ToCssColor(R, G, B, A));
        };

        UI["SetTextColor"] = [](const FString& ElementId, float R, float G, float B, float A) -> bool
        {
            return SetStyle(ElementId, "color", ToCssColor(R, G, B, A));
        };

        UI["SetAlpha"] = [](const FString& ElementId, float Alpha) -> bool
        {
            return SetStyle(ElementId, "opacity", std::to_string(std::clamp(Alpha, 0.0f, 1.0f)));
        };

        UI["SetRounding"] = [](const FString& ElementId, float Rounding) -> bool
        {
            return SetStyle(ElementId, "border-radius", ToPx(Rounding));
        };

        UI["SetPosition"] = [](const FString& ElementId, float X, float Y) -> bool
        {
            bool bResult = SetStyle(ElementId, "position", "absolute");
            bResult = SetStyle(ElementId, "left", ToPx(X)) || bResult;
            bResult = SetStyle(ElementId, "top", ToPx(Y)) || bResult;
            return bResult;
        };

        UI["SetSize"] = [](const FString& ElementId, float W, float H) -> bool
        {
            bool bResult = SetStyle(ElementId, "width", ToPx(std::max(W, 1.0f)));
            bResult = SetStyle(ElementId, "height", ToPx(std::max(H, 1.0f))) || bResult;
            bResult = SetAttribute(ElementId, "width", std::to_string(static_cast<int32>(std::round(std::max(W, 1.0f))))) || bResult;
            bResult = SetAttribute(ElementId, "height", std::to_string(static_cast<int32>(std::round(std::max(H, 1.0f))))) || bResult;
            return bResult;
        };

        UI["SetFontSize"] = [](const FString& ElementId, float FontSize) -> bool
        {
            return SetStyle(ElementId, "font-size", ToPx(std::max(FontSize, 1.0f)));
        };

        UI["SetFontFamily"] = [](const FString& ElementId, const FString& FontFamily) -> bool
        {
            return SetStyle(ElementId, "font-family", "\"" + FontFamily + "\"");
        };

        UI["SetFontWeight"] = [](const FString& ElementId, int32 FontWeight) -> bool
        {
            return SetStyle(ElementId, "font-weight", std::to_string(std::clamp(FontWeight, 100, 900)));
        };

        UI["SetFontScale"] = [](const FString& ElementId, float FontScale) -> bool
        {
            return SetStyle(ElementId, "font-size", std::to_string(std::max(FontScale, 0.0f)) + "em");
        };

        UI["SetLineHeight"] = [](const FString& ElementId, float LineHeight) -> bool
        {
            return SetStyle(ElementId, "line-height", ToPx(std::max(LineHeight, 1.0f)));
        };

        UI["SetLetterSpacing"] = [](const FString& ElementId, float LetterSpacing) -> bool
        {
            return SetStyle(ElementId, "letter-spacing", ToPx(LetterSpacing));
        };

        UI["SetTextWrap"] = [](const FString& ElementId, bool bWrap) -> bool
        {
            return SetStyle(ElementId, "white-space", bWrap ? "normal" : "nowrap");
        };

        UI["SetTextAlign"] = [](const FString& ElementId, const FString& Align) -> bool
        {
            if (Align != "left" && Align != "center" && Align != "right")
            {
                return false;
            }
            return SetStyle(ElementId, "text-align", Align);
        };

        UI["SetElementTransform"] = [](const FString& ElementId, float X, float Y, float W, float H) -> bool
        {
            return SetTransformStyles(ElementId, X, Y, W, H);
        };

        UI["PollActionEvents"] = [](sol::this_state State)
        {
            sol::state_view Lua(State);
            sol::table Events = Lua.create_table();
            if (!GEngine)
            {
                return Events;
            }

            const TArray<FString> PendingEvents = GEngine->GetRmlUiSystem().PollActionEvents();
            int32 Index = 1;
            for (const FString& EventName : PendingEvents)
            {
                Events[Index++] = EventName;
            }
            return Events;
        };

        UI["DispatchActionEvents"] = [](sol::table Handlers) -> int32
        {
            if (!GEngine)
            {
                return 0;
            }

            int32 DispatchCount = 0;
            const TArray<FString> PendingEvents = GEngine->GetRmlUiSystem().PollActionEvents();
            for (const FString& EventName : PendingEvents)
            {
                sol::object Handler = Handlers.get<sol::object>(EventName);
                if (!Handler.is<sol::protected_function>())
                {
                    continue;
                }

                sol::protected_function Function = Handler.as<sol::protected_function>();
                sol::protected_function_result Result = Function(EventName);
                ++DispatchCount;
                if (!Result.valid())
                {
                    sol::error Error = Result;
                    UE_LOG_WARNING("[LuaUI] Action handler failed for %s: %s", EventName.c_str(), Error.what());
                }
            }
            return DispatchCount;
        };

        API["UI"] = UI;
    }
}
