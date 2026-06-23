#pragma once

#include "Engine/Input/InputBinding.h"

namespace ObjViewerInputMapping
{
    enum class EObjViewerAction : int32
    {
        LookRightDown,
        PanMiddleDown,
        ZoomWheel
    };

    inline const TArray<FInputBinding>& GetBindings()
    {
        static const TArray<FInputBinding> Bindings =
        {
            { static_cast<int32>(EObjViewerAction::LookRightDown), EInputBindingTrigger::Down, { VK_RBUTTON, false, false, false }, EInputEventType::KeyPressed },
            { static_cast<int32>(EObjViewerAction::PanMiddleDown), EInputBindingTrigger::Down, { VK_MBUTTON, false, false, false }, EInputEventType::KeyPressed },
            { static_cast<int32>(EObjViewerAction::ZoomWheel), EInputBindingTrigger::EventType, { 0, false, false, false }, EInputEventType::WheelScrolled }
        };

        return Bindings;
    }

    inline bool IsTriggered(const FViewportInputContext& Context, EObjViewerAction Action)
    {
        return InputBindingUtils::IsActionTriggered(Context, GetBindings(), static_cast<int32>(Action));
    }
}

