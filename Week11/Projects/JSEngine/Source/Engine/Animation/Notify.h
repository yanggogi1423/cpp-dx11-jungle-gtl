#pragma once
#include "Animation/AnimSequenceBase.h"
#include "Engine/Object/Object.h"

enum class EAnimNotifyType : uint8
{
    Trigger,
    Start,
    Tick,
    End
};
struct FAnimNotifyContext
{
    class USkeletalMeshComponent* Component = nullptr;
    class UAnimSequence* Sequence = nullptr;
    FAnimNotifyEvent Notify;
    EAnimNotifyType Type = EAnimNotifyType::Trigger;
};

UCLASS()
class UNotify : public UObject
{
public:
    GENERATED_BODY(UNotify, UObject)

    void ProcessNotify(const FAnimNotifyContext& Context);

protected:
    virtual void OnTriggered(const FAnimNotifyContext& Context) {}
    virtual void OnStarted(const FAnimNotifyContext& Context) {}
    virtual void OnTicked(const FAnimNotifyContext& Context) {}
    virtual void OnEnded(const FAnimNotifyContext& Context) {}
};
