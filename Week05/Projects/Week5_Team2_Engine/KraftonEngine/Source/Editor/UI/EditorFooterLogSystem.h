#pragma once

#include "Core/CoreTypes.h"

class FEditorFooterLogSystem
{
public:
	void Tick(float DeltaTime);
	void Push(const FString& InMessage, float InLifetimeSeconds = 5.0f);
	TArray<FString> GetActiveMessages() const;

private:
	struct FLogEntry
	{
		FString Message;
		float RemainingSeconds = 0.0f;
	};

	TArray<FLogEntry> Entries;
};
