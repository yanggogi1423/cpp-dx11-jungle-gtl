#pragma once

#include "Animation/AnimSequence.h"
#include "Engine/Object/Object.h"
#include "Engine/Animation/Notify.h"
#include "Core/Delegates/Delegate.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UAnimSequence;

struct FQueuedAnimNotify
{
    UAnimSequence* Sequence = nullptr;
    FAnimNotifyEvent Notify;
    EAnimNotifyType Type = EAnimNotifyType::Trigger;
    float Weight = 1.0f;
    float DeltaTime = 0.0f;
};

struct FBoundAnimNotifyHandler
{
    FName NotifyName;
    UNotify* NotifyObject = nullptr;
    uint64 DelegateHandle = 0;
};

struct FActiveAnimNotifyState
{
    UAnimSequence* Sequence = nullptr;
    uint32 NotifyId = 0;
};

DECLARE_DELEGATE(FOnAnimNotify, const FAnimNotifyContext&)

UCLASS()
class UAnimInstance : public UObject
{
public:
    GENERATED_BODY(UAnimInstance, UObject)

public:
    virtual void InitializeAnimation(USkeletalMeshComponent* InOwningComponent);
    virtual void UninitializeAnimation();

    void UpdateAnimation(float DeltaTime);
    virtual bool EvaluateAnimation(const USkeletalMesh* SkeletalMesh, TArray<FMatrix>& OutLocalPose) const;

    void DispatchQueuedAnimEvents();

    USkeletalMeshComponent* GetOwningComponent() const { return OwningComponent; }

protected:
    virtual void NativeInitializeAnimation() {}
    virtual void NativeUninitializeAnimation() {}
    virtual void NativeUpdateAnimation(float DeltaTime) { (void)DeltaTime; }

    virtual void UpdateAnimGraph(float DeltaTime) { (void)DeltaTime; }

protected:
    void ResetNotifyQueue();

    void QueueAnimNotify(
        UAnimSequence* Sequence,
        const FAnimNotifyEvent& Notify,
        EAnimNotifyType Type,
        float Weight,
        float DeltaTime);

    void QueueSequenceNotifies(
        UAnimSequence* Sequence,
        float PreviousTime,
        float CurrentTime,
        bool bLoop,
        bool bLooped,
        bool bReverse,
        float Weight,
        float DeltaTime);

    bool IsNotifyStateActive(UAnimSequence* Sequence, const FAnimNotifyEvent& Notify) const;
    void AddActiveNotifyState(UAnimSequence* Sequence, const FAnimNotifyEvent& Notify);
    void RemoveActiveNotifyState(UAnimSequence* Sequence, const FAnimNotifyEvent& Notify);

protected:
    void EnsureNotifyHandlerBound(const FAnimNotifyEvent& Notify);
    void ClearBoundNotifyHandlers();

protected:
    TArray<FBoundAnimNotifyHandler> BoundNotifyHandlers;

public:
    FOnAnimNotify OnAnimNotify;

protected:
    USkeletalMeshComponent* OwningComponent = nullptr;

    TArray<FQueuedAnimNotify> QueuedAnimNotifies;
    TArray<FActiveAnimNotifyState> ActiveNotifyStates;
};