#include "Editor/UI/Panel/EditorProjectSettingsWidget.h"
#include "Core/ProjectSettings.h"
#include "Core/Logging/Notification.h"
#include "Platform/Paths.h"
#include "Serialization/SceneSaveManager.h"
#include "GameFramework/GameMode/GameModeBase.h"
#include "Object/Reflection/UClass.h"
#include "ImGui/imgui.h"

#include <Shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
	std::filesystem::path GetProjectRootPath()
	{
		return std::filesystem::path(FPaths::RootDir()).lexically_normal();
	}

	bool IsPathInsideProject(const std::filesystem::path& Path)
	{
		const std::filesystem::path ProjectRoot = GetProjectRootPath();
		const std::filesystem::path RelPath = Path.lexically_normal().lexically_relative(ProjectRoot);
		if (RelPath.empty())
		{
			return true;
		}

		if (RelPath.is_absolute())
		{
			return false;
		}

		for (const std::filesystem::path& Part : RelPath)
		{
			if (Part == L"..")
			{
				return false;
			}
		}

		return true;
	}

	bool ResolvePrefabPathForValidation(const FString& Path, std::filesystem::path& OutPath)
	{
		if (Path.empty())
		{
			return false;
		}

		std::filesystem::path Candidate(FPaths::ToWide(Path));
		if (Candidate.extension().empty())
		{
			Candidate += L".prefab";
		}

		if (!Candidate.is_absolute())
		{
			if (Candidate.parent_path().empty())
			{
				Candidate = std::filesystem::path(FPaths::AssetDir()) / L"Prefab" / Candidate;
			}
			else
			{
				Candidate = GetProjectRootPath() / Candidate;
			}
		}

		Candidate = Candidate.lexically_normal();
		if (!IsPathInsideProject(Candidate))
		{
			return false;
		}

		OutPath = Candidate;
		return true;
	}

	bool SceneExistsByName(const FString& SceneName)
	{
		for (const FString& SceneFile : FSceneSaveManager::GetSceneFileList())
		{
			if (SceneFile == SceneName)
			{
				return true;
			}
		}

		return false;
	}

	bool ValidatePackagingSettings(const FProjectSettings& PS, FString& OutMessage)
	{
		const std::filesystem::path ProjectRoot = GetProjectRootPath();
		const std::filesystem::path GameBuildScript = ProjectRoot / L"GameBuild.bat";
		const std::filesystem::path PackageReleaseScript = ProjectRoot / L"PackageRelease.bat";

		if (!std::filesystem::exists(GameBuildScript))
		{
			OutMessage = "Packaging validation failed: GameBuild.bat was not found.";
			return false;
		}

		if (!std::filesystem::exists(PackageReleaseScript))
		{
			OutMessage = "Packaging validation failed: PackageRelease.bat was not found.";
			return false;
		}

		if (PS.Build.bValidateStartupScene)
		{
			if (PS.Game.StartLevelName.empty())
			{
				OutMessage = "Packaging validation failed: Start Level is empty.";
				return false;
			}

			if (!SceneExistsByName(PS.Game.StartLevelName))
			{
				OutMessage = "Packaging validation failed: Start Level scene was not found in Content/Scene.";
				return false;
			}
		}

		if (PS.Build.bValidateDefaultPawnPrefab && !PS.Game.DefaultPawnPrefabPath.empty())
		{
			std::filesystem::path PrefabPath;
			if (!ResolvePrefabPathForValidation(PS.Game.DefaultPawnPrefabPath, PrefabPath))
			{
				OutMessage = "Packaging validation failed: Default Pawn Prefab path is outside the project.";
				return false;
			}

			if (!std::filesystem::exists(PrefabPath))
			{
				OutMessage = "Packaging validation failed: Default Pawn Prefab was not found.";
				return false;
			}
		}

		OutMessage = "Packaging validation succeeded.";
		return true;
	}

	bool LaunchProjectBatch(const wchar_t* ScriptName, const FString& Args, FString& OutMessage)
	{
		const std::filesystem::path ProjectRoot = GetProjectRootPath();
		const std::filesystem::path ScriptPath = ProjectRoot / ScriptName;
		if (!std::filesystem::exists(ScriptPath))
		{
			OutMessage = "Batch script was not found.";
			return false;
		}

		const std::wstring WideArgs = FPaths::ToWide(Args);
		HINSTANCE Result = ShellExecuteW(
			nullptr,
			L"open",
			ScriptPath.c_str(),
			WideArgs.empty() ? nullptr : WideArgs.c_str(),
			ProjectRoot.c_str(),
			SW_SHOWNORMAL);

		if (reinterpret_cast<intptr_t>(Result) <= 32)
		{
			OutMessage = "Failed to launch batch script.";
			return false;
		}

		OutMessage = "Batch script launched.";
		return true;
	}

	void AppendBatchArg(FString& Args, const FString& Arg)
	{
		if (Arg.empty())
		{
			return;
		}

		if (!Args.empty())
		{
			Args += " ";
		}
		Args += Arg;
	}

	FString BuildPackageReleaseArgs(const FProjectSettings& PS, bool bDryRun)
	{
		FString Args;
		AppendBatchArg(Args, PS.Build.PackageVersionName);
		if (bDryRun)
		{
			AppendBatchArg(Args, "--dry-run");
		}
		if (PS.Build.bLaunchSmokeTest)
		{
			AppendBatchArg(Args, "--launch-smoke");
			AppendBatchArg(Args, "--launch-smoke-timeout");
			AppendBatchArg(Args, std::to_string((std::max)(1, (std::min)(PS.Build.LaunchSmokeTimeoutSeconds, 60))));
		}
		return Args;
	}

	void NotifyPackagingResult(bool bSuccess, const FString& Message)
	{
		FNotificationManager::Get().AddNotification(
			Message,
			bSuccess ? ENotificationType::Success : ENotificationType::Error,
			bSuccess ? 3.0f : 5.0f);
	}
}

void EditorProjectSettingsWidget::Render()
{
	if (!bOpen) return;

	ImGui::SetNextWindowSize(ImVec2(360, 200), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Project Settings", &bOpen))
	{
		ImGui::End();
		return;
	}

	FProjectSettings& PS = FProjectSettings::Get();

	if (ImGui::CollapsingHeader("Game", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Scene 파일 목록을 콤보박스로 표시
		TArray<FString> SceneFiles = FSceneSaveManager::GetSceneFileList();

		int CurrentIdx = -1;
		for (int i = 0; i < static_cast<int>(SceneFiles.size()); ++i)
		{
			if (SceneFiles[i] == PS.Game.StartLevelName)
			{
				CurrentIdx = i;
				break;
			}
		}

		const char* Preview = CurrentIdx >= 0 ? SceneFiles[CurrentIdx].c_str() : "(None)";
		if (ImGui::BeginCombo("Start Level", Preview))
		{
			for (int i = 0; i < static_cast<int>(SceneFiles.size()); ++i)
			{
				bool bSelected = (i == CurrentIdx);
				if (ImGui::Selectable(SceneFiles[i].c_str(), bSelected))
				{
					PS.Game.StartLevelName = SceneFiles[i];
				}
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		// GameMode 클래스 — UClass 레지스트리에서 AGameModeBase 파생만 필터링.
		// 첫 항목은 "(Default)"로, 빈 문자열에 매핑 — GameEngine이 코드 디폴트 사용.
		TArray<UClass*> GameModeClasses;
		GameModeClasses.push_back(nullptr); // sentinel for "(Default)"
		for (UClass* C : UClass::GetAllClasses())
		{
			if (C && C->IsA(AGameModeBase::StaticClass()))
				GameModeClasses.push_back(C);
		}

		int GMIdx = 0;
		for (int i = 1; i < static_cast<int>(GameModeClasses.size()); ++i)
		{
			if (PS.Game.GameModeClassName == GameModeClasses[i]->GetName())
			{
				GMIdx = i;
				break;
			}
		}

		const char* GMPreview = (GMIdx == 0) ? "(Default)" : GameModeClasses[GMIdx]->GetName();
		if (ImGui::BeginCombo("GameMode Class", GMPreview))
		{
			for (int i = 0; i < static_cast<int>(GameModeClasses.size()); ++i)
			{
				const char* Label = (i == 0) ? "(Default)" : GameModeClasses[i]->GetName();
				bool bSelected = (i == GMIdx);
				if (ImGui::Selectable(Label, bSelected))
				{
					PS.Game.GameModeClassName = (i == 0) ? FString() : FString(GameModeClasses[i]->GetName());
				}
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		char DefaultPawnPrefabPath[512] = {};
		strncpy_s(DefaultPawnPrefabPath, PS.Game.DefaultPawnPrefabPath.c_str(), _TRUNCATE);
		if (ImGui::InputText("Default Pawn Prefab", DefaultPawnPrefabPath, sizeof(DefaultPawnPrefabPath)))
		{
			PS.Game.DefaultPawnPrefabPath = DefaultPawnPrefabPath;
		}
		ImGui::TextDisabled("Requires scene reload to take effect.");
	}

	if (ImGui::CollapsingHeader("Packaging", ImGuiTreeNodeFlags_DefaultOpen))
	{
		char PackageVersionName[128] = {};
		strncpy_s(PackageVersionName, PS.Build.PackageVersionName.c_str(), _TRUNCATE);
		if (ImGui::InputText("Version Name", PackageVersionName, sizeof(PackageVersionName)))
		{
			PS.Build.PackageVersionName = PackageVersionName;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Passed to PackageRelease.bat. Empty uses the script's timestamp prompt.");
		}

		ImGui::Checkbox("Validate Startup Scene", &PS.Build.bValidateStartupScene);
		ImGui::Checkbox("Validate Default Pawn Prefab", &PS.Build.bValidateDefaultPawnPrefab);
		ImGui::Checkbox("Launch Smoke Test", &PS.Build.bLaunchSmokeTest);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("After packaging, launch the packaged executable briefly and fail if it exits with an error.");
		}
		int LaunchSmokeTimeoutSeconds = PS.Build.LaunchSmokeTimeoutSeconds;
		if (ImGui::InputInt("Launch Smoke Timeout", &LaunchSmokeTimeoutSeconds))
		{
			PS.Build.LaunchSmokeTimeoutSeconds = (std::max)(1, (std::min)(LaunchSmokeTimeoutSeconds, 60));
		}

		FString ValidationMessage;
		if (ImGui::Button("Validate Package"))
		{
			const bool bValid = ValidatePackagingSettings(PS, ValidationMessage);
			NotifyPackagingResult(bValid, ValidationMessage);
		}

		ImGui::SameLine();
		if (ImGui::Button("Run Game Build"))
		{
			const bool bValid = ValidatePackagingSettings(PS, ValidationMessage);
			if (!bValid)
			{
				NotifyPackagingResult(false, ValidationMessage);
			}
			else
			{
				FString LaunchMessage;
				const bool bLaunched = LaunchProjectBatch(L"GameBuild.bat", "", LaunchMessage);
				NotifyPackagingResult(bLaunched, bLaunched ? "GameBuild.bat launched." : LaunchMessage);
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Run Package Release"))
		{
			const bool bValid = ValidatePackagingSettings(PS, ValidationMessage);
			if (!bValid)
			{
				NotifyPackagingResult(false, ValidationMessage);
			}
			else
			{
				FString LaunchMessage;
				const bool bLaunched = LaunchProjectBatch(L"PackageRelease.bat", BuildPackageReleaseArgs(PS, false), LaunchMessage);
				NotifyPackagingResult(bLaunched, bLaunched ? "PackageRelease.bat launched." : LaunchMessage);
			}
		}

		if (ImGui::Button("Dry Run Package"))
		{
			const bool bValid = ValidatePackagingSettings(PS, ValidationMessage);
			if (!bValid)
			{
				NotifyPackagingResult(false, ValidationMessage);
			}
			else
			{
				FString LaunchMessage;
				const bool bLaunched = LaunchProjectBatch(L"PackageRelease.bat", BuildPackageReleaseArgs(PS, true), LaunchMessage);
				NotifyPackagingResult(bLaunched, bLaunched ? "Package dry run launched." : LaunchMessage);
			}
		}

		ImGui::TextDisabled("Runs existing project-root build/package scripts.");
		ImGui::TextDisabled("Outputs: GameBuild/ and ReleaseBuild/.");
	}

    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Async Physics", &PS.Physics.bAsyncPhysics);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("ON: 1-frame latency, full overlap with render.\nOFF: wait for physics before render.");

        float fixedHz = 1.0f / PS.Physics.FixedTimeStep;
        if (ImGui::SliderFloat("Fixed Step Hz", &fixedHz, 1.0f, 240.0f, "%.0f"))
        {
            PS.Physics.FixedTimeStep = 1.0f / fixedHz;
            PS.Physics.MaxSimulationSubstepDeltaTime = (std::min)(PS.Physics.MaxSimulationSubstepDeltaTime, PS.Physics.FixedTimeStep);
        }

        float simSubstepHz = 1.0f / PS.Physics.MaxSimulationSubstepDeltaTime;
        const float minSimSubstepHz = fixedHz;
        if (ImGui::SliderFloat("Max Simulation Substep Hz", &simSubstepHz, minSimSubstepHz, 240.0f, "%.0f"))
        {
            PS.Physics.MaxSimulationSubstepDeltaTime = 1.0f / simSubstepHz;
            PS.Physics.MaxSimulationSubstepDeltaTime = (std::min)(PS.Physics.MaxSimulationSubstepDeltaTime, PS.Physics.FixedTimeStep);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Caps actual PxScene::simulate(dt). Keep 60Hz or higher when Fixed Step Hz is low.");

        ImGui::SliderInt("Max Substeps", &PS.Physics.MaxSubsteps, 1, 32);
        const int RequiredSubsteps = (PS.Physics.MaxSimulationSubstepDeltaTime > 0.0f)
            ? static_cast<int>(std::ceil(PS.Physics.FixedTimeStep / PS.Physics.MaxSimulationSubstepDeltaTime - 1.e-6f))
            : 1;
        PS.Physics.MaxSubsteps = (std::max)(PS.Physics.MaxSubsteps, (std::max)(1, RequiredSubsteps));
        ImGui::SliderInt("Worker Threads", &PS.Physics.WorkerThreadCount, 0, 32);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = auto (hardware_concurrency - 1)");

        ImGui::TextDisabled("Hz/Substeps/Threads require scene reload.");
    }

	if (ImGui::CollapsingHeader("Shadow", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Shadows", &PS.Shadow.bEnabled);
		if (PS.Shadow.bEnabled)
		{
			// Resolution 선택지 (power of 2)
			static const int kResOptions[] = { 64, 128, 256, 512, 1024, 2048, 4096, 8192 };
			static const char* kResLabels[] = { "64", "128", "256", "512", "1024", "2048", "4096", "8192" };
			constexpr int kNumRes = 8;

			auto ResCombo = [](const char* label, uint32& value) {
				int cur = 0;
				for (int i = 0; i < kNumRes; ++i)
					if (kResOptions[i] == static_cast<int>(value)) { cur = i; break; }
				if (ImGui::Combo(label, &cur, kResLabels, kNumRes))
					value = static_cast<uint32>(kResOptions[cur]);
			};

			ResCombo("CSM Resolution", PS.Shadow.CSMResolution);
			ResCombo("Spot Atlas Resolution", PS.Shadow.SpotAtlasResolution);
			ResCombo("Point Atlas Resolution", PS.Shadow.PointAtlasResolution);

			int spotPages = static_cast<int>(PS.Shadow.MaxSpotAtlasPages);
			if (ImGui::SliderInt("Max Spot Atlas Pages", &spotPages, 1, 16))
				PS.Shadow.MaxSpotAtlasPages = static_cast<uint32>(spotPages);

			int pointPages = static_cast<int>(PS.Shadow.MaxPointAtlasPages);
			if (ImGui::SliderInt("Max Point Atlas Pages", &pointPages, 1, 16))
				PS.Shadow.MaxPointAtlasPages = static_cast<uint32>(pointPages);
		}
	}

	ImGui::End();
}
