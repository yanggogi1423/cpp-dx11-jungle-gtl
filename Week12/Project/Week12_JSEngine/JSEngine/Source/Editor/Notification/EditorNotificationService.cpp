#include "Editor/Notification/EditorNotificationService.h"

#include "Editor/EditorEngine.h"
#include "Core/Logging/Log.h"

#include "ImGui/imgui.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
constexpr int32 MaxToastCount = 6;
constexpr float DefaultToastDuration = 3.5f;
constexpr float ErrorToastDuration = 5.0f;
constexpr float TaskToastCompleteDuration = 2.0f;

float NormalizeProgress(float Progress)
{
	return Progress < 0.0f ? -1.0f : std::clamp(Progress, 0.0f, 1.0f);
}

float GetDurationForType(EEditorNotificationType Type)
{
	switch (Type)
	{
	case EEditorNotificationType::Error:
		return ErrorToastDuration;
	case EEditorNotificationType::Warning:
	case EEditorNotificationType::Info:
	default:
		return DefaultToastDuration;
	}
}

const char* GetToastTitle(EEditorNotificationType Type)
{
	switch (Type)
	{
	case EEditorNotificationType::Warning:
		return "Warning";
	case EEditorNotificationType::Error:
		return "Error";
	case EEditorNotificationType::Info:
	default:
		return "Info";
	}
}

ImVec4 GetAccentColor(EEditorNotificationType Type, float Alpha)
{
	switch (Type)
	{
	case EEditorNotificationType::Warning:
		return ImVec4(0.95f, 0.64f, 0.18f, Alpha);
	case EEditorNotificationType::Error:
		return ImVec4(0.92f, 0.22f, 0.20f, Alpha);
	case EEditorNotificationType::Info:
	default:
		return ImVec4(0.30f, 0.62f, 0.95f, Alpha);
	}
}
}

void FEditorNotificationService::Initialize(UEditorEngine* InEditorEngine)
{
	EditorEngine = InEditorEngine;
}

void FEditorNotificationService::Info(const FString& Message) const
{
	Notify(EEditorNotificationType::Info, Message);
}

void FEditorNotificationService::Warning(const FString& Message) const
{
	Notify(EEditorNotificationType::Warning, Message);
}

void FEditorNotificationService::Error(const FString& Message) const
{
	Notify(EEditorNotificationType::Error, Message);
}

void FEditorNotificationService::Notify(EEditorNotificationType Type, const FString& Message) const
{
	if (Message.empty())
	{
		return;
	}

	switch (Type)
	{
	case EEditorNotificationType::Warning:
		UE_LOG_WARNING("[EditorNotification] %s", Message.c_str());
		break;
	case EEditorNotificationType::Error:
		UE_LOG_ERROR("[EditorNotification] %s", Message.c_str());
		break;
	case EEditorNotificationType::Info:
	default:
		UE_LOG("[EditorNotification] %s", Message.c_str());
		break;
	}

	PushToast(Type, Message, GetDurationForType(Type));
}

void FEditorNotificationService::PushToast(EEditorNotificationType Type, const FString& Message, float Duration) const
{
	if (Message.empty() || Duration <= 0.0f)
	{
		return;
	}

	std::lock_guard<std::mutex> Lock(ToastMutex);
	FToast Toast;
	Toast.Message = Message;
	Toast.Type = Type;
	Toast.Duration = Duration;
	Toasts.push_back(std::move(Toast));
	while (Toasts.size() > MaxToastCount)
	{
		Toasts.erase(Toasts.begin());
	}
}

FEditorNotificationService::FToast* FEditorNotificationService::FindToast(uint64 Id) const
{
	if (Id == 0)
	{
		return nullptr;
	}

	for (FToast& Toast : Toasts)
	{
		if (Toast.Id == Id)
		{
			return &Toast;
		}
	}
	return nullptr;
}

FEditorNotificationHandle FEditorNotificationService::BeginTask(
	const FString& Title,
	const FString& Message,
	float Progress) const
{
	if (Title.empty() && Message.empty())
	{
		return {};
	}

	std::lock_guard<std::mutex> Lock(ToastMutex);
	FToast Toast;
	Toast.Id = NextToastId++;
	Toast.Title = Title.empty() ? "Working" : Title;
	Toast.Message = Message;
	Toast.Type = EEditorNotificationType::Info;
	Toast.Duration = TaskToastCompleteDuration;
	Toast.Progress = NormalizeProgress(Progress);
	Toast.bTask = true;
	Toasts.push_back(std::move(Toast));
	while (Toasts.size() > MaxToastCount)
	{
		Toasts.erase(Toasts.begin());
	}

	return { Toasts.back().Id };
}

void FEditorNotificationService::UpdateTask(
	FEditorNotificationHandle Handle,
	const FString& Message,
	float Progress) const
{
	std::lock_guard<std::mutex> Lock(ToastMutex);
	FToast* Toast = FindToast(Handle.Id);
	if (!Toast || Toast->bCompleted)
	{
		return;
	}

	if (!Message.empty())
	{
		Toast->Message = Message;
	}
	Toast->Progress = NormalizeProgress(Progress);
}

void FEditorNotificationService::FinishTask(
	FEditorNotificationHandle Handle,
	EEditorNotificationType Type,
	const FString& Message) const
{
	if (!Handle.IsValid())
	{
		Notify(Type, Message);
		return;
	}

	std::lock_guard<std::mutex> Lock(ToastMutex);
	FToast* Toast = FindToast(Handle.Id);
	if (!Toast)
	{
		return;
	}

	Toast->Type = Type;
	Toast->Title = GetToastTitle(Type);
	Toast->Message = Message;
	Toast->Progress = Type == EEditorNotificationType::Error ? -1.0f : 1.0f;
	Toast->Duration = Type == EEditorNotificationType::Error ? ErrorToastDuration : TaskToastCompleteDuration;
	Toast->ElapsedTime = 0.0f;
	Toast->bCompleted = true;
}

void FEditorNotificationService::RenderToasts(float DeltaTime)
{
	std::lock_guard<std::mutex> Lock(ToastMutex);
	if (Toasts.empty())
	{
		return;
	}

	const float SafeDeltaTime = DeltaTime > 0.0f ? DeltaTime : ImGui::GetIO().DeltaTime;
	for (FToast& Toast : Toasts)
	{
		Toast.ElapsedTime += std::max(0.0f, SafeDeltaTime);
	}

	Toasts.erase(
		std::remove_if(
			Toasts.begin(),
			Toasts.end(),
			[](const FToast& Toast)
			{
				return (!Toast.bTask || Toast.bCompleted) && Toast.ElapsedTime >= Toast.Duration;
			}),
		Toasts.end());

	if (Toasts.empty())
	{
		return;
	}

	ImGuiViewport* Viewport = ImGui::GetMainViewport();
	if (!Viewport)
	{
		return;
	}

	ImDrawList* DrawList = ImGui::GetForegroundDrawList(Viewport);
	constexpr float Padding = 18.0f;
	constexpr float FooterReserve = 42.0f;
	constexpr float ToastMaxWidth = 420.0f;
	constexpr float ToastMinWidth = 240.0f;
	constexpr float ToastPadX = 14.0f;
	constexpr float ToastPadY = 11.0f;
	constexpr float TitleGap = 4.0f;
	constexpr float Spacing = 8.0f;
	constexpr float FadeTime = 0.22f;
	constexpr float Rounding = 7.0f;

	float OffsetY = Viewport->WorkPos.y + Viewport->WorkSize.y - Padding - FooterReserve;
	for (int32 Index = static_cast<int32>(Toasts.size()) - 1; Index >= 0; --Index)
	{
		const FToast& Toast = Toasts[Index];
		float Alpha = 1.0f;
		if (Toast.bTask && !Toast.bCompleted)
		{
			Alpha = 1.0f;
		}
		else if (Toast.ElapsedTime < FadeTime)
		{
			Alpha = Toast.ElapsedTime / FadeTime;
		}
		else if (Toast.Duration - Toast.ElapsedTime < FadeTime)
		{
			Alpha = (Toast.Duration - Toast.ElapsedTime) / FadeTime;
		}
		Alpha = std::clamp(Alpha, 0.0f, 1.0f);

		const FString Title = Toast.bTask && !Toast.Title.empty()
			? Toast.Title
			: FString(GetToastTitle(Toast.Type));
		const ImVec2 TitleSize = ImGui::CalcTextSize(Title.c_str());
		const ImVec2 TextSize = ImGui::CalcTextSize(
			Toast.Message.c_str(),
			nullptr,
			false,
			ToastMaxWidth - ToastPadX * 2.0f);

		const float ToastW = std::clamp(
			std::max(TitleSize.x, TextSize.x) + ToastPadX * 2.0f,
			ToastMinWidth,
			ToastMaxWidth);
		const bool bHasProgress = Toast.Progress >= 0.0f;
		const float ProgressHeight = bHasProgress ? 8.0f : 0.0f;
		const float ProgressGap = bHasProgress ? 9.0f : 0.0f;
		const float ToastH = TitleSize.y + TitleGap + TextSize.y + ProgressGap + ProgressHeight + ToastPadY * 2.0f;
		OffsetY -= ToastH;

		const float PosX = Viewport->WorkPos.x + Viewport->WorkSize.x - ToastW - Padding;
		const float PosY = OffsetY;
		const ImVec2 Min(PosX, PosY);
		const ImVec2 Max(PosX + ToastW, PosY + ToastH);

		const ImU32 BgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.075f, 0.083f, 0.10f, Alpha * 0.95f));
		const ImU32 BorderColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.24f, 0.27f, 0.32f, Alpha));
		const ImU32 AccentColor = ImGui::ColorConvertFloat4ToU32(GetAccentColor(Toast.Type, Alpha));
		const ImU32 TitleColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.92f, 0.95f, 1.0f, Alpha));
		const ImU32 TextColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.78f, 0.82f, 0.90f, Alpha));

		DrawList->AddRectFilled(Min, Max, BgColor, Rounding);
		DrawList->AddRect(Min, Max, BorderColor, Rounding);
		DrawList->AddRectFilled(Min, ImVec2(Min.x + 4.0f, Max.y), AccentColor, Rounding, ImDrawFlags_RoundCornersLeft);
		DrawList->AddText(
			ImVec2(PosX + ToastPadX, PosY + ToastPadY),
			TitleColor,
			Title.c_str());
		DrawList->AddText(
			nullptr,
			0.0f,
			ImVec2(PosX + ToastPadX, PosY + ToastPadY + TitleSize.y + TitleGap),
			TextColor,
			Toast.Message.c_str(),
			nullptr,
			ToastMaxWidth - ToastPadX * 2.0f);

		if (bHasProgress)
		{
			const float ClampedProgress = std::clamp(Toast.Progress, 0.0f, 1.0f);
			const float BarX = PosX + ToastPadX;
			const float BarY = PosY + ToastPadY + TitleSize.y + TitleGap + TextSize.y + ProgressGap;
			const float BarW = ToastW - ToastPadX * 2.0f;
			const ImVec2 BarMin(BarX, BarY);
			const ImVec2 BarMax(BarX + BarW, BarY + ProgressHeight);
			DrawList->AddRectFilled(
				BarMin,
				BarMax,
				ImGui::ColorConvertFloat4ToU32(ImVec4(0.10f, 0.12f, 0.15f, Alpha)),
				4.0f);
			const float FillW = std::floor(BarW * ClampedProgress);
			if (FillW > 0.0f)
			{
				const float FillRounding = std::min(4.0f, FillW * 0.5f);
				DrawList->AddRectFilled(
					BarMin,
					ImVec2(std::min(BarMin.x + FillW, BarMax.x), BarMax.y),
					AccentColor,
					FillRounding,
					FillW >= BarW - 0.5f ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
			}
		}

		OffsetY -= Spacing;
	}
}
