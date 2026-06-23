#include "ContentBrowser.h"

#include "Asset/AssetPackage.h"
#include "Animation/Graph/AnimGraphAsset.h"
#include "Animation/Graph/AnimGraphManager.h"
#include "CameraShake/CameraShakeAsset.h"
#include "CameraShake/CameraShakeManager.h"
#include "ContentBrowserElement.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Subsystem/AssetFactory.h"
#include "Editor/UI/Util/EditorMeshThumbnailManager.h"
#include "Editor/Undo/EditorUndoSystem.h"
#include "Editor/UI/Util/EditorTextureManager.h"
#include "FloatCurve/FloatCurveAsset.h"
#include "FloatCurve/FloatCurveManager.h"
#include "LuaBlueprint/LuaBlueprintAsset.h"
#include "LuaBlueprint/LuaBlueprintManager.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Editor/UI/Asset/Mesh/MeshEditorWidget.h"
#include "EditorEngine.h"
#include "Editor/UI/Dialog/FbxImportOptionsDialog.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

#include "Particle/ParticleSystemManager.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"
#include "UI/RuntimeUILayoutAsset.h"
#include "UI/RuntimeUILayoutManager.h"

namespace
{
	bool IsParentDirectoryReference(const std::filesystem::path& Path)
	{
		for (const std::filesystem::path& Part : Path)
		{
			if (Part == L"..")
			{
				return true;
			}
		}

		return false;
	}

	FString MakeContentBrowserSettingsPath(const std::wstring& CurrentPath)
	{
		const std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir()).lexically_normal();
		const std::filesystem::path Path = std::filesystem::path(CurrentPath).lexically_normal();
		const std::filesystem::path RelativePath = Path.lexically_relative(RootPath);

		if (!RelativePath.empty() && !IsParentDirectoryReference(RelativePath))
		{
			return FPaths::ToUtf8(RelativePath.generic_wstring());
		}

		return FPaths::ToUtf8(Path.wstring());
	}

	std::wstring ResolveContentBrowserSettingsPath(const FString& SavedPath)
	{
		if (SavedPath.empty())
		{
			return FPaths::RootDir();
		}

		std::filesystem::path Path(FPaths::ToWide(SavedPath));
		if (!Path.is_absolute())
		{
			Path = std::filesystem::path(FPaths::RootDir()) / Path;
		}

		Path = Path.lexically_normal();
		if (std::filesystem::exists(Path) && std::filesystem::is_directory(Path))
		{
			return Path.wstring();
		}

		return FPaths::RootDir();
	}

	bool IsSubPath(const std::filesystem::path& Parent, const std::filesystem::path& Child)
	{
		std::filesystem::path P = std::filesystem::weakly_canonical(Parent);
		std::filesystem::path C = std::filesystem::weakly_canonical(Child);

		auto PIt = P.begin();
		auto CIt = C.begin();

		for (; PIt != P.end() && CIt != C.end(); ++PIt, ++CIt)
		{
			if (*PIt != *CIt)
			{
				return false;
			}
		}

		return PIt == P.end();
	}

	bool IsContentFontMetadata(const std::filesystem::path& Path, const FString& LowerExtension)
	{
		if (LowerExtension != ".font" && LowerExtension != ".fnt")
		{
			return false;
		}

		const std::filesystem::path FontRoot = std::filesystem::path(FPaths::RootDir()) / L"Content" / L"Font";
		std::error_code Error;
		return std::filesystem::exists(FontRoot, Error) && IsSubPath(FontRoot, Path.parent_path());
	}

	FString ToContentUndoPath(const std::filesystem::path& Path)
	{
		return FPaths::ToUtf8(Path.wstring());
	}

	FEditorFileSystemState CaptureContentUndoState(UEditorEngine* Editor, const FString& Path, const FString& Label)
	{
		if (!Editor || Path.empty())
		{
			return {};
		}
		return Editor->GetUndoSystem().CaptureFileSystemState(Path, Label);
	}

	void RecordCreatedContentPaths(UEditorEngine* Editor, const TArray<FString>& Paths, const FString& Label)
	{
		if (!Editor || Paths.empty())
		{
			return;
		}

		TArray<FEditorFileSystemState> CreatedStates;
		for (const FString& Path : Paths)
		{
			FEditorFileSystemState State = CaptureContentUndoState(Editor, Path, Label);
			if (State.IsValid() && !State.Entries.empty())
			{
				CreatedStates.push_back(std::move(State));
			}
		}

		if (!CreatedStates.empty())
		{
			Editor->GetUndoSystem().RecordCreateFileSystemPaths(CreatedStates, Label);
		}
	}

	void RecordCreatedContentPath(UEditorEngine* Editor, const FString& Path, const FString& Label)
	{
		RecordCreatedContentPaths(Editor, TArray<FString>{ Path }, Label);
	}

	FString ReplaceExtensionForUndo(const FString& Path, const wchar_t* Extension)
	{
		std::filesystem::path WidePath(FPaths::ToWide(Path));
		WidePath.replace_extension(Extension);
		return FPaths::ToUtf8(WidePath.wstring());
	}

	void RecordFbxSceneImportCreationForBrowser(UEditorEngine* Editor, const FFbxSceneImportResult& Result)
	{
		TArray<FString> CreatedPaths;
		if (Result.Skeleton)
		{
			CreatedPaths.push_back(Result.Skeleton->GetAssetPathFileName());
		}
		if (Result.SkeletalMesh)
		{
			CreatedPaths.push_back(Result.SkeletalMesh->GetAssetPathFileName());
		}
		for (const UAnimSequence* AnimSequence : Result.AnimSequences)
		{
			if (AnimSequence)
			{
				CreatedPaths.push_back(AnimSequence->GetAssetPathFileName());
			}
		}
		RecordCreatedContentPaths(Editor, CreatedPaths, "Import FBX Scene");
	}
}

namespace
{
	using FContentBrowserImportClock = std::chrono::steady_clock;

	static double GetElapsedImportSeconds(const FContentBrowserImportClock::time_point& StartTime)
	{
		return std::chrono::duration<double>(FContentBrowserImportClock::now() - StartTime).count();
	}
}

void FEditorContentBrowserWidget::Initialize(UEditorEngine* InEditor, ID3D11Device* InDevice)
{
	FEditorWidget::Initialize(InEditor);
	if (!InDevice) return;

	IconFileMap[".Scene"] = L"World_64x.png";
	IconFileMap[".prefab"] = L"StartMerge_42x.png";
	IconFileMap[".obj"] = L"icon_MatEd_Mesh_40x.png";
	IconFileMap[".mat"] = L"Sphere_64x.png";
	IconFileMap[".shake"] = L"StartMerge_42x.png";
	IconFileMap[".fbx"] = L"icon_MatEd_Mesh_40x.png";
	IconFileMap[".fga"] = L"StartMerge_42x.png";
	IconFileMap[".font"] = L"StartMerge_42x.png";
	IconFileMap[".fnt"] = L"StartMerge_42x.png";
	IconFileMap[".uasset"] = L"icon_MatEd_Mesh_40x.png";

	ContentBrowserContext Context;
	Context.ContentSize = ImVec2(112, 112);
	Context.EditorEngine = InEditor;
	BrowserContext = Context;
	LoadFromSettings();

	Refresh();
}

void FEditorContentBrowserWidget::Render(float DeltaTime)
{
	RenderWithFlags(DeltaTime, ImGuiWindowFlags_NoDocking);
}

void FEditorContentBrowserWidget::RenderWithFlags(float DeltaTime, ImGuiWindowFlags WindowFlags)
{
	if (!ImGui::Begin("ContentBrowser", nullptr, WindowFlags))
	{
		ImGui::End();
		return;
	}

	(void)DeltaTime;

	if (BrowserContext.bPendingContentRefresh)
	{
		RefreshContent();
		BrowserContext.bPendingContentRefresh = false;
	}

	// ── F2 / 우클릭 메뉴 → Rename popup ──
	// 윈도우 포커스 + 텍스트 입력 중 아님 + Selected 있음 → popup 열기.
	// (우클릭 메뉴의 "Rename" 항목은 SelectedElement->Render 안에서 PendingRenameOpen 플래그 set.)
	static char       sRenameBuf[256] = {};
	static FString    sRenameError;
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
	    !ImGui::GetIO().WantTextInput &&
	    ImGui::IsKeyPressed(ImGuiKey_F2, false) &&
	    BrowserContext.SelectedElement)
	{
		BrowserContext.bRenameRequested = true;
	}
	if (BrowserContext.bRenameRequested && BrowserContext.SelectedElement)
	{
		const FContentItem& Item = BrowserContext.SelectedElement->GetContentItem();
		const FString Stem = Item.bIsDirectory
			? FPaths::ToUtf8(Item.Path.filename().wstring())
			: FPaths::ToUtf8(Item.Path.stem().wstring());
		strncpy_s(sRenameBuf, sizeof(sRenameBuf), Stem.c_str(), _TRUNCATE);
		sRenameError.clear();
		ImGui::OpenPopup("##RenameElement");
		BrowserContext.bRenameRequested = false;
	}
	if (ImGui::BeginPopupModal("##RenameElement", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Rename");
		ImGui::Separator();
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(320.0f);
		const bool bSubmit = ImGui::InputText("##rename", sRenameBuf, sizeof(sRenameBuf),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

		if (!sRenameError.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", sRenameError.c_str());
		}

		const bool bOk     = bSubmit || ImGui::Button("OK");
		ImGui::SameLine();
		const bool bCancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);

		if (bOk)
		{
			if (BrowserContext.SelectedElement)
			{
				FString Error;
				const FContentItem& Item = BrowserContext.SelectedElement->GetContentItem();
				const FEditorFileSystemState BeforeState = CaptureContentUndoState(
					BrowserContext.EditorEngine,
					ToContentUndoPath(Item.Path),
					"Rename Content");
				if (BrowserContext.SelectedElement->RenameTo(sRenameBuf, &Error))
				{
					const FContentItem& RenamedItem = BrowserContext.SelectedElement->GetContentItem();
					const FEditorFileSystemState AfterState = CaptureContentUndoState(
						BrowserContext.EditorEngine,
						ToContentUndoPath(RenamedItem.Path),
						"Rename Content");
					if (BrowserContext.EditorEngine)
					{
						BrowserContext.EditorEngine->GetUndoSystem().RecordRenameFileSystemPath(
							BeforeState,
							AfterState,
							"Rename Content");
					}

					// 디스크 변경됐으니 다음 frame 에 디렉토리 다시 스캔. SelectedElement 의
					// shared_ptr 는 무효화될 거라 해제 — 사용자가 다시 클릭해 선택.
					BrowserContext.SelectedElement.reset();
					BrowserContext.bPendingContentRefresh = true;
					ImGui::CloseCurrentPopup();
				}
				else
				{
					sRenameError = Error;
				}
			}
			else
			{
				ImGui::CloseCurrentPopup();
			}
		}
		if (bCancel)
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// ── 우클릭 메뉴 → Delete 확인 popup ──
	static FString sDeleteError;
	if (BrowserContext.bDeleteRequested && BrowserContext.SelectedElement)
	{
		sDeleteError.clear();
		ImGui::OpenPopup("##DeleteElement");
		BrowserContext.bDeleteRequested = false;
	}
	if (ImGui::BeginPopupModal("##DeleteElement", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Delete");
		ImGui::Separator();
		if (BrowserContext.SelectedElement)
		{
			const FContentItem& Item = BrowserContext.SelectedElement->GetContentItem();
			const FString Name = FPaths::ToUtf8(Item.Path.filename().wstring());
			if (Item.bIsDirectory)
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "폴더 '%s' 와 내부 전체를 삭제합니다. 되돌릴 수 없습니다.", Name.c_str());
			else
				ImGui::Text("'%s' 을(를) 삭제합니다. 되돌릴 수 없습니다.", Name.c_str());
		}
		if (!sDeleteError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", sDeleteError.c_str());

		const bool bConfirmDel = ImGui::Button("Delete");
		ImGui::SameLine();
		const bool bCancelDel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);
		if (bConfirmDel && BrowserContext.SelectedElement)
		{
			FString Error;
			const FContentItem& Item = BrowserContext.SelectedElement->GetContentItem();
			const FEditorFileSystemState DeletedState = CaptureContentUndoState(
				BrowserContext.EditorEngine,
				ToContentUndoPath(Item.Path),
				"Delete Content");
			if (BrowserContext.SelectedElement->Delete(&Error))
			{
				if (BrowserContext.EditorEngine)
				{
					BrowserContext.EditorEngine->GetUndoSystem().RecordDeleteFileSystemPath(
						DeletedState,
						"Delete Content");
				}
				BrowserContext.SelectedElement.reset();
				BrowserContext.bPendingContentRefresh = true;
				ImGui::CloseCurrentPopup();
			}
			else
			{
				sDeleteError = Error;
			}
		}
		if (bCancelDel)
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	std::wstring PathText = BrowserContext.CurrentPath;
	if (BrowserContext.SelectedElement)
		PathText += L"/" + BrowserContext.SelectedElement->GetFileName();

	ImGui::SameLine();
	int Size = static_cast<int>(BrowserContext.ContentSize.x);
	BrowserContext.ContentSize = ImVec2(static_cast<float>(Size), static_cast<float>(Size));

	if (!ImGui::BeginTable("ContentBrowserLayout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::End();
		return;
	}

	ImGui::TableSetupColumn("Directory", ImGuiTableColumnFlags_WidthFixed, 250.0f);
	ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 260.0f);

	ImGui::TableNextColumn();
	{
		ImGui::BeginChild("DirectoryTree", ImVec2(0, 0), true);
		DrawDirNode(RootNode);
		BrowserContext.PendingRevealPath.clear();
		ImGui::EndChild();
	}

	ImGui::TableNextColumn();
	{
		ImGui::BeginChild("ContentArea", ImVec2(0, 0), true);
		DrawContents();
		ImGui::EndChild();
	}

	ImGui::TableNextColumn();
	{
		ImGui::BeginChild("DetailsPanel", ImVec2(0, 0), true);

		if (BrowserContext.SelectedElement)
		{
			BrowserContext.SelectedElement->RenderDetail();
		}
		else
		{
			ImGui::TextDisabled("No asset selected");
		}

		ImGui::EndChild();
	}

	ImGui::EndTable();

	RenderFbxImportOptionsPopup();

	ImGui::End();
}

void FEditorContentBrowserWidget::RenderFbxImportOptionsPopup()
{
	FFbxSceneImportRequest       Request;
	const EFbxImportDialogResult DialogResult = FFbxImportOptionsDialog::RenderSceneImportPopup(
		"FBX Import Options",
		BrowserContext.FbxImportDialog,
		Request
	);

	if (DialogResult != EFbxImportDialogResult::Submitted)
	{
		return;
	}

	if (!BrowserContext.EditorEngine)
	{
		BrowserContext.FbxImportDialog.Error = "Editor engine is not available.";
		return;
	}

	FFbxSceneImportResult Result;
	const auto            ImportStartTime = FContentBrowserImportClock::now();
	const bool            bImported       = FMeshManager::ImportFbxScene(
		Request,
		BrowserContext.EditorEngine->GetRenderer().GetFD3DDevice().GetDevice(),
		Result
	);

	if (bImported)
	{
		RecordFbxSceneImportCreationForBrowser(BrowserContext.EditorEngine, Result);
		if (Result.SkeletalMesh)
		{
			FMeshEditorWidget::RecordImportDurationForAsset(
				Result.SkeletalMesh->GetAssetPathFileName(),
				GetElapsedImportSeconds(ImportStartTime)
			);
			BrowserContext.EditorEngine->OpenAssetEditorForObject(Result.SkeletalMesh);
		}

		Refresh();
		FFbxImportOptionsDialog::RequestClose(BrowserContext.FbxImportDialog);
	}
	else
	{
		BrowserContext.FbxImportDialog.Error = "FBX import failed. See the engine log for details.";
	}
}

void FEditorContentBrowserWidget::Refresh()
{
	RootNode = BuildDirectoryTree(FPaths::RootDir());
	RefreshContent();

	BrowserContext.bPendingContentRefresh = false;
}

void FEditorContentBrowserWidget::SetIconSize(float Size)
{
	const float ClampedSize = (std::max)(72.0f, (std::min)(Size, 160.0f));
	BrowserContext.ContentSize = ImVec2(ClampedSize, ClampedSize);
}

void FEditorContentBrowserWidget::LoadFromSettings()
{
	BrowserContext.CurrentPath = ResolveContentBrowserSettingsPath(FEditorSettings::Get().ContentBrowserPath);
	BrowserContext.PendingRevealPath = BrowserContext.CurrentPath;
}

void FEditorContentBrowserWidget::SaveToSettings() const
{
	FEditorSettings::Get().ContentBrowserPath = MakeContentBrowserSettingsPath(BrowserContext.CurrentPath);
}

void FEditorContentBrowserWidget::RefreshContent()
{
	FEditorTextureManager::Get().ClearThumbnails();
	FEditorMeshThumbnailManager::Get().ClearThumbnails();
	CachedBrowserElements.clear();
	TArray<FContentItem> CurrentContents = ReadDirectory(BrowserContext.CurrentPath);
	for (const auto& Content : CurrentContents)
	{
		std::shared_ptr<ContentBrowserElement> Element;
		FString Extension = FPaths::ToUtf8(Content.Path.extension());
		for (char& Character : Extension)
		{
			Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
		}
		ID3D11ShaderResourceView* Icon = nullptr;

		if (Content.bIsDirectory)
		{
			Element = std::make_shared<DirectoryElement>();
			Icon = FEditorTextureManager::Get().GetOrLoadIcon(FPaths::ToUtf8(FPaths::Combine(FPaths::AssetDir(), L"Editor/Icons/", L"Folder_Base_256x.png")));
		}
		else if (Extension == ".scene")
		{
			Element = std::make_shared<SceneElement>();
		}
		else if (Extension == ".prefab")
		{
			Element = std::make_shared<PrefabElement>();
		}
		else if (Extension == ".obj")
		{
			Element = std::make_shared<ObjectElement>();
		}
		else if (Extension == ".mat")
		{
			Element = std::make_shared<MaterialElement>();
		}
		else if (Extension == ".curve")
		{
			Element = std::make_shared<FloatCurveElement>();
		}
		else if (Extension == ".shake")
		{
			Element = std::make_shared<CameraShakeElement>();
		}
		else if (Extension == ".fbx")
		{
			Element = std::make_shared<MeshElement>();
		}
		else if (Extension == ".fga")
		{
			Element = std::make_shared<VectorFieldSourceElement>();
		}
		else if (Extension == ".png")
		{
			Element = std::make_shared<PNGElement>();
			Icon = FEditorTextureManager::Get().GetOrLoadThumbnail(FPaths::ToUtf8(Content.Path.lexically_relative(FPaths::RootDir()).generic_wstring()));
		}
		else if (IsContentFontMetadata(Content.Path, Extension))
		{
			Element = std::make_shared<FontElement>();
		}
		else if (Extension == ".rml" || Extension == ".rcss")
		{
			Element = std::make_shared<RuntimeUIElement>();
		}
		else if (Extension == ".uasset")
		{
			FString PackagePath = FPaths::ToUtf8(Content.Path.lexically_relative(FPaths::RootDir()).generic_wstring());

			EAssetPackageType Type = EAssetPackageType::Unknown;
			if (FAssetPackage::GetPackageType(PackagePath, Type))
			{
				switch (Type)
				{
				case EAssetPackageType::StaticMesh:
					Element = std::make_shared<ObjectElement>();
					Element->SetMeshThumbnailRequest(PackagePath, EMeshThumbnailType::StaticMesh);
					break;
				case EAssetPackageType::SkeletalMesh:
					Element = std::make_shared<MeshElement>();
					break;
				case EAssetPackageType::FloatCurve:
					Element = std::make_shared<FloatCurveElement>();
					break;
				case EAssetPackageType::CameraShake:
					Element = std::make_shared<CameraShakeElement>();
					break;
				case EAssetPackageType::Skeleton:
					Element = std::make_shared<SkeletonElement>();
					break;
				case EAssetPackageType::AnimSequence:
					Element = std::make_shared<AnimationElement>();
					break;
				case EAssetPackageType::AnimMontage:
					Element = std::make_shared<AnimationElement>();
					break;
				case EAssetPackageType::AnimGraph:
					Element = std::make_shared<AnimGraphElement>();
					break;
				case EAssetPackageType::ParticleSystem:
					Element = std::make_shared<ParticleSystemElement>();
					break;
				case EAssetPackageType::VectorField:
					Element = std::make_shared<VectorFieldElement>();
					break;
				case EAssetPackageType::LuaBlueprint:
					Element = std::make_shared<LuaBlueprintElement>();
					break;
				case EAssetPackageType::PhysicsAsset:
					Element = std::make_shared<PhysicsAssetElement>();
					break;
				case EAssetPackageType::RuntimeUILayout:
					Element = std::make_shared<RuntimeUILayoutElement>();
					break;
				case EAssetPackageType::Material:
					Element = std::make_shared<MaterialElement>();
					break;
				default:
					Element = std::make_shared<ContentBrowserElement>();
					break;
				}
			}
			else
			{
				// 손상/미완성된 .uasset 도 generic element 로 fallback 해서 SetIcon 단계에서 null deref 를 막는다.
				Element = std::make_shared<ContentBrowserElement>();
			}
		}
		else
		{
			Element = std::make_shared<ContentBrowserElement>();
		}

		if (!Icon)
		{
			std::wstring IconFileName = L"StartMerge_42x.png";
			if (auto It = IconFileMap.find(Extension); It != IconFileMap.end())
			{
				IconFileName = It->second;
			}

			Icon = FEditorTextureManager::Get().GetOrLoadIcon(FPaths::ToUtf8(FPaths::Combine(FPaths::AssetDir(), L"Editor/Icons/", IconFileName)));
		}

		Element->SetIcon(Icon);

		Element->SetContent(Content);
		CachedBrowserElements.push_back(std::move(Element));
	}
}

void FEditorContentBrowserWidget::DrawDirNode(const FDirNode& InNode)
{
	ImGuiTreeNodeFlags Flag =
		InNode.Children.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_OpenOnArrow;

	if (InNode.Self.Path == BrowserContext.CurrentPath)
	{
		Flag |= ImGuiTreeNodeFlags_Selected;
	}
	if (!BrowserContext.PendingRevealPath.empty() && IsSubPath(InNode.Self.Path, BrowserContext.PendingRevealPath))
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}

	bool bIsOpen = ImGui::TreeNodeEx(FPaths::ToUtf8(InNode.Self.Name).c_str(), Flag);
	if (ImGui::IsItemClicked())
	{
		if (BrowserContext.CurrentPath != InNode.Self.Path)
		{
			BrowserContext.CurrentPath = InNode.Self.Path;
			RefreshContent();
		}
	}

	if (!bIsOpen)
	{
		return;
	}

	int32 ChildrenCount = static_cast<int32>(InNode.Children.size());
	for (int i = 0; i < ChildrenCount; i++)
	{
		DrawDirNode(InNode.Children[i]);
	}

	ImGui::TreePop();
}

void FEditorContentBrowserWidget::DrawContents()
{
	int ElementCount = static_cast<int>(CachedBrowserElements.size());

	const float ContentWidth = ImGui::GetContentRegionAvail().x;
	const float ItemWidth = BrowserContext.ContentSize.x;
	const float ItemHeight = BrowserContext.ContentSize.y;

	int ColumnCount = static_cast<int>(ContentWidth / ItemWidth);
	if (ColumnCount < 1)
	{
		ColumnCount = 1;
	}

	float GapSize = 0.0f;
	if (ColumnCount > 1)
	{
		GapSize = (ContentWidth - ItemWidth * ColumnCount) / (ColumnCount);
	}

	ImVec2 StartPos = ImGui::GetCursorPos();

	for (int i = 0; i < ElementCount; ++i)
	{
		int Column = i % ColumnCount;
		int Row = i / ColumnCount;

		float X = StartPos.x + Column * (ItemWidth + GapSize);
		float Y = StartPos.y + Row * (ItemHeight + GapSize * 2.f);

		ImGui::SetCursorPos(ImVec2(X, Y));
		CachedBrowserElements[i]->Render(BrowserContext);
	}

	int RowCount = (ElementCount + ColumnCount - 1) / ColumnCount;
	ImGui::SetCursorPos(ImVec2(StartPos.x, StartPos.y + RowCount * ItemHeight));

	if (ImGui::BeginPopupContextWindow("##ContentBrowserBackgroundContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::BeginMenu("Create"))
		{
			if (ImGui::MenuItem("Float Curve"))
			{
				FString CreatedPath;
				if (FAssetFactory::CreateFloatCurve(FPaths::ToUtf8(BrowserContext.CurrentPath), "NewFloatCurve", CreatedPath))
				{
					RecordCreatedContentPath(BrowserContext.EditorEngine, CreatedPath, "Create Float Curve");
					Refresh();
					if (BrowserContext.EditorEngine)
					{
						if (UFloatCurveAsset* CurveAsset = FFloatCurveManager::Get().Load(CreatedPath))
						{
							BrowserContext.EditorEngine->OpenAssetEditorForObject(CurveAsset);
						}
					}
				}
			}
			if (ImGui::MenuItem("Camera Shake"))
			{
				FString CreatedPath;
				if (FAssetFactory::CreateCameraShake(FPaths::ToUtf8(BrowserContext.CurrentPath), "NewCameraShake", CreatedPath))
				{
					RecordCreatedContentPath(BrowserContext.EditorEngine, CreatedPath, "Create Camera Shake");
					Refresh();
					if (BrowserContext.EditorEngine)
					{
						if (UCameraShakeAsset* ShakeAsset = FCameraShakeManager::Get().Load(CreatedPath))
						{
							BrowserContext.EditorEngine->OpenAssetEditorForObject(ShakeAsset);
						}
					}
				}
			}
			if (ImGui::MenuItem("Anim Graph"))
			{
				FString CreatedPath;
				if (FAssetFactory::CreateAnimGraph(FPaths::ToUtf8(BrowserContext.CurrentPath), "NewAnimGraph", CreatedPath))
				{
					RecordCreatedContentPath(BrowserContext.EditorEngine, CreatedPath, "Create Anim Graph");
					Refresh();
					if (BrowserContext.EditorEngine)
					{
						if (UAnimGraphAsset* GraphAsset = FAnimGraphManager::Get().Load(CreatedPath))
						{
							BrowserContext.EditorEngine->OpenAssetEditorForObject(GraphAsset);
						}
					}
				}
			}
			if (ImGui::MenuItem("Material"))
				{
					FString CreatedPath;
					if (FAssetFactory::CreateMaterial(FPaths::ToUtf8(BrowserContext.CurrentPath), "NewMaterial", CreatedPath))
					{
						RecordCreatedContentPath(BrowserContext.EditorEngine, CreatedPath, "Create Material");
						Refresh();
					}
				}
				if (ImGui::MenuItem("Lua Blueprint"))
			{
				FString CreatedPath;
				if (FAssetFactory::CreateLuaBlueprint(FPaths::ToUtf8(BrowserContext.CurrentPath), "NewLuaBlueprint", CreatedPath))
				{
					RecordCreatedContentPath(BrowserContext.EditorEngine, CreatedPath, "Create Lua Blueprint");
					Refresh();
					if (BrowserContext.EditorEngine)
					{
						if (ULuaBlueprintAsset* Asset = FLuaBlueprintManager::Get().Load(CreatedPath))
						{
							BrowserContext.EditorEngine->OpenAssetEditorForObject(Asset);
						}
					}
				}
			}
			if (ImGui::MenuItem("Runtime UI Layout"))
			{
				FString CreatedPath;
				FString GeneratedRmlPath;
				if (FAssetFactory::CreateRuntimeUILayout(
					FPaths::ToUtf8(BrowserContext.CurrentPath),
					"NewRuntimeUI",
					CreatedPath,
					&GeneratedRmlPath))
				{
					TArray<FString> CreatedPaths{ CreatedPath };
					if (!GeneratedRmlPath.empty())
					{
						CreatedPaths.push_back(GeneratedRmlPath);
						CreatedPaths.push_back(ReplaceExtensionForUndo(GeneratedRmlPath, L".rcss"));
					}
					RecordCreatedContentPaths(BrowserContext.EditorEngine, CreatedPaths, "Create Runtime UI Layout");
					Refresh();
					if (BrowserContext.EditorEngine)
					{
						if (URuntimeUILayoutAsset* Layout = FRuntimeUILayoutManager::Get().Load(CreatedPath))
						{
							BrowserContext.EditorEngine->OpenAssetEditorForObject(Layout);
						}
						else
						{
							BrowserContext.EditorEngine->OpenRuntimeUIPreviewDocument(GeneratedRmlPath);
						}
					}
				}
			}
			if (ImGui::MenuItem("Particle System"))
			{
				FString CreatedPath;
				if (FAssetFactory::CreateParticleSystem(
					FPaths::ToUtf8(BrowserContext.CurrentPath),
					"NewParticleSystem",
					CreatedPath))
				{
					RecordCreatedContentPath(BrowserContext.EditorEngine, CreatedPath, "Create Particle System");
					Refresh();
					if (BrowserContext.EditorEngine)
					{
						if (UParticleSystem* Asset = FParticleSystemManager::Get().Load(CreatedPath))
						{
							BrowserContext.EditorEngine->OpenAssetEditorForObject(Asset);
						}
					}
				}
			}
				if (ImGui::MenuItem("Vector Field"))
				{
					FString CreatedPath;
					if (FAssetFactory::CreateVectorField(FPaths::ToUtf8(BrowserContext.CurrentPath), "NewVectorField", CreatedPath))
					{
						RecordCreatedContentPath(BrowserContext.EditorEngine, CreatedPath, "Create Vector Field");
						Refresh();
					}
				}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("New Folder"))
		{
			std::filesystem::path Dir(BrowserContext.CurrentPath);
			std::filesystem::path NewDir = Dir / L"NewFolder";
			int Suffix = 1;
			while (std::filesystem::exists(NewDir))
				NewDir = Dir / (std::wstring(L"NewFolder_") + std::to_wstring(Suffix++));
			std::error_code Ec;
			std::filesystem::create_directories(NewDir, Ec);
			if (!Ec)
			{
				RecordCreatedContentPath(BrowserContext.EditorEngine, ToContentUndoPath(NewDir), "Create Folder");
				Refresh();
			}
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Refresh"))
		{
			Refresh();
		}

		ImGui::EndPopup();
	}
}

TArray<FContentItem> FEditorContentBrowserWidget::ReadDirectory(std::wstring Path)
{
	TArray<FContentItem> Items;

	if (!std::filesystem::exists(Path) || !std::filesystem::is_directory(Path))
		return Items;

	for (const auto& Entry : std::filesystem::directory_iterator(Path))
	{
		std::wstring Name = Entry.path().filename().wstring();
		if (Entry.is_directory())
		{
			if (Name == L"Bin" || Name == L"Build" || Name == L".git" || Name == L".vs")
				continue;
		}

		FContentItem Item;
		Item.Path = Entry.path();
		Item.Name = Name;
		Item.bIsDirectory = Entry.is_directory();

		Items.push_back(Item);
	}

	std::sort(Items.begin(), Items.end(),
		[](const FContentItem& A, const FContentItem& B)
		{
			if (A.bIsDirectory != B.bIsDirectory)
				return A.bIsDirectory > B.bIsDirectory;

			return A.Name < B.Name;
		});

	return Items;
}

FEditorContentBrowserWidget::FDirNode FEditorContentBrowserWidget::BuildDirectoryTree(const std::filesystem::path& DirPath)
{
	FDirNode Node;
	Node.Self.Path = DirPath;
	Node.Self.Name = DirPath.filename().wstring();
	Node.Self.bIsDirectory = true;

	for (const auto& Entry : std::filesystem::directory_iterator(DirPath))
	{
		if (!Entry.is_directory())
			continue;

		std::wstring DirName = Entry.path().filename().wstring();
		if (DirName == L"Bin" || DirName == L"Build" || DirName == L".git" || DirName == L".vs")
			continue;

		Node.Children.push_back(BuildDirectoryTree(Entry.path()));
	}

	if (Node.Self.Name.empty())
		Node.Self.Name = FPaths::ToWide("Project");

	return Node;
}
