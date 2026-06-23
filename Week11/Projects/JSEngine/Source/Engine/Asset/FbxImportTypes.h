#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"

struct FFbxMeshContentInfo
{
    bool bHasStaticMesh = false;
    bool bHasSkeletalMesh = false;
};

struct FFbxAnimationClipInfo
{
    FString Name;
    double StartSeconds = 0.0;
    double EndSeconds = 0.0;
    double DurationSeconds = 0.0;
    int32 AnimStackIndex = -1;
};
