#include "Engine/Runtime/Launch.h"

#include "Engine/Runtime/EngineLoop.h"
#include "Engine/Platform/CrashDump.h"
#include "Core/Logging/Log.h"
#include "Diagnostics/ActorSequenceDiagnostics.h"
#include "Diagnostics/CameraEditorMeshDiagnostics.h"
#include "Diagnostics/GameViewportInputDiagnostics.h"
#include "Diagnostics/RuntimeUILayoutDiagnostics.h"
#include <objbase.h>
#include <string>

#pragma comment(lib, "ole32.lib")

// 빌드 변종에 맞는 UEngine 서브클래스 헤더만 포함. EngineLoop 자체는 구체 클래스를
// 모르고, 진입점인 이 파일이 팩토리를 만들어 주입한다 (Engine→Editor/Game 의존
// 끊기 위함).
#if IS_OBJ_VIEWER
#include "ObjViewer/ObjViewerEngine.h"
#elif WITH_EDITOR
#include "Editor/EditorEngine.h"
#elif WITH_STANDALONE
#include "Engine/Runtime/GameEngine.h"
#endif

namespace
{
	UEngine* CreateConcreteEngine()
	{
#if IS_OBJ_VIEWER
		return UObjectManager::Get().CreateObject<UObjViewerEngine>();
#elif WITH_EDITOR
		return UObjectManager::Get().CreateObject<UEditorEngine>();
#elif WITH_STANDALONE
		return UObjectManager::Get().CreateObject<UGameEngine>();
#else
		return UObjectManager::Get().CreateObject<UEngine>();
#endif
	}

	bool HasCommandLineFlag(const wchar_t* Flag)
	{
		const wchar_t* CommandLine = GetCommandLineW();
		return CommandLine && Flag && std::wstring(CommandLine).find(Flag) != std::wstring::npos;
	}

	bool ShouldRunActorSequenceSelfTest()
	{
		return HasCommandLineFlag(L"--run-actor-sequence-self-test")
			|| HasCommandLineFlag(L"--actor-sequence-self-test");
	}

	bool ShouldRunGameJamSelfTests()
	{
		return HasCommandLineFlag(L"--run-gamejam-self-tests")
			|| HasCommandLineFlag(L"--gamejam-self-tests");
	}

	bool ShouldRunCameraEditorMeshSelfTest()
	{
		return HasCommandLineFlag(L"--run-camera-editor-mesh-self-test")
			|| HasCommandLineFlag(L"--camera-editor-mesh-self-test");
	}

	bool ShouldRunRuntimeUILayoutSelfTest()
	{
		return HasCommandLineFlag(L"--run-runtime-ui-layout-self-test")
			|| HasCommandLineFlag(L"--runtime-ui-layout-self-test");
	}

	bool ShouldRunGameViewportInputSelfTest()
	{
		return HasCommandLineFlag(L"--run-game-viewport-input-self-test")
			|| HasCommandLineFlag(L"--game-viewport-input-self-test");
	}

	bool RunGameJamSelfTests()
	{
		const FActorSequenceRoundTripSelfTestResult ActorSequenceResult =
			FActorSequenceDiagnostics::RunRoundTripSelfTest();
		UE_LOG(
			"[GameJamDiagnostics][ActorSequence] %s (%d checks): %s",
			ActorSequenceResult.bPassed ? "PASS" : "FAIL",
			ActorSequenceResult.ChecksRun,
			ActorSequenceResult.Message.c_str());

		const FCameraEditorMeshSelfTestResult CameraMeshResult =
			FCameraEditorMeshDiagnostics::RunSelfTest();
		UE_LOG(
			"[GameJamDiagnostics][CameraEditorMesh] %s (%d checks): %s",
			CameraMeshResult.bPassed ? "PASS" : "FAIL",
			CameraMeshResult.ChecksRun,
			CameraMeshResult.Message.c_str());

		const FRuntimeUILayoutSelfTestResult RuntimeUILayoutResult =
			FRuntimeUILayoutDiagnostics::RunRoundTripSelfTest();
		UE_LOG(
			"[GameJamDiagnostics][RuntimeUILayout] %s (%d checks): %s",
			RuntimeUILayoutResult.bPassed ? "PASS" : "FAIL",
			RuntimeUILayoutResult.ChecksRun,
			RuntimeUILayoutResult.Message.c_str());

		const FGameViewportInputSelfTestResult GameViewportInputResult =
			FGameViewportInputDiagnostics::RunSelfTest();
		UE_LOG(
			"[GameJamDiagnostics][GameViewportInput] %s (%d checks): %s",
			GameViewportInputResult.bPassed ? "PASS" : "FAIL",
			GameViewportInputResult.ChecksRun,
			GameViewportInputResult.Message.c_str());

		return ActorSequenceResult.bPassed
			&& CameraMeshResult.bPassed
			&& RuntimeUILayoutResult.bPassed
			&& GameViewportInputResult.bPassed;
	}

	int GuardedMain(HINSTANCE hInstance, int nShowCmd)
	{
		const HRESULT ComInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		const bool bDidInitializeCOM = SUCCEEDED(ComInitResult);

		FEngineLoop EngineLoop(&CreateConcreteEngine);
		if (!EngineLoop.Init(hInstance, nShowCmd))
		{
			if (bDidInitializeCOM)
			{
				CoUninitialize();
			}
			return -1;
		}

		if (ShouldRunGameJamSelfTests())
		{
			const bool bPassed = RunGameJamSelfTests();
			UE_LOG("[GameJamDiagnostics] %s", bPassed ? "PASS" : "FAIL");

			EngineLoop.Shutdown();
			if (bDidInitializeCOM)
			{
				CoUninitialize();
			}
			return bPassed ? 0 : 2;
		}

		if (ShouldRunActorSequenceSelfTest())
		{
			const FActorSequenceRoundTripSelfTestResult TestResult =
				FActorSequenceDiagnostics::RunRoundTripSelfTest();
			UE_LOG(
				"[ActorSequenceDiagnostics] %s (%d checks): %s",
				TestResult.bPassed ? "PASS" : "FAIL",
				TestResult.ChecksRun,
				TestResult.Message.c_str());

			EngineLoop.Shutdown();
			if (bDidInitializeCOM)
			{
				CoUninitialize();
			}
			return TestResult.bPassed ? 0 : 2;
		}

		if (ShouldRunCameraEditorMeshSelfTest())
		{
			const FCameraEditorMeshSelfTestResult TestResult =
				FCameraEditorMeshDiagnostics::RunSelfTest();
			UE_LOG(
				"[CameraEditorMeshDiagnostics] %s (%d checks): %s",
				TestResult.bPassed ? "PASS" : "FAIL",
				TestResult.ChecksRun,
				TestResult.Message.c_str());

			EngineLoop.Shutdown();
			if (bDidInitializeCOM)
			{
				CoUninitialize();
			}
			return TestResult.bPassed ? 0 : 2;
		}

		if (ShouldRunRuntimeUILayoutSelfTest())
		{
			const FRuntimeUILayoutSelfTestResult TestResult =
				FRuntimeUILayoutDiagnostics::RunRoundTripSelfTest();
			UE_LOG(
				"[RuntimeUILayoutDiagnostics] %s (%d checks): %s",
				TestResult.bPassed ? "PASS" : "FAIL",
				TestResult.ChecksRun,
				TestResult.Message.c_str());

			EngineLoop.Shutdown();
			if (bDidInitializeCOM)
			{
				CoUninitialize();
			}
			return TestResult.bPassed ? 0 : 2;
		}

		if (ShouldRunGameViewportInputSelfTest())
		{
			const FGameViewportInputSelfTestResult TestResult =
				FGameViewportInputDiagnostics::RunSelfTest();
			UE_LOG(
				"[GameViewportInputDiagnostics] %s (%d checks): %s",
				TestResult.bPassed ? "PASS" : "FAIL",
				TestResult.ChecksRun,
				TestResult.Message.c_str());

			EngineLoop.Shutdown();
			if (bDidInitializeCOM)
			{
				CoUninitialize();
			}
			return TestResult.bPassed ? 0 : 2;
		}

		const int ExitCode = EngineLoop.Run();
		EngineLoop.Shutdown();
		if (bDidInitializeCOM)
		{
			CoUninitialize();
		}
		return ExitCode;
	}
}

int Launch(HINSTANCE hInstance, int nShowCmd)
{
	__try
	{
		return GuardedMain(hInstance, nShowCmd);
	}
	__except (WriteCrashDump(GetExceptionInformation()))
	{
		return static_cast<int>(GetExceptionCode());
	}
}
