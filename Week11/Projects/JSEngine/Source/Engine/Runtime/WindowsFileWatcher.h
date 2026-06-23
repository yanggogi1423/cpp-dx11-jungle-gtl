#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/String.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/Map.h"
#include <windows.h>
#include <filesystem>
#include <chrono>
#include <functional>

using FClock = std::chrono::steady_clock;

struct FPendingChange
{
	FClock::time_point LastEventTime;
};

class FWindowsFileWatcher
{
public:
	using FShaderInvalidatedCallback = std::function<void(const FString&)>;

	bool Initialize(const FWString& InDirectory, bool bRecursive = true);
	void Tick();
	void Shutdown();
	void SetShaderInvalidatedCallback(FShaderInvalidatedCallback InCallback);

private:
	bool BeginRead();
	void ProcessNotifications(uint32 BytesTransferred);
	void ProcessPendingChanges();
	const char* ActionToString(uint32 Action) const;

	void OnFileEvent(const std::filesystem::path& Path);

private:
	FWString WatchedDirectory;
	HANDLE DirectoryHandle = INVALID_HANDLE_VALUE;
	HANDLE EventHandle = nullptr;
	OVERLAPPED Overlapped = {};
	TArray<uint8> Buffer;
	bool bRecursive = true;
	bool bPendingRead = false;

	TMap<FWString, FPendingChange> PendingChanges;
	FShaderInvalidatedCallback ShaderInvalidatedCallback;
};
