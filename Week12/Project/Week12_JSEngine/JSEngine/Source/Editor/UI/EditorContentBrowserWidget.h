#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Asset/AssetMetaData.h"
#include "Core/Containers/Set.h"
#include "Render/Common/ComPtr.h"
#include "ImGui/imgui.h"

#include <filesystem>

class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

class FEditorContentBrowserWidget : public FEditorWidget
{
public:
	enum class EPresentationMode
	{
		Drawer,
		FloatingWindow,
	};

	void Initialize(UEditorEngine* InEditorEngine) override;
	void Render(float DeltaTime) override;
	void Refresh();
	void SetVisible(bool bInVisible)
	{
		bVisible = bInVisible;
		if (bVisible && IsFloatingWindowMode())
		{
			bRequestFocusWindow = true;
		}
	}
	bool IsVisible() const { return bVisible; }
	void ToggleVisible()
	{
		bVisible = !bVisible;
		if (bVisible && IsFloatingWindowMode())
		{
			bRequestFocusWindow = true;
		}
	}
	void SetPresentationMode(EPresentationMode InMode)
	{
		PresentationMode = InMode;
		if (IsFloatingWindowMode())
		{
			bRequestFocusWindow = true;
		}
	}
	EPresentationMode GetPresentationMode() const { return PresentationMode; }
	bool IsDrawerMode() const { return PresentationMode == EPresentationMode::Drawer; }
	bool IsFloatingWindowMode() const { return PresentationMode == EPresentationMode::FloatingWindow; }
	void OpenAssetRoot();
	bool IsMouseOverBrowser() const;
	bool ConsumeReleasedDragPayload(FString& OutPayloadType, FString& OutPayloadPath);

private:
	struct FContentItem
	{
		std::filesystem::path Path;
		FString Name;
		FString Extension;
		bool bIsDirectory = false;
		bool bHasAssetMetadata = false;
		FAssetMetaData AssetMetadata;
	};

	struct FDirNode
	{
		std::filesystem::path Path;
		FString Name;
		TArray<FDirNode> Children;
	};

	struct FMaterialPreviewSnapshot
	{
		TComPtr<ID3D11Texture2D> Texture;
		TComPtr<ID3D11ShaderResourceView> SRV;
		uint32 Width = 0;
		uint32 Height = 0;
	};

	void LoadFromSettings();
	void SaveToSettings() const;
	void RefreshContent();
	void RebuildRootNode();
	FDirNode BuildDirectoryTree(const std::filesystem::path& DirPath) const;
	TArray<FContentItem> ReadDirectory(const std::filesystem::path& DirPath) const;
	void DrawBrowserContents();
	void DrawFloatingWindowChrome(bool& bOpen);
	void DrawToolbar();
	void DrawDirectoryNode(const FDirNode& Node);
	void DrawContentGrid();
	void DrawContentTile(const FContentItem& Item, const ImVec2& TileSize);
	void DrawContentContextMenu(bool bHasSelectedItem);
	bool CreateFolder();
	bool CreateTextFile();
	bool CreateLuaScriptFile();
	bool CreateMaterialAsset();
	bool CreateCurveAsset();
	bool CreateAnimGraphAsset();
	bool CreateLuaAnimGraphAsset();
	bool CreateParticleSystemAsset();
	bool CreateRuntimeUILayoutAsset();
	bool CreateSceneAsset();
	bool DeleteSelectedItem();
	void RequestRenameSelectedItem();
	bool CommitRename();
	void DrawRenamePopup();
	std::filesystem::path MakeUniquePath(const std::filesystem::path& DesiredPath) const;
	void DrawFolderIcon(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color) const;
	void DrawDetails();
	void DrawAssetPreview();
	void NavigateTo(const std::filesystem::path& Path);
	void NavigateTo(const std::filesystem::path& Path, bool bAddHistory);
	void NavigateBack();
	FString MakeDisplayPath(const std::filesystem::path& Path) const;
	FString GetPayloadType(const FContentItem& Item) const;
	ImU32 GetItemColor(const FContentItem& Item) const;
	ID3D11ShaderResourceView* GetImagePreviewSRV(const FContentItem& Item);
	ID3D11ShaderResourceView* GetAnimSequenceIconSRV();
	ID3D11ShaderResourceView* GetMaterialPreviewSRV(const FContentItem& Item, uint32 Width, uint32 Height, bool bHighPriority = false);
	ID3D11ShaderResourceView* GetStaticMeshPreviewSRV(const FContentItem& Item, bool bHighPriority = false);
	ID3D11ShaderResourceView* GetSkeletalMeshPreviewSRV(const FContentItem& Item, bool bHighPriority = false);
	bool CapturePreviewSnapshot(ID3D11ShaderResourceView* SourceSRV, FMaterialPreviewSnapshot& OutSnapshot, uint32 Width, uint32 Height);
	UMaterialInterface* ResolveMaterialAsset(const std::filesystem::path& Path);
	bool IsPathAllowed(const std::filesystem::path& Path) const;
	bool IsProjectRootPath(const std::filesystem::path& Path) const;
	bool IsPreviewableImage(const FString& Extension) const;
	bool IsMaterialAsset(const FString& Extension) const;
	bool IsMaterialAsset(const FContentItem& Item) const;
	bool IsStaticMeshAsset(const FContentItem& Item) const;
	bool IsSkeletalMeshAsset(const FContentItem& Item) const;
	bool IsRawMeshSource(const FContentItem& Item) const;
	bool ImportRawMeshSource(const FContentItem& Item);
	bool IsCurveAsset(const std::filesystem::path& Path) const;
	bool IsSequenceAsset(const FString& Extension) const;
	bool IsAnimGraphAsset(const FString& Extension) const;
	bool IsAnimGraphAsset(const FContentItem& Item) const;
	bool IsAnimLuaProgramAsset(const FContentItem& Item) const;
	bool IsParticleSystemAsset(const FContentItem& Item) const;
	bool IsPrefabAsset(const FString& Extension) const;
	bool IsRuntimeUILayoutAsset(const FContentItem& Item) const;
	std::filesystem::path ResolveLuaScriptCreateDirectory() const;
	FString MakeRelativeProjectPath(const std::filesystem::path& Path) const;

private:
	FDirNode RootNode;
	TArray<FContentItem> CurrentItems;
	TArray<std::filesystem::path> BrowserRootPaths;
	std::filesystem::path RootPath;
	std::filesystem::path CurrentPath;
	std::filesystem::path PendingRevealPath;
	std::filesystem::path SelectedPath;
	TArray<std::filesystem::path> BackHistory;
	TMap<FString, FMaterialPreviewSnapshot> MaterialPreviewCache;
	TMap<FString, FMaterialPreviewSnapshot> StaticMeshPreviewCache;
	TMap<FString, FMaterialPreviewSnapshot> SkeletalMeshPreviewCache;
	TSet<FString> FailedPreviewCacheKeys;
	TComPtr<ID3D11ShaderResourceView> AnimSequenceIconSRV;
	FString SearchFilter;
	char RenameBuffer[260] = {};
	float TileSize = 72.0f;
	float AnimAlpha = 0.0f;
	int32 MaterialPreviewBuildsThisFrame = 0;
	bool bVisible = false;
	bool bNeedsRefresh = true;
	bool bPendingMaterialPreviewCacheClear = false;
	bool bAnimSequenceIconLoadAttempted = false;
	bool bRenamePopupRequested = false;
	bool bRequestFocusWindow = false;
	bool bMouseOverBrowser = false;
	bool bHasBrowserScreenRect = false;
	bool bOpenContentContextMenu = false;
	bool bContentContextMenuHasSelection = false;
	ImVec2 BrowserScreenMin = ImVec2(0.0f, 0.0f);
	ImVec2 BrowserScreenMax = ImVec2(0.0f, 0.0f);
	EPresentationMode PresentationMode = EPresentationMode::Drawer;
	UStaticMesh* MaterialPreviewMesh = nullptr;
	std::filesystem::path RenameSourcePath;
	FString ActiveDragPayloadType;
	FString ActiveDragPayloadPath;
};
