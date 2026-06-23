#pragma once

#include "Core/Types/CoreTypes.h"

struct FActorSequenceRoundTripSelfTestResult
{
	bool bPassed = false;
	int32 ChecksRun = 0;
	FString Message;
};

class FActorSequenceDiagnostics
{
public:
	static FActorSequenceRoundTripSelfTestResult RunRoundTripSelfTest();
};
