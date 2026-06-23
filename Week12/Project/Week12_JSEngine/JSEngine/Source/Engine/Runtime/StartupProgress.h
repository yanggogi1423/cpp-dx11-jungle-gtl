#pragma once

#include "Core/Containers/String.h"

class IStartupProgressReporter
{
public:
	virtual ~IStartupProgressReporter() = default;
	virtual void Report(const FString& Message, float Progress) = 0;
};

class FNullStartupProgressReporter final : public IStartupProgressReporter
{
public:
	void Report(const FString& Message, float Progress) override
	{
		(void)Message;
		(void)Progress;
	}
};
