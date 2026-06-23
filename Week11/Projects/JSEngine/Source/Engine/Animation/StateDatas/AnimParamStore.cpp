#include "Pch.h"
#include "AnimParamStore.h"

void FAnimParamStore::SetFloat(FName Name, float Value)
{
    FloatParams[Name] = Value;
}

void FAnimParamStore::SetBool(FName Name, bool Value)
{
    BoolParams[Name] = Value;
}

void FAnimParamStore::SetInt(FName Name, int32 Value)
{
    IntParams[Name] = Value;
}

bool FAnimParamStore::TryGetFloat(FName Name, float& OutValue) const
{
    const auto It = FloatParams.find(Name);
    if (It == FloatParams.end())
    {
        return false;
    }

    OutValue = It->second;
    return true;
}

bool FAnimParamStore::TryGetBool(FName Name, bool& OutValue) const
{
    const auto It = BoolParams.find(Name);
    if (It == BoolParams.end())
    {
        return false;
    }

    OutValue = It->second;
    return true;
}

bool FAnimParamStore::TryGetInt(FName Name, int32& OutValue) const
{
    const auto It = IntParams.find(Name);
    if (It == IntParams.end())
    {
        return false;
    }

    OutValue = It->second;
    return true;
}
