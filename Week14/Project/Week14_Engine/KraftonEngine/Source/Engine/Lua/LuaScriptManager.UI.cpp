#include "LuaScriptManager.h"

#include "Object/Object.h"
#include "UI/UIManager.h"
#include "UI/UserWidget.h"

#include <algorithm>
#include <cstdio>

namespace
{
    FString LuaUiToPx(float Value)
    {
        return std::to_string(Value) + "px";
    }

    FString LuaUiToSeconds(float Value)
    {
        return std::to_string((std::max)(Value, 0.0f)) + "s";
    }

    FString LuaUiToCssColor(float R, float G, float B, float A)
    {
        const int32 RI = static_cast<int32>(std::clamp(R, 0.0f, 1.0f) * 255.0f);
        const int32 GI = static_cast<int32>(std::clamp(G, 0.0f, 1.0f) * 255.0f);
        const int32 BI = static_cast<int32>(std::clamp(B, 0.0f, 1.0f) * 255.0f);
        const float Alpha = std::clamp(A, 0.0f, 1.0f);

        char Buffer[96] = {};
        std::snprintf(Buffer, sizeof(Buffer), "rgba(%d,%d,%d,%.3f)", RI, GI, BI, Alpha);
        return FString(Buffer);
    }

    FString LuaUiMakeTransition(const FString& PropertyName, float Duration, const FString& Timing, float Delay)
    {
        FString Value = (PropertyName.empty() ? "all" : PropertyName) + " " + LuaUiToSeconds(Duration);
        Value += " " + (Timing.empty() ? "linear" : Timing);
        if (Delay > 0.0f)
        {
            Value += " " + LuaUiToSeconds(Delay);
        }
        return Value;
    }

    FString LuaUiMakeBoxTransition(float Duration, const FString& Timing, float Delay)
    {
        const FString Suffix = " " + LuaUiToSeconds(Duration) + " " + (Timing.empty() ? "linear" : Timing)
            + (Delay > 0.0f ? " " + LuaUiToSeconds(Delay) : "");
        return "left" + Suffix + ", top" + Suffix + ", width" + Suffix + ", height" + Suffix;
    }

    bool LuaUiSetWidgetTransform(UUserWidget& Widget, const FString& ElementId, float X, float Y, float W, float H)
    {
        bool bResult = Widget.SetElementStyle(ElementId, "position", "absolute");
        bResult = Widget.SetElementStyle(ElementId, "left", LuaUiToPx(X)) || bResult;
        bResult = Widget.SetElementStyle(ElementId, "top", LuaUiToPx(Y)) || bResult;
        bResult = Widget.SetElementStyle(ElementId, "width", LuaUiToPx(W)) || bResult;
        bResult = Widget.SetElementStyle(ElementId, "height", LuaUiToPx(H)) || bResult;
        return bResult;
    }

    bool LuaUiSetGlobalTransform(const FString& ElementId, float X, float Y, float W, float H)
    {
        bool bResult = UUIManager::Get().SetElementStyle(ElementId, "position", "absolute");
        bResult = UUIManager::Get().SetElementStyle(ElementId, "left", LuaUiToPx(X)) || bResult;
        bResult = UUIManager::Get().SetElementStyle(ElementId, "top", LuaUiToPx(Y)) || bResult;
        bResult = UUIManager::Get().SetElementStyle(ElementId, "width", LuaUiToPx(W)) || bResult;
        bResult = UUIManager::Get().SetElementStyle(ElementId, "height", LuaUiToPx(H)) || bResult;
        return bResult;
    }

    bool LuaUiRemoveWidgetElement(UUserWidget& Widget, const FString& ElementId)
    {
        bool bResult = Widget.SetElementVisible(ElementId, false);
        bResult = Widget.SetElementAttribute(ElementId, "disabled", "true") || bResult;
        return bResult;
    }

    bool LuaUiRemoveGlobalElement(const FString& ElementId)
    {
        bool bResult = UUIManager::Get().SetElementVisible(ElementId, false);
        bResult = UUIManager::Get().SetElementAttribute(ElementId, "disabled", "true") || bResult;
        return bResult;
    }

    bool LuaUiSetWidgetTransition(UUserWidget& Widget, const FString& ElementId, const FString& PropertyName, float Duration, const FString& Timing, float Delay)
    {
        return Widget.SetElementStyle(ElementId, "transition", LuaUiMakeTransition(PropertyName, Duration, Timing, Delay));
    }

    bool LuaUiSetGlobalTransition(const FString& ElementId, const FString& PropertyName, float Duration, const FString& Timing, float Delay)
    {
        return UUIManager::Get().SetElementStyle(ElementId, "transition", LuaUiMakeTransition(PropertyName, Duration, Timing, Delay));
    }
}

void FLuaScriptManager::RegisterUIBindings(sol::state& Lua)
{
    Lua.new_usertype<UUserWidget>(
        "UserWidget",
        sol::base_classes,
        sol::bases<UObject>(),
        "AddToViewport",
        [](UUserWidget& Widget)
        {
            Widget.AddToViewport();
        },
        "AddToViewportZ",
        [](UUserWidget& Widget, int32 ZOrder)
        {
            Widget.AddToViewport(ZOrder);
        },
        "RemoveFromParent",
        &UUserWidget::RemoveFromParent,
        "Show",
        [](UUserWidget& Widget)
        {
            Widget.AddToViewport();
        },
        "Hide",
        &UUserWidget::RemoveFromParent,
        "show",
        [](UUserWidget& Widget)
        {
            Widget.AddToViewport();
        },
        "hide",
        &UUserWidget::RemoveFromParent,
        "IsInViewport",
        &UUserWidget::IsInViewport,
        "bind_click",
        [](UUserWidget& Widget, const FString& ElementId, sol::protected_function Callback)
        {
            Widget.BindClick(ElementId, Callback);
        },
        "SetText",
        &UUserWidget::SetText,
        "set_text",
        &UUserWidget::SetText,
        "GetText",
        &UUserWidget::GetText,
        "get_text",
        &UUserWidget::GetText,
        "SetProperty",
        &UUserWidget::SetProperty,
        "set_property",
        &UUserWidget::SetProperty,
        "HasElement",
        &UUserWidget::HasElement,
        "GetElementValue",
        &UUserWidget::GetElementValue,
        "GetValue",
        &UUserWidget::GetElementValue,
        "SetElementValue",
        &UUserWidget::SetElementValue,
        "SetValue",
        &UUserWidget::SetElementValue,
        "SetElementClass",
        &UUserWidget::SetElementClass,
        "SetClass",
        &UUserWidget::SetElementClass,
        "HasElementClass",
        &UUserWidget::HasElementClass,
        "HasClass",
        &UUserWidget::HasElementClass,
        "GetElementClassNames",
        &UUserWidget::GetElementClassNames,
        "GetClassNames",
        &UUserWidget::GetElementClassNames,
        "SetElementClassNames",
        &UUserWidget::SetElementClassNames,
        "SetClassNames",
        &UUserWidget::SetElementClassNames,
        "HasElementAttribute",
        &UUserWidget::HasElementAttribute,
        "HasAttribute",
        &UUserWidget::HasElementAttribute,
        "GetElementAttribute",
        &UUserWidget::GetElementAttribute,
        "GetAttribute",
        &UUserWidget::GetElementAttribute,
        "SetElementAttribute",
        &UUserWidget::SetElementAttribute,
        "SetAttribute",
        &UUserWidget::SetElementAttribute,
        "RemoveElementAttribute",
        &UUserWidget::RemoveElementAttribute,
        "RemoveAttribute",
        &UUserWidget::RemoveElementAttribute,
        "GetElementStyle",
        &UUserWidget::GetElementStyle,
        "GetStyle",
        &UUserWidget::GetElementStyle,
        "SetElementStyle",
        &UUserWidget::SetElementStyle,
        "SetStyle",
        &UUserWidget::SetElementStyle,
        "SetImage",
        [](UUserWidget& Widget, const FString& ElementId, const FString& ImagePath)
        {
            return Widget.SetElementAttribute(ElementId, "src", ImagePath);
        },
        "SetProgress",
        [](UUserWidget& Widget, const FString& ElementId, float Value)
        {
            return Widget.SetElementValue(ElementId, std::to_string(Value));
        },
        "SetZOrder",
        [](UUserWidget& Widget, const FString& ElementId, int32 ZOrder)
        {
            return Widget.SetElementStyle(ElementId, "z-index", std::to_string(ZOrder));
        },
        "SetTint",
        [](UUserWidget& Widget, const FString& ElementId, float R, float G, float B, sol::optional<float> A)
        {
            return Widget.SetElementStyle(ElementId, "color", LuaUiToCssColor(R, G, B, A.value_or(1.0f)));
        },
        "SetTextColor",
        [](UUserWidget& Widget, const FString& ElementId, float R, float G, float B, sol::optional<float> A)
        {
            return Widget.SetElementStyle(ElementId, "color", LuaUiToCssColor(R, G, B, A.value_or(1.0f)));
        },
        "SetBackgroundColor",
        [](UUserWidget& Widget, const FString& ElementId, float R, float G, float B, sol::optional<float> A)
        {
            return Widget.SetElementStyle(ElementId, "background-color", LuaUiToCssColor(R, G, B, A.value_or(1.0f)));
        },
        "SetAlpha",
        [](UUserWidget& Widget, const FString& ElementId, float Alpha)
        {
            return Widget.SetElementStyle(ElementId, "opacity", std::to_string(std::clamp(Alpha, 0.0f, 1.0f)));
        },
        "SetRounding",
        [](UUserWidget& Widget, const FString& ElementId, float Rounding)
        {
            return Widget.SetElementStyle(ElementId, "border-radius", LuaUiToPx(Rounding));
        },
        "SetFontScale",
        [](UUserWidget& Widget, const FString& ElementId, float FontScale)
        {
            return Widget.SetElementStyle(ElementId, "font-size", std::to_string((std::max)(FontScale, 0.0f)) + "em");
        },
        "SetElementTransform",
        [](UUserWidget& Widget, const FString& ElementId, float X, float Y, float W, float H)
        {
            return LuaUiSetWidgetTransform(Widget, ElementId, X, Y, W, H);
        },
        "SetTransform",
        [](UUserWidget& Widget, const FString& ElementId, float X, float Y, float W, float H)
        {
            return LuaUiSetWidgetTransform(Widget, ElementId, X, Y, W, H);
        },
        "SetTransition",
        [](UUserWidget& Widget, const FString& ElementId, const FString& PropertyName, float Duration, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            return LuaUiSetWidgetTransition(Widget, ElementId, PropertyName, Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
        },
        "SetTransitionAll",
        [](UUserWidget& Widget, const FString& ElementId, float Duration, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            return LuaUiSetWidgetTransition(Widget, ElementId, "all", Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
        },
        "ClearTransition",
        [](UUserWidget& Widget, const FString& ElementId)
        {
            return Widget.SetElementStyle(ElementId, "transition", "none");
        },
        "AnimateAlpha",
        [](UUserWidget& Widget, const FString& ElementId, float Alpha, float Duration, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            bool bResult = LuaUiSetWidgetTransition(Widget, ElementId, "opacity", Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
            bResult = Widget.SetElementStyle(ElementId, "opacity", std::to_string(std::clamp(Alpha, 0.0f, 1.0f))) || bResult;
            return bResult;
        },
        "AnimateTextColor",
        [](UUserWidget& Widget, const FString& ElementId, float R, float G, float B, float Duration, sol::optional<float> A, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            bool bResult = LuaUiSetWidgetTransition(Widget, ElementId, "color", Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
            bResult = Widget.SetElementStyle(ElementId, "color", LuaUiToCssColor(R, G, B, A.value_or(1.0f))) || bResult;
            return bResult;
        },
        "AnimateBackgroundColor",
        [](UUserWidget& Widget, const FString& ElementId, float R, float G, float B, float Duration, sol::optional<float> A, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            bool bResult = LuaUiSetWidgetTransition(Widget, ElementId, "background-color", Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
            bResult = Widget.SetElementStyle(ElementId, "background-color", LuaUiToCssColor(R, G, B, A.value_or(1.0f))) || bResult;
            return bResult;
        },
        "AnimateTransform",
        [](UUserWidget& Widget, const FString& ElementId, float X, float Y, float W, float H, float Duration, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            bool bResult = Widget.SetElementStyle(ElementId, "transition", LuaUiMakeBoxTransition(Duration, Timing.value_or("linear"), Delay.value_or(0.0f)));
            bResult = LuaUiSetWidgetTransform(Widget, ElementId, X, Y, W, H) || bResult;
            return bResult;
        },
        "AnimateClass",
        [](UUserWidget& Widget, const FString& ElementId, const FString& ClassName, bool bEnabled, float Duration, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            bool bResult = LuaUiSetWidgetTransition(Widget, ElementId, "all", Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
            bResult = Widget.SetElementClass(ElementId, ClassName, bEnabled) || bResult;
            return bResult;
        },
        "RemoveElementStyle",
        &UUserWidget::RemoveElementStyle,
        "RemoveStyle",
        &UUserWidget::RemoveElementStyle,
        "RemoveElement",
        [](UUserWidget& Widget, const FString& ElementId)
        {
            return LuaUiRemoveWidgetElement(Widget, ElementId);
        },
        "FocusElement",
        [](UUserWidget& Widget, const FString& ElementId, sol::optional<bool> bFocusVisible)
        {
            return Widget.FocusElement(ElementId, bFocusVisible.value_or(false));
        },
        "Focus",
        [](UUserWidget& Widget, const FString& ElementId, sol::optional<bool> bFocusVisible)
        {
            return Widget.FocusElement(ElementId, bFocusVisible.value_or(false));
        },
        "BlurElement",
        &UUserWidget::BlurElement,
        "Blur",
        &UUserWidget::BlurElement,
        "IsElementFocused",
        &UUserWidget::IsElementFocused,
        "IsFocused",
        &UUserWidget::IsElementFocused,
        "ClickElement",
        &UUserWidget::ClickElement,
        "Click",
        &UUserWidget::ClickElement,
        "SetElementVisible",
        &UUserWidget::SetElementVisible,
        "SetVisible",
        &UUserWidget::SetElementVisible,
        "SetElementEnabled",
        &UUserWidget::SetElementEnabled,
        "SetEnabled",
        &UUserWidget::SetElementEnabled,
        "SetActionEvent",
        &UUserWidget::SetActionEvent,
        "PollActionEvents",
        [](UUserWidget& Widget, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table Events = L.create_table();
            int Index = 1;
            for (const FString& EventName : Widget.PollActionEvents())
            {
                Events[Index++] = EventName;
            }
            return Events;
        },
        "SetWantsMouse",
        &UUserWidget::SetWantsMouse,
        "WantsMouse",
        &UUserWidget::WantsMouse,
        "SetWantsKeyboard",
        &UUserWidget::SetWantsKeyboard,
        "WantsKeyboard",
        &UUserWidget::WantsKeyboard,
        "SetWantsTextInput",
        &UUserWidget::SetWantsTextInput,
        "WantsTextInput",
        &UUserWidget::WantsTextInput,
        "SetBlocksGameInput",
        &UUserWidget::SetBlocksGameInput,
        "BlocksGameInput",
        &UUserWidget::BlocksGameInput,
        "SetBlocksGameKeyboard",
        &UUserWidget::SetBlocksGameKeyboard,
        "BlocksGameKeyboard",
        &UUserWidget::BlocksGameKeyboard,
        "SetBlocksGameMouseLook",
        &UUserWidget::SetBlocksGameMouseLook,
        "BlocksGameMouseLook",
        &UUserWidget::BlocksGameMouseLook
    );

    sol::table UI = Lua.create_named_table("UI");
    UI.set_function(
        "CreateWidget",
        [](const FString& DocumentPath)
        {
            return UUIManager::Get().CreateWidget(nullptr, DocumentPath);
        }
    );
    UI.set_function(
        "ClearViewport",
        []()
        {
            UUIManager::Get().ClearViewport();
        }
    );
    UI.set_function(
        "GetViewportSize",
        [](sol::this_state State)
        {
            sol::state_view Lua(State);
            const FVector2 Size = UUIManager::Get().GetVirtualViewportSize();
            sol::table Result = Lua.create_table();
            Result["X"] = Size.X;
            Result["Y"] = Size.Y;
            Result["x"] = Size.X;
            Result["y"] = Size.Y;
            Result["Width"] = Size.X;
            Result["Height"] = Size.Y;
            Result["width"] = Size.X;
            Result["height"] = Size.Y;
            return Result;
        }
    );
    UI.set_function(
        "GetPhysicalViewportSize",
        [](sol::this_state State)
        {
            sol::state_view Lua(State);
            const FVector2 Size = UUIManager::Get().GetPhysicalViewportSize();
            sol::table Result = Lua.create_table();
            Result["X"] = Size.X;
            Result["Y"] = Size.Y;
            Result["x"] = Size.X;
            Result["y"] = Size.Y;
            Result["Width"] = Size.X;
            Result["Height"] = Size.Y;
            Result["width"] = Size.X;
            Result["height"] = Size.Y;
            return Result;
        }
    );
    UI.set_function(
        "GetElementText",
        [](const FString& ElementId)
        {
            return UUIManager::Get().GetElementText(ElementId);
        }
    );
    UI.set_function(
        "SetElementText",
        [](const FString& ElementId, const FString& Text)
        {
            return UUIManager::Get().SetElementText(ElementId, Text);
        }
    );
    UI.set_function(
        "SetText",
        [](const FString& ElementId, const FString& Text)
        {
            return UUIManager::Get().SetElementText(ElementId, Text);
        }
    );
    UI.set_function(
        "GetElementValue",
        [](const FString& ElementId)
        {
            return UUIManager::Get().GetElementValue(ElementId);
        }
    );
    UI.set_function(
        "GetValue",
        [](const FString& ElementId)
        {
            return UUIManager::Get().GetElementValue(ElementId);
        }
    );
    UI.set_function(
        "SetElementValue",
        [](const FString& ElementId, const FString& Value)
        {
            return UUIManager::Get().SetElementValue(ElementId, Value);
        }
    );
    UI.set_function(
        "SetValue",
        [](const FString& ElementId, const FString& Value)
        {
            return UUIManager::Get().SetElementValue(ElementId, Value);
        }
    );
    UI.set_function(
        "SetElementClass",
        [](const FString& ElementId, const FString& ClassName, bool bEnabled)
        {
            return UUIManager::Get().SetElementClass(ElementId, ClassName, bEnabled);
        }
    );
    UI.set_function(
        "SetClass",
        [](const FString& ElementId, const FString& ClassName, bool bEnabled)
        {
            return UUIManager::Get().SetElementClass(ElementId, ClassName, bEnabled);
        }
    );
    UI.set_function(
        "HasElementClass",
        [](const FString& ElementId, const FString& ClassName)
        {
            return UUIManager::Get().HasElementClass(ElementId, ClassName);
        }
    );
    UI.set_function(
        "HasClass",
        [](const FString& ElementId, const FString& ClassName)
        {
            return UUIManager::Get().HasElementClass(ElementId, ClassName);
        }
    );
    UI.set_function(
        "GetElementClassNames",
        [](const FString& ElementId)
        {
            return UUIManager::Get().GetElementClassNames(ElementId);
        }
    );
    UI.set_function(
        "GetClassNames",
        [](const FString& ElementId)
        {
            return UUIManager::Get().GetElementClassNames(ElementId);
        }
    );
    UI.set_function(
        "SetElementClassNames",
        [](const FString& ElementId, const FString& ClassNames)
        {
            return UUIManager::Get().SetElementClassNames(ElementId, ClassNames);
        }
    );
    UI.set_function(
        "SetClassNames",
        [](const FString& ElementId, const FString& ClassNames)
        {
            return UUIManager::Get().SetElementClassNames(ElementId, ClassNames);
        }
    );
    UI.set_function(
        "HasElementAttribute",
        [](const FString& ElementId, const FString& AttributeName)
        {
            return UUIManager::Get().HasElementAttribute(ElementId, AttributeName);
        }
    );
    UI.set_function(
        "HasAttribute",
        [](const FString& ElementId, const FString& AttributeName)
        {
            return UUIManager::Get().HasElementAttribute(ElementId, AttributeName);
        }
    );
    UI.set_function(
        "GetElementAttribute",
        [](const FString& ElementId, const FString& AttributeName)
        {
            return UUIManager::Get().GetElementAttribute(ElementId, AttributeName);
        }
    );
    UI.set_function(
        "GetAttribute",
        [](const FString& ElementId, const FString& AttributeName)
        {
            return UUIManager::Get().GetElementAttribute(ElementId, AttributeName);
        }
    );
    UI.set_function(
        "SetElementAttribute",
        [](const FString& ElementId, const FString& AttributeName, const FString& Value)
        {
            return UUIManager::Get().SetElementAttribute(ElementId, AttributeName, Value);
        }
    );
    UI.set_function(
        "SetAttribute",
        [](const FString& ElementId, const FString& AttributeName, const FString& Value)
        {
            return UUIManager::Get().SetElementAttribute(ElementId, AttributeName, Value);
        }
    );
    UI.set_function(
        "RemoveElementAttribute",
        [](const FString& ElementId, const FString& AttributeName)
        {
            return UUIManager::Get().RemoveElementAttribute(ElementId, AttributeName);
        }
    );
    UI.set_function(
        "RemoveAttribute",
        [](const FString& ElementId, const FString& AttributeName)
        {
            return UUIManager::Get().RemoveElementAttribute(ElementId, AttributeName);
        }
    );
    UI.set_function(
        "GetElementStyle",
        [](const FString& ElementId, const FString& StyleName)
        {
            return UUIManager::Get().GetElementStyle(ElementId, StyleName);
        }
    );
    UI.set_function(
        "GetStyle",
        [](const FString& ElementId, const FString& StyleName)
        {
            return UUIManager::Get().GetElementStyle(ElementId, StyleName);
        }
    );
    UI.set_function(
        "SetElementStyle",
        [](const FString& ElementId, const FString& StyleName, const FString& Value)
        {
            return UUIManager::Get().SetElementStyle(ElementId, StyleName, Value);
        }
    );
    UI.set_function(
        "SetStyle",
        [](const FString& ElementId, const FString& StyleName, const FString& Value)
        {
            return UUIManager::Get().SetElementStyle(ElementId, StyleName, Value);
        }
    );
    UI.set_function(
        "SetImage",
        [](const FString& ElementId, const FString& ImagePath)
        {
            return UUIManager::Get().SetElementAttribute(ElementId, "src", ImagePath);
        }
    );
    UI.set_function(
        "SetProgress",
        [](const FString& ElementId, float Value)
        {
            return UUIManager::Get().SetElementValue(ElementId, std::to_string(Value));
        }
    );
    UI.set_function(
        "SetZOrder",
        [](const FString& ElementId, int32 ZOrder)
        {
            return UUIManager::Get().SetElementStyle(ElementId, "z-index", std::to_string(ZOrder));
        }
    );
    UI.set_function(
        "SetTint",
        [](const FString& ElementId, float R, float G, float B, sol::optional<float> A)
        {
            return UUIManager::Get().SetElementStyle(ElementId, "color", LuaUiToCssColor(R, G, B, A.value_or(1.0f)));
        }
    );
    UI.set_function(
        "SetTextColor",
        [](const FString& ElementId, float R, float G, float B, sol::optional<float> A)
        {
            return UUIManager::Get().SetElementStyle(ElementId, "color", LuaUiToCssColor(R, G, B, A.value_or(1.0f)));
        }
    );
    UI.set_function(
        "SetBackgroundColor",
        [](const FString& ElementId, float R, float G, float B, sol::optional<float> A)
        {
            return UUIManager::Get().SetElementStyle(ElementId, "background-color", LuaUiToCssColor(R, G, B, A.value_or(1.0f)));
        }
    );
    UI.set_function(
        "SetAlpha",
        [](const FString& ElementId, float Alpha)
        {
            return UUIManager::Get().SetElementStyle(ElementId, "opacity", std::to_string(std::clamp(Alpha, 0.0f, 1.0f)));
        }
    );
    UI.set_function(
        "SetRounding",
        [](const FString& ElementId, float Rounding)
        {
            return UUIManager::Get().SetElementStyle(ElementId, "border-radius", LuaUiToPx(Rounding));
        }
    );
    UI.set_function(
        "SetFontScale",
        [](const FString& ElementId, float FontScale)
        {
            return UUIManager::Get().SetElementStyle(ElementId, "font-size", std::to_string((std::max)(FontScale, 0.0f)) + "em");
        }
    );
    UI.set_function(
        "SetElementTransform",
        [](const FString& ElementId, float X, float Y, float W, float H)
        {
            return LuaUiSetGlobalTransform(ElementId, X, Y, W, H);
        }
    );
    UI.set_function(
        "SetTransform",
        [](const FString& ElementId, float X, float Y, float W, float H)
        {
            return LuaUiSetGlobalTransform(ElementId, X, Y, W, H);
        }
    );
    UI.set_function(
        "SetTransition",
        [](const FString& ElementId, const FString& PropertyName, float Duration, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            return LuaUiSetGlobalTransition(ElementId, PropertyName, Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
        }
    );
    UI.set_function(
        "SetTransitionAll",
        [](const FString& ElementId, float Duration, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            return LuaUiSetGlobalTransition(ElementId, "all", Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
        }
    );
    UI.set_function(
        "ClearTransition",
        [](const FString& ElementId)
        {
            return UUIManager::Get().SetElementStyle(ElementId, "transition", "none");
        }
    );
    UI.set_function(
        "AnimateAlpha",
        [](const FString& ElementId, float Alpha, float Duration, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            bool bResult = LuaUiSetGlobalTransition(ElementId, "opacity", Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
            bResult = UUIManager::Get().SetElementStyle(ElementId, "opacity", std::to_string(std::clamp(Alpha, 0.0f, 1.0f))) || bResult;
            return bResult;
        }
    );
    UI.set_function(
        "AnimateTextColor",
        [](const FString& ElementId, float R, float G, float B, float Duration, sol::optional<float> A, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            bool bResult = LuaUiSetGlobalTransition(ElementId, "color", Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
            bResult = UUIManager::Get().SetElementStyle(ElementId, "color", LuaUiToCssColor(R, G, B, A.value_or(1.0f))) || bResult;
            return bResult;
        }
    );
    UI.set_function(
        "AnimateBackgroundColor",
        [](const FString& ElementId, float R, float G, float B, float Duration, sol::optional<float> A, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            bool bResult = LuaUiSetGlobalTransition(ElementId, "background-color", Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
            bResult = UUIManager::Get().SetElementStyle(ElementId, "background-color", LuaUiToCssColor(R, G, B, A.value_or(1.0f))) || bResult;
            return bResult;
        }
    );
    UI.set_function(
        "AnimateTransform",
        [](const FString& ElementId, float X, float Y, float W, float H, float Duration, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            bool bResult = UUIManager::Get().SetElementStyle(ElementId, "transition", LuaUiMakeBoxTransition(Duration, Timing.value_or("linear"), Delay.value_or(0.0f)));
            bResult = LuaUiSetGlobalTransform(ElementId, X, Y, W, H) || bResult;
            return bResult;
        }
    );
    UI.set_function(
        "AnimateClass",
        [](const FString& ElementId, const FString& ClassName, bool bEnabled, float Duration, sol::optional<FString> Timing, sol::optional<float> Delay)
        {
            bool bResult = LuaUiSetGlobalTransition(ElementId, "all", Duration, Timing.value_or("linear"), Delay.value_or(0.0f));
            bResult = UUIManager::Get().SetElementClass(ElementId, ClassName, bEnabled) || bResult;
            return bResult;
        }
    );
    UI.set_function(
        "RemoveElement",
        [](const FString& ElementId)
        {
            return LuaUiRemoveGlobalElement(ElementId);
        }
    );
    UI.set_function(
        "RemoveElementStyle",
        [](const FString& ElementId, const FString& StyleName)
        {
            return UUIManager::Get().RemoveElementStyle(ElementId, StyleName);
        }
    );
    UI.set_function(
        "RemoveStyle",
        [](const FString& ElementId, const FString& StyleName)
        {
            return UUIManager::Get().RemoveElementStyle(ElementId, StyleName);
        }
    );
    UI.set_function(
        "FocusElement",
        [](const FString& ElementId, sol::optional<bool> bFocusVisible)
        {
            return UUIManager::Get().FocusElement(ElementId, bFocusVisible.value_or(false));
        }
    );
    UI.set_function(
        "Focus",
        [](const FString& ElementId, sol::optional<bool> bFocusVisible)
        {
            return UUIManager::Get().FocusElement(ElementId, bFocusVisible.value_or(false));
        }
    );
    UI.set_function(
        "BlurElement",
        [](const FString& ElementId)
        {
            return UUIManager::Get().BlurElement(ElementId);
        }
    );
    UI.set_function(
        "Blur",
        [](const FString& ElementId)
        {
            return UUIManager::Get().BlurElement(ElementId);
        }
    );
    UI.set_function(
        "IsElementFocused",
        [](const FString& ElementId)
        {
            return UUIManager::Get().IsElementFocused(ElementId);
        }
    );
    UI.set_function(
        "IsFocused",
        [](const FString& ElementId)
        {
            return UUIManager::Get().IsElementFocused(ElementId);
        }
    );
    UI.set_function(
        "ClickElement",
        [](const FString& ElementId)
        {
            return UUIManager::Get().ClickElement(ElementId);
        }
    );
    UI.set_function(
        "Click",
        [](const FString& ElementId)
        {
            return UUIManager::Get().ClickElement(ElementId);
        }
    );
    UI.set_function(
        "SetElementVisible",
        [](const FString& ElementId, bool bVisible)
        {
            return UUIManager::Get().SetElementVisible(ElementId, bVisible);
        }
    );
    UI.set_function(
        "SetVisible",
        [](const FString& ElementId, bool bVisible)
        {
            return UUIManager::Get().SetElementVisible(ElementId, bVisible);
        }
    );
    UI.set_function(
        "SetElementEnabled",
        [](const FString& ElementId, bool bEnabled)
        {
            return UUIManager::Get().SetElementEnabled(ElementId, bEnabled);
        }
    );
    UI.set_function(
        "SetEnabled",
        [](const FString& ElementId, bool bEnabled)
        {
            return UUIManager::Get().SetElementEnabled(ElementId, bEnabled);
        }
    );
    UI.set_function(
        "SetActionEvent",
        [](const FString& ElementId, const FString& EventName)
        {
            return UUIManager::Get().SetActionEvent(ElementId, EventName);
        }
    );
    UI.set_function(
        "PollActionEvents",
        [](sol::this_state State)
        {
            sol::state_view L(State);
            sol::table Events = L.create_table();
            int Index = 1;
            for (const FString& EventName : UUIManager::Get().PollActionEvents())
            {
                Events[Index++] = EventName;
            }
            return Events;
        }
    );
}
