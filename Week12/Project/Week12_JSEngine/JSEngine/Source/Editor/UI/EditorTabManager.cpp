#include "Editor/UI/EditorTabManager.h"

#include "Core/AssetPathPolicy.h"

#include <cstdio>
#include <utility>

namespace
{
	bool IsAnimSequenceViewerPath(const FString& Path)
	{
		return FAssetPathPolicy::IsAnimSequenceAssetPath(Path);
	}

	bool IsStaticMeshViewerPath(const FString& Path)
	{
		return FAssetPathPolicy::IsStaticMeshAssetPath(Path);
	}
}

bool FEditorTabId::Matches(const FEditorTabId& Other) const
{
	return Kind == Other.Kind && PayloadId == Other.PayloadId;
}

FEditorTabId MakeEditorViewerTabId(const FString& ViewerFileName, const void* FallbackAddress)
{
	FEditorTabId TabId;
	if (IsAnimSequenceViewerPath(ViewerFileName))
	{
		TabId.Kind = EEditorTabKind::AnimSequenceViewer;
	}
	else if (IsStaticMeshViewerPath(ViewerFileName))
	{
		TabId.Kind = EEditorTabKind::StaticMeshViewer;
	}
	else
	{
		TabId.Kind = EEditorTabKind::SkeletalMeshViewer;
	}
	TabId.PayloadId = ViewerFileName;
	if (TabId.PayloadId.empty() && FallbackAddress)
	{
		char Buffer[64];
		snprintf(Buffer, sizeof(Buffer), "__Viewer_%p", FallbackAddress);
		TabId.PayloadId = Buffer;
	}
	return TabId;
}

FString MakeEditorViewerTabLabel(const FString& ViewerFileName)
{
	if (ViewerFileName.empty())
	{
		return "Skeletal Mesh Viewer";
	}

	const bool bAnimSequence = IsAnimSequenceViewerPath(ViewerFileName);
	const bool bStaticMesh = IsStaticMeshViewerPath(ViewerFileName);

	const size_t SlashIndex = ViewerFileName.find_last_of("/\\");
	FString FileName = SlashIndex == FString::npos ? ViewerFileName : ViewerFileName.substr(SlashIndex + 1);
	if (bAnimSequence)
	{
		return FString("Anim: ") + FileName;
	}
	if (bStaticMesh)
	{
		return FString("Static: ") + FileName;
	}
	return FileName;
}

FEditorTabId MakeRuntimeUIPreviewTabId()
{
	FEditorTabId TabId;
	TabId.Kind = EEditorTabKind::RuntimeUIPreview;
	TabId.PayloadId = "__RuntimeUIPreview";
	return TabId;
}

FString MakeRuntimeUIPreviewTabLabel(const FString& DocumentPath)
{
	if (DocumentPath.empty())
	{
		return "Runtime UI Preview";
	}

	const size_t SlashIndex = DocumentPath.find_last_of("/\\");
	const FString FileName = SlashIndex == FString::npos ? DocumentPath : DocumentPath.substr(SlashIndex + 1);
	return FileName.empty() ? "Runtime UI Preview" : FileName;
}

FEditorTabId MakeAnimGraphEditorTabId(const FString& AnimGraphPath)
{
	FEditorTabId TabId;
	TabId.Kind = EEditorTabKind::AnimGraphEditor;
	TabId.PayloadId = AnimGraphPath;
	return TabId;
}

FString MakeAnimGraphEditorTabLabel(const FString& AnimGraphPath)
{
	if (AnimGraphPath.empty())
	{
		return "Anim Graph";
	}

	const size_t SlashIndex = AnimGraphPath.find_last_of("/\\");
	const FString FileName = SlashIndex == FString::npos ? AnimGraphPath : AnimGraphPath.substr(SlashIndex + 1);
	return FileName.empty() ? "Anim Graph" : FileName;
}

FEditorTabId MakeLuaAnimGraphEditorTabId(const FString& AssetPath)
{
	FEditorTabId TabId;
	TabId.Kind = EEditorTabKind::LuaAnimGraphEditor;
	TabId.PayloadId = AssetPath;
	return TabId;
}

FString MakeLuaAnimGraphEditorTabLabel(const FString& AssetPath)
{
	if (AssetPath.empty())
	{
		return "Lua Anim Graph";
	}

	const size_t SlashIndex = AssetPath.find_last_of("/\\");
	const FString FileName = SlashIndex == FString::npos ? AssetPath : AssetPath.substr(SlashIndex + 1);
	return FileName.empty() ? "Lua Anim Graph" : FileName;
}

FEditorTabId MakeParticleSystemEditorTabId(const FString& ParticleSystemPath)
{
	FEditorTabId TabId;
	TabId.Kind = EEditorTabKind::ParticleSystemEditor;
	TabId.PayloadId = ParticleSystemPath;
	return TabId;
}

FString MakeParticleSystemEditorTabLabel(const FString& ParticleSystemPath)
{
	if (ParticleSystemPath.empty())
	{
		return "Particle System";
	}

	const size_t SlashIndex = ParticleSystemPath.find_last_of("/\\");
	const FString FileName = SlashIndex == FString::npos ? ParticleSystemPath : ParticleSystemPath.substr(SlashIndex + 1);
	return FileName.empty() ? "Particle System" : FileName;
}

void FEditorTabManager::ResetToLevelEditor()
{
	Tabs.clear();

	FEditorTabEntry LevelEditorTab;
	LevelEditorTab.Id.Kind = EEditorTabKind::LevelEditor;
	LevelEditorTab.Label = "Untitled";
	LevelEditorTab.bCanClose = false;

	Tabs.emplace_back(std::move(LevelEditorTab));
	ActiveTabIndex = 0;
}

const FEditorTabEntry& FEditorTabManager::OpenOrFocusTab(const FEditorTabId& Id, const FString& Label, bool bCanClose)
{
	const int32 ExistingIndex = FindTabIndex(Id);
	if (ExistingIndex >= 0)
	{
		ActiveTabIndex = ExistingIndex;
		return Tabs[ExistingIndex];
	}

	FEditorTabEntry NewTab;
	NewTab.Id = Id;
	NewTab.Label = Label;
	NewTab.bCanClose = bCanClose;

	Tabs.emplace_back(std::move(NewTab));
	ActiveTabIndex = static_cast<int32>(Tabs.size()) - 1;
	return Tabs[ActiveTabIndex];
}

bool FEditorTabManager::CloseTab(const FEditorTabId& Id)
{
	const int32 Index = FindTabIndex(Id);
	if (Index < 0 || !Tabs[Index].bCanClose)
	{
		return false;
	}

	Tabs.erase(Tabs.begin() + Index);
	if (Tabs.empty())
	{
		ResetToLevelEditor();
		return true;
	}

	if (ActiveTabIndex >= Index)
	{
		ActiveTabIndex = std::max(0, ActiveTabIndex - 1);
	}
	if (ActiveTabIndex >= static_cast<int32>(Tabs.size()))
	{
		ActiveTabIndex = static_cast<int32>(Tabs.size()) - 1;
	}

	return true;
}

bool FEditorTabManager::SetActiveTab(const FEditorTabId& Id)
{
	const int32 Index = FindTabIndex(Id);
	if (Index < 0)
	{
		return false;
	}

	ActiveTabIndex = Index;
	return true;
}

bool FEditorTabManager::ReplaceTab(const FEditorTabId& OldId, const FEditorTabId& NewId, const FString& Label)
{
	const int32 Index = FindTabIndex(OldId);
	if (Index < 0)
	{
		return false;
	}

	Tabs[Index].Id = NewId;
	Tabs[Index].Label = Label;
	return true;
}

bool FEditorTabManager::SetTabLabel(const FEditorTabId& Id, const FString& Label)
{
	const int32 Index = FindTabIndex(Id);
	if (Index < 0)
	{
		return false;
	}

	Tabs[Index].Label = Label;
	return true;
}

bool FEditorTabManager::SetTabDirty(const FEditorTabId& Id, bool bDirty)
{
	const int32 Index = FindTabIndex(Id);
	if (Index < 0)
	{
		return false;
	}

	Tabs[Index].bDirty = bDirty;
	return true;
}

const FEditorTabEntry* FEditorTabManager::GetActiveTab() const
{
	if (ActiveTabIndex < 0 || ActiveTabIndex >= static_cast<int32>(Tabs.size()))
	{
		return nullptr;
	}

	return &Tabs[ActiveTabIndex];
}

EEditorTabKind FEditorTabManager::GetActiveTabKind() const
{
	const FEditorTabEntry* ActiveTab = GetActiveTab();
	return ActiveTab ? ActiveTab->Id.Kind : EEditorTabKind::LevelEditor;
}

const TArray<FEditorTabEntry>& FEditorTabManager::GetTabs() const
{
	return Tabs;
}

bool FEditorTabManager::MoveTab(int32 FromIndex, int32 ToIndex)
{
	if (FromIndex <= 0 || ToIndex <= 0 ||
		FromIndex >= static_cast<int32>(Tabs.size()) ||
		ToIndex >= static_cast<int32>(Tabs.size()) ||
		FromIndex == ToIndex)
	{
		return false;
	}

	FEditorTabId ActiveId;
	const bool bHadActive = GetActiveTab() != nullptr;
	if (bHadActive)
	{
		ActiveId = Tabs[ActiveTabIndex].Id;
	}

	FEditorTabEntry MovingTab = std::move(Tabs[FromIndex]);
	Tabs.erase(Tabs.begin() + FromIndex);
	Tabs.insert(Tabs.begin() + ToIndex, std::move(MovingTab));

	if (bHadActive)
	{
		ActiveTabIndex = FindTabIndex(ActiveId);
	}
	return true;
}

bool FEditorTabManager::MoveTab(const FEditorTabId& FromId, const FEditorTabId& ToId)
{
	const int32 FromIndex = FindTabIndex(FromId);
	const int32 ToIndex = FindTabIndex(ToId);
	return MoveTab(FromIndex, ToIndex);
}

bool FEditorTabManager::SetTabDetached(const FEditorTabId& Id, bool bDetached)
{
	const int32 Index = FindTabIndex(Id);
	if (Index <= 0 || Index >= static_cast<int32>(Tabs.size()))
	{
		return false;
	}

	Tabs[Index].bDetached = bDetached;
	return true;
}

bool FEditorTabManager::IsTabDetached(const FEditorTabId& Id) const
{
	const int32 Index = FindTabIndex(Id);
	return Index >= 0 && Tabs[Index].bDetached;
}

const FEditorTabEntry* FEditorTabManager::FindTab(const FEditorTabId& Id) const
{
	const int32 Index = FindTabIndex(Id);
	return Index >= 0 ? &Tabs[Index] : nullptr;
}

int32 FEditorTabManager::FindTabIndex(const FEditorTabId& Id) const
{
	for (int32 Index = 0; Index < static_cast<int32>(Tabs.size()); ++Index)
	{
		if (Tabs[Index].Id.Matches(Id))
		{
			return Index;
		}
	}

	return -1;
}
