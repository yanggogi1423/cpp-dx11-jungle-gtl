#include "Diagnostics/RuntimeUILayoutDiagnostics.h"

#include "Object/GarbageCollection.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Platform/Paths.h"
#include "UI/RuntimeUILayoutAsset.h"
#include "UI/RuntimeUILayoutManager.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace
{
	constexpr const char* SelfTestAssetPath = "Saved/Diagnostics/RuntimeUILayoutSelfTest.uasset";
	constexpr const char* SelfTestRmlPath = "Saved/Diagnostics/RuntimeUILayoutSelfTest.rml";
	constexpr const char* SelfTestRcssPath = "Saved/Diagnostics/RuntimeUILayoutSelfTest.rcss";

	struct FRuntimeUILayoutSelfTestContext
	{
		FRuntimeUILayoutSelfTestResult Result;

		void Check(bool bCondition, const char* Message)
		{
			++Result.ChecksRun;
			if (bCondition)
			{
				return;
			}

			Result.bPassed = false;
			if (!Result.Message.empty())
			{
				Result.Message += "\n";
			}
			Result.Message += Message ? Message : "unknown failure";
		}
	};

	std::filesystem::path ToAbsoluteProjectPath(const FString& Path)
	{
		std::filesystem::path Result(FPaths::ToWide(Path));
		if (Result.is_relative())
		{
			Result = std::filesystem::path(FPaths::RootDir()) / Result;
		}
		return Result.lexically_normal();
	}

	void DeleteSelfTestFiles()
	{
		for (const char* Path : { SelfTestAssetPath, SelfTestRmlPath, SelfTestRcssPath })
		{
			std::error_code Error;
			std::filesystem::remove(ToAbsoluteProjectPath(Path), Error);
		}
	}

	FString ReadTextFile(const FString& Path)
	{
		std::ifstream File(ToAbsoluteProjectPath(Path), std::ios::binary);
		if (!File)
		{
			return FString();
		}

		std::ostringstream Stream;
		Stream << File.rdbuf();
		return Stream.str();
	}

	FRuntimeUIWidgetNode* FindMutableWidgetById(URuntimeUILayoutAsset* Layout, const FString& Id)
	{
		if (!Layout)
		{
			return nullptr;
		}

		for (FRuntimeUIWidgetNode& Node : Layout->GetMutableWidgets())
		{
			if (Node.Id == Id)
			{
				return &Node;
			}
		}
		return nullptr;
	}

	const FRuntimeUIWidgetNode* FindWidgetById(const URuntimeUILayoutAsset* Layout, const FString& Id)
	{
		if (!Layout)
		{
			return nullptr;
		}

		for (const FRuntimeUIWidgetNode& Node : Layout->GetWidgets())
		{
			if (Node.Id == Id)
			{
				return &Node;
			}
		}
		return nullptr;
	}

	bool ContainsText(const FString& Text, const char* Needle)
	{
		return Needle && Text.find(Needle) != FString::npos;
	}
}

FRuntimeUILayoutSelfTestResult FRuntimeUILayoutDiagnostics::RunRoundTripSelfTest()
{
	FScopedGarbageCollectionBlocker GCBlocker;
	FRuntimeUILayoutSelfTestContext Context;
	Context.Result.bPassed = true;

	DeleteSelfTestFiles();
	FRuntimeUILayoutManager::Get().ClearCache();

	URuntimeUILayoutAsset* Layout = UObjectManager::Get().CreateObject<URuntimeUILayoutAsset>();
	Context.Check(Layout != nullptr, "Runtime UI self-test should create a layout asset.");
	if (!Layout)
	{
		return Context.Result;
	}

	Layout->SetAssetPath(SelfTestAssetPath);
	Layout->SetGeneratedPaths(SelfTestRmlPath, SelfTestRcssPath);
	Layout->SetCanvasSize(FVector2(1280.0f, 720.0f));

	FRuntimeUIWidgetNode* StartButton = FindMutableWidgetById(Layout, "startButton");
	Context.Check(StartButton != nullptr, "Runtime UI self-test should find the default startButton node.");
	if (StartButton)
	{
		StartButton->Text = "Diagnostics Start";
		StartButton->OnClickAction = "DiagnosticsStart";
		StartButton->Position = FVector2(320.0f, 420.0f);
		StartButton->Size = FVector2(300.0f, 80.0f);
	}

	const bool bSaved = FRuntimeUILayoutManager::Get().Save(Layout);
	Context.Check(bSaved, "Runtime UI self-test should save the layout package.");

	UObjectManager::Get().DestroyObject(Layout);
	FRuntimeUILayoutManager::Get().ClearCache();

	URuntimeUILayoutAsset* LoadedLayout = FRuntimeUILayoutManager::Get().Load(SelfTestAssetPath);
	Context.Check(LoadedLayout != nullptr, "Runtime UI self-test should load the saved layout package.");
	if (!LoadedLayout)
	{
		DeleteSelfTestFiles();
		return Context.Result;
	}

	Context.Check(LoadedLayout->GetCanvasSize().X == 1280.0f && LoadedLayout->GetCanvasSize().Y == 720.0f,
		"Runtime UI self-test should preserve the canvas size through save/load.");
	Context.Check(LoadedLayout->GetGeneratedRmlPath() == SelfTestRmlPath,
		"Runtime UI self-test should preserve the generated RML path.");
	Context.Check(LoadedLayout->GetGeneratedRcssPath() == SelfTestRcssPath,
		"Runtime UI self-test should preserve the generated RCSS path.");

	const FRuntimeUIWidgetNode* LoadedButton = FindWidgetById(LoadedLayout, "startButton");
	Context.Check(LoadedButton != nullptr, "Runtime UI self-test should preserve startButton through save/load.");
	if (LoadedButton)
	{
		Context.Check(LoadedButton->Text == "Diagnostics Start",
			"Runtime UI self-test should preserve button text through save/load.");
		Context.Check(LoadedButton->OnClickAction == "DiagnosticsStart",
			"Runtime UI self-test should preserve button action through save/load.");
		Context.Check(LoadedButton->ParentIndex >= 0,
			"Runtime UI self-test should rebuild button hierarchy through save/load.");
	}

	FString ExportError;
	const bool bExported = LoadedLayout->ExportRmlAndRcss(SelfTestRmlPath, SelfTestRcssPath, &ExportError);
	Context.Check(bExported, "Runtime UI self-test should export RML and RCSS.");
	if (!bExported && !ExportError.empty())
	{
		Context.Check(false, ExportError.c_str());
	}

	const FString RmlSource = ReadTextFile(SelfTestRmlPath);
	const FString RcssSource = ReadTextFile(SelfTestRcssPath);
	Context.Check(!RmlSource.empty(), "Runtime UI self-test should write a non-empty RML file.");
	Context.Check(!RcssSource.empty(), "Runtime UI self-test should write a non-empty RCSS file.");
	Context.Check(ContainsText(RmlSource, "<link type=\"text/rcss\""),
		"Runtime UI exported RML should link its generated RCSS file.");
	Context.Check(ContainsText(RmlSource, "id=\"startButton\""),
		"Runtime UI exported RML should contain the startButton id.");
	Context.Check(ContainsText(RmlSource, "data-action=\"DiagnosticsStart\""),
		"Runtime UI exported RML should contain the button action binding.");
	Context.Check(ContainsText(RmlSource, "Diagnostics Start"),
		"Runtime UI exported RML should contain the mutated button text.");
	Context.Check(ContainsText(RcssSource, "#startButton"),
		"Runtime UI exported RCSS should contain the startButton style block.");
	Context.Check(ContainsText(RcssSource, "width: 1280.000000px"),
		"Runtime UI exported RCSS should contain the mutated canvas width.");
	Context.Check(ContainsText(RcssSource, "height: 720.000000px"),
		"Runtime UI exported RCSS should contain the mutated canvas height.");

	FRuntimeUILayoutManager::Get().ClearCache();
	DeleteSelfTestFiles();

	if (Context.Result.bPassed && Context.Result.Message.empty())
	{
		Context.Result.Message = "Runtime UI layout save/load/export self-test passed.";
	}
	return Context.Result;
}
