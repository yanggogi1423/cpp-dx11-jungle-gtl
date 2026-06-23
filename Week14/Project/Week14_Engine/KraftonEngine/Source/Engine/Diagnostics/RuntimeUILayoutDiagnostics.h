#pragma once

#include "Core/Types/CoreTypes.h"

struct FRuntimeUILayoutSelfTestResult
{
	bool bPassed = false;
	int32 ChecksRun = 0;
	FString Message;
};

class FRuntimeUILayoutDiagnostics
{
public:
	static FRuntimeUILayoutSelfTestResult RunRoundTripSelfTest();
};
