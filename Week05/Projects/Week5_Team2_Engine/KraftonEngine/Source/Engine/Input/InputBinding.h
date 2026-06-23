#pragma once

#include "Engine/Input/InputTypes.h"

enum class EInputBindingTrigger : uint8
{
    Pressed,
    Down,
    Released,
    EventType
};

struct FInputBinding
{
    int32 ActionId = 0;
    EInputBindingTrigger Trigger = EInputBindingTrigger::Pressed;
    FInputChord Chord{};
    EInputEventType EventType = EInputEventType::KeyPressed;
    int32 Priority = 0;
};

namespace InputBindingUtils
{
    inline bool IsBindingTriggered(const FViewportInputContext& Context, const FInputBinding& Binding)
    {
        switch (Binding.Trigger)
        {
        case EInputBindingTrigger::Pressed:
            return Context.MatchesChordPressed(Binding.Chord);
        case EInputBindingTrigger::Down:
            return Context.MatchesChordDown(Binding.Chord);
        case EInputBindingTrigger::Released:
            return Context.WasReleased(Binding.Chord.Key) && Binding.Chord.MatchesState(Context.Frame);
        case EInputBindingTrigger::EventType:
            for (const FInputEvent& Event : Context.Events)
            {
                if (Event.Type != Binding.EventType)
                {
                    continue;
                }

                if (Binding.Chord.Key != 0 && Event.Key != Binding.Chord.Key)
                {
                    continue;
                }

                if (!Binding.Chord.MatchesState(Context.Frame))
                {
                    continue;
                }

                return true;
            }
            return false;
        default:
            return false;
        }
    }

    inline bool IsActionTriggered(const FViewportInputContext& Context, const TArray<FInputBinding>& Bindings, int32 ActionId)
    {
        for (const FInputBinding& Binding : Bindings)
        {
            if (Binding.ActionId != ActionId)
            {
                continue;
            }

            if (IsBindingTriggered(Context, Binding))
            {
                return true;
            }
        }

        return false;
    }

    inline bool TryGetHighestPriorityTriggeredAction(
        const FViewportInputContext& Context,
        const TArray<FInputBinding>& Bindings,
        const TArray<int32>& CandidateActionIds,
        int32& OutActionId)
    {
        bool bFound = false;
        int32 BestPriority = 0;
        int32 BestActionId = 0;

        for (const FInputBinding& Binding : Bindings)
        {
            bool bCandidate = CandidateActionIds.empty();
            if (!bCandidate)
            {
                for (int32 CandidateActionId : CandidateActionIds)
                {
                    if (CandidateActionId == Binding.ActionId)
                    {
                        bCandidate = true;
                        break;
                    }
                }
            }

            if (!bCandidate)
            {
                continue;
            }

            if (!IsBindingTriggered(Context, Binding))
            {
                continue;
            }

            if (!bFound || Binding.Priority > BestPriority)
            {
                bFound = true;
                BestPriority = Binding.Priority;
                BestActionId = Binding.ActionId;
            }
        }

        if (!bFound)
        {
            return false;
        }

        OutActionId = BestActionId;
        return true;
    }
}
