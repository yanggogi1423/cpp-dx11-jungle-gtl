#include "Notify.h"
#include "Engine/Animation/AnimSequence.h"
#include "Engine/Component/SkeletalMeshComponent.h"

void UNotify::ProcessNotify(const FAnimNotifyContext& Context)
{
    if (Context.Type == EAnimNotifyType::Trigger)
	{
        OnTriggered(Context);
        return;
    }

	if (Context.Notify.Duration <= 0.0f)
        return;

	switch (Context.Type)
	{
	case EAnimNotifyType::Start:
		OnStarted(Context);
		return;
	case EAnimNotifyType::Tick:
		OnTicked(Context);
        return;
	case EAnimNotifyType::End:
		OnEnded(Context);
        return;
	default:
        return;
    }
}
