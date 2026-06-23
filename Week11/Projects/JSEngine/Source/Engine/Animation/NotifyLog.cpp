#include "NotifyLog.h"

#include "Core/Logging/Log.h"
#include "Object/ObjectFactory.h"

void UNotify_Log::OnTriggered(const FAnimNotifyContext& Context)
{
    (void)Context;
    UE_LOG("Notify triggered");
}

void UNotify_Log::OnStarted(const FAnimNotifyContext& Context)
{
    (void)Context;
    UE_LOG("Duration Notify Start!");
}

void UNotify_Log::OnTicked(const FAnimNotifyContext& Context)
{
    (void)Context;
    UE_LOG("Duration Notify Tick!");
}

void UNotify_Log::OnEnded(const FAnimNotifyContext& Context)
{
    (void)Context;
    UE_LOG("Duration Notify End!");
}