#include "AnimInstance.h"

#include "Asset/SkeletalMesh.h"
#include "Component/SkeletalMeshComponent.h"

#include <algorithm>

namespace
{
bool IsSameNotifyState(
    const FActiveAnimNotifyState& State,
    UAnimSequence* Sequence,
    const FAnimNotifyEvent& Notify)
{
    return State.Sequence == Sequence && State.NotifyId == Notify.NotifyId;
}

bool IsDuplicateQueuedNotify(
    const TArray<FQueuedAnimNotify>& Queue,
    UAnimSequence* Sequence,
    const FAnimNotifyEvent& Notify,
    EAnimNotifyType Type)
{
    return std::any_of(
        Queue.begin(),
        Queue.end(),
        [Sequence, &Notify, Type](const FQueuedAnimNotify& Existing)
        {
            return Existing.Sequence == Sequence && Existing.Notify.NotifyId == Notify.NotifyId && Existing.Type == Type;
        });
}
} // namespace

void UAnimInstance::InitializeAnimation(USkeletalMeshComponent* InOwningComponent)
{
    OwningComponent = InOwningComponent;
    ResetNotifyQueue();
    ActiveNotifyStates.clear();

    NativeInitializeAnimation();
}

void UAnimInstance::UninitializeAnimation()
{
    NativeUninitializeAnimation();

    ClearBoundNotifyHandlers();

    ResetNotifyQueue();
    ActiveNotifyStates.clear();
    OwningComponent = nullptr;
}

void UAnimInstance::UpdateAnimation(float DeltaTime)
{
    ResetNotifyQueue();

    NativeUpdateAnimation(DeltaTime);
    UpdateAnimGraph(DeltaTime);
}

bool UAnimInstance::EvaluateAnimation(
    const USkeletalMesh* SkeletalMesh,
    TArray<FMatrix>& OutLocalPose) const
{
    if (!SkeletalMesh || !SkeletalMesh->HasValidMeshData())
    {
        return false;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMesh->GetBones();
    if (Bones.empty())
    {
        return false;
    }

    OutLocalPose.resize(Bones.size());

    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
    {
        OutLocalPose[BoneIndex] = Bones[BoneIndex].LocalBindTransform;
    }

    return true;
}

void UAnimInstance::ResetNotifyQueue()
{
    QueuedAnimNotifies.clear();
}

void UAnimInstance::QueueAnimNotify(
    UAnimSequence* Sequence,
    const FAnimNotifyEvent& Notify,
    EAnimNotifyType Type,
    float Weight,
    float DeltaTime)
{
    if (!Sequence)
    {
        return;
    }

    if (Weight < Notify.TriggerWeightThreshold)
    {
        return;
    }

    if (IsDuplicateQueuedNotify(QueuedAnimNotifies, Sequence, Notify, Type))
    {
        return;
    }

    EnsureNotifyHandlerBound(Notify);

    FQueuedAnimNotify Queued;
    Queued.Sequence = Sequence;
    Queued.Notify = Notify;
    Queued.Type = Type;
    Queued.Weight = Weight;
    Queued.DeltaTime = DeltaTime;

    QueuedAnimNotifies.push_back(Queued);
}

void UAnimInstance::QueueSequenceNotifies(
    UAnimSequence* Sequence,
    float PreviousTime,
    float CurrentTime,
    bool bLoop,
    bool bLooped,
    bool bReverse,
    float Weight,
    float DeltaTime)
{
    if (!Sequence)
    {
        return;
    }

    TArray<FAnimNotifyEvent> CrossedNotifies;
    Sequence->GetAnimNotifiesFromDeltaPositions(
        PreviousTime,
        CurrentTime,
        bLoop,
        bLooped,
        bReverse,
        CrossedNotifies);

    for (const FAnimNotifyEvent& Notify : CrossedNotifies)
    {
        if (Notify.Duration > 0.0f)
        {
            QueueAnimNotify(Sequence, Notify, EAnimNotifyType::Start, Weight, DeltaTime);
            AddActiveNotifyState(Sequence, Notify);
        }
        else
        {
            QueueAnimNotify(Sequence, Notify, EAnimNotifyType::Trigger, Weight, DeltaTime);
        }
    }
    //모든 Notify중 Duration Notify만? ??? 뭥미 왜하는거임?
    for (const FAnimNotifyEvent& Notify : Sequence->GetNotifyEvents())
    {
        if (Notify.Duration <= 0.0f)
        {
            continue;
        }

        const bool bActiveNow = Sequence->IsNotifyStateActiveAtTime(Notify, CurrentTime);
        const bool bWasActive = IsNotifyStateActive(Sequence, Notify);

        if (bActiveNow)
        {
            if (!bWasActive)
            {
                QueueAnimNotify(Sequence, Notify, EAnimNotifyType::Start, Weight, DeltaTime);
                AddActiveNotifyState(Sequence, Notify);
            }

            QueueAnimNotify(Sequence, Notify, EAnimNotifyType::Tick, Weight, DeltaTime);
        }
        else if (bWasActive)
        {
            QueueAnimNotify(Sequence, Notify, EAnimNotifyType::End, 1.0f, DeltaTime);
            RemoveActiveNotifyState(Sequence, Notify);
        }
    }

    TArray<FAnimNotifyEvent> EndedNotifies;
    Sequence->GetAnimNotifyStateEndsFromDeltaPositions(
        PreviousTime,
        CurrentTime,
        bLoop,
        bLooped,
        bReverse,
        EndedNotifies);

    for (const FAnimNotifyEvent& Notify : EndedNotifies)
    {
        if (IsNotifyStateActive(Sequence, Notify))
        {
            QueueAnimNotify(Sequence, Notify, EAnimNotifyType::End, 1.0f, DeltaTime);
            RemoveActiveNotifyState(Sequence, Notify);
        }
    }
}

bool UAnimInstance::IsNotifyStateActive(
    UAnimSequence* Sequence,
    const FAnimNotifyEvent& Notify) const
{
    return std::any_of(
        ActiveNotifyStates.begin(),
        ActiveNotifyStates.end(),
        [Sequence, &Notify](const FActiveAnimNotifyState& State)
        {
            return IsSameNotifyState(State, Sequence, Notify);
        });
}

void UAnimInstance::AddActiveNotifyState(
    UAnimSequence* Sequence,
    const FAnimNotifyEvent& Notify)
{
    if (IsNotifyStateActive(Sequence, Notify))
    {
        return;
    }

    FActiveAnimNotifyState State;
    State.Sequence = Sequence;
    State.NotifyId = Notify.NotifyId;

    ActiveNotifyStates.push_back(State);
}

void UAnimInstance::RemoveActiveNotifyState(
    UAnimSequence* Sequence,
    const FAnimNotifyEvent& Notify)
{
    ActiveNotifyStates.erase(
        std::remove_if(
            ActiveNotifyStates.begin(),
            ActiveNotifyStates.end(),
            [Sequence, &Notify](const FActiveAnimNotifyState& State)
            {
                return IsSameNotifyState(State, Sequence, Notify);
            }),
        ActiveNotifyStates.end());
}

void UAnimInstance::DispatchQueuedAnimEvents()
{
    if (!OwningComponent)
    {
        ResetNotifyQueue();
        return;
    }

    TArray<FQueuedAnimNotify> NotifiesToDispatch = QueuedAnimNotifies;
    ResetNotifyQueue();

    for (const FQueuedAnimNotify& Queued : NotifiesToDispatch)
    {
        FAnimNotifyContext Context;
        Context.Component = OwningComponent;
        Context.Sequence = Queued.Sequence;
        Context.Notify = Queued.Notify;
        Context.Type = Queued.Type;

        OnAnimNotify.Broadcast(Context);
    }
}

void UAnimInstance::EnsureNotifyHandlerBound(const FAnimNotifyEvent& Notify)
{
    if (!OwningComponent)
    {
        return;
    }

    const FString NotifyTypeName = Notify.NotifyName.ToString();
    if (NotifyTypeName.empty())
    {
        return;
    }

    for (const FBoundAnimNotifyHandler& Handler : BoundNotifyHandlers)
    {
        if (Handler.NotifyName.ToString() == NotifyTypeName)
        {
            return;
        }
    }

    UObject* NewObject = FObjectFactory::Get().Create(NotifyTypeName);
    UNotify* NotifyObject = Cast<UNotify>(NewObject);

    if (!NotifyObject)
    {
        if (NewObject)
        {
            UObjectManager::Get().DestroyObject(NewObject);
        }

        UE_LOG_ERROR("[AnimNotify] Failed to create notify object: %s", NotifyTypeName.c_str());
        return;
    }

    const uint64 Handle = OnAnimNotify.Add(
        [NotifyObject, NotifyTypeName](const FAnimNotifyContext& Context)
        {
            if (!NotifyObject)
            {
                return;
            }

            if (Context.Notify.NotifyName.ToString() != NotifyTypeName)
            {
                return;
            }

            NotifyObject->ProcessNotify(Context);
        });

    FBoundAnimNotifyHandler Handler;
    Handler.NotifyName = Notify.NotifyName;
    Handler.NotifyObject = NotifyObject;
    Handler.DelegateHandle = Handle;

    BoundNotifyHandlers.push_back(Handler);
}

void UAnimInstance::ClearBoundNotifyHandlers()
{
    if (OwningComponent)
    {
        for (const FBoundAnimNotifyHandler& Handler : BoundNotifyHandlers)
        {
            if (Handler.DelegateHandle != 0)
            {
                OnAnimNotify.Remove(Handler.DelegateHandle);
            }
        }
    }

    for (const FBoundAnimNotifyHandler& Handler : BoundNotifyHandlers)
    {
        if (Handler.NotifyObject)
        {
            UObjectManager::Get().DestroyObject(Handler.NotifyObject);
        }
    }

    BoundNotifyHandlers.clear();
}