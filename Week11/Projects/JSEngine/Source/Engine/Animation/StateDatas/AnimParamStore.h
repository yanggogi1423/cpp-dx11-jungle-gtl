#pragma once
#include "Core/Containers/Map.h"
#include "Object/FName.h"

struct FAnimParamStore
{
    TMap<FName, float, FName::Hash> FloatParams;
    TMap<FName, bool, FName::Hash> BoolParams;
    TMap<FName, int32, FName::Hash> IntParams;

    void SetFloat(FName Name, float Value);
    void SetBool(FName Name, bool Value);
    void SetInt(FName Name, int32 Value);

    bool TryGetFloat(FName Name, float& OutValue) const;
    bool TryGetBool(FName Name, bool& OutValue) const;
    bool TryGetInt(FName Name, int32& OutValue) const;
};
