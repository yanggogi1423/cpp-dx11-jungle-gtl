#pragma once

#include "Core/Types/CoreTypes.h"

struct FGameViewportInputSelfTestResult
{
	bool bPassed = false;
	int32 ChecksRun = 0;
	FString Message;
};

class FGameViewportInputDiagnostics
{
public:
	static FGameViewportInputSelfTestResult RunSelfTest();
};
