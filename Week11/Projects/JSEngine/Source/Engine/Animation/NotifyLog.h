#pragma once

#include "Animation/Notify.h"

UCLASS()
class UNotify_Log : public UNotify
{
public:
    GENERATED_BODY(UNotify_Log, UNotify)

    void OnTriggered(const FAnimNotifyContext& Context) override;

    void OnStarted(const FAnimNotifyContext& Context) override;
    void OnTicked(const FAnimNotifyContext& Context) override;
    void OnEnded(const FAnimNotifyContext& Context) override;
};
