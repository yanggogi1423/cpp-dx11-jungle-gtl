#pragma once

#include "Core/CoreMinimal.h"

#include <mutex>

class UEditorEngine;

enum class EEditorNotificationType : uint8
{
	Info,
	Warning,
	Error,
};

struct FEditorNotificationHandle
{
	uint64 Id = 0;

	bool IsValid() const { return Id != 0; }
};

class FEditorNotificationService
{
public:
	void Initialize(UEditorEngine* InEditorEngine);

	void Info(const FString& Message) const;
	void Warning(const FString& Message) const;
	void Error(const FString& Message) const;
	void Notify(EEditorNotificationType Type, const FString& Message) const;
	FEditorNotificationHandle BeginTask(const FString& Title, const FString& Message, float Progress = -1.0f) const;
	void UpdateTask(FEditorNotificationHandle Handle, const FString& Message, float Progress = -1.0f) const;
	void FinishTask(FEditorNotificationHandle Handle, EEditorNotificationType Type, const FString& Message) const;
	void RenderToasts(float DeltaTime);

private:
	struct FToast
	{
		uint64 Id = 0;
		FString Title;
		FString Message;
		EEditorNotificationType Type = EEditorNotificationType::Info;
		float Duration = 3.0f;
		float ElapsedTime = 0.0f;
		float Progress = -1.0f;
		bool bTask = false;
		bool bCompleted = false;
	};

	void PushToast(EEditorNotificationType Type, const FString& Message, float Duration) const;
	FToast* FindToast(uint64 Id) const;

	UEditorEngine* EditorEngine = nullptr;
	mutable std::mutex ToastMutex;
	mutable TArray<FToast> Toasts;
	mutable uint64 NextToastId = 1;
};
