#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/Notification/EditorNotificationService.h"

void FEditorMainPanel::OpenLuaAnimGraphAsset(const FString& AssetPath)
{
    if (!EditorEngine || AssetPath.empty())
    {
        return;
    }

    if (!Widgets.LuaAnimGraphWidget.OpenAsset(AssetPath))
    {
        EditorEngine->GetNotificationService().Error("Failed to open Lua Anim Graph asset.");
        return;
    }

    const FString OpenedPath = Widgets.LuaAnimGraphWidget.GetAssetPath();
    const FEditorTabId TabId = MakeLuaAnimGraphEditorTabId(OpenedPath);
    const FString TabLabel = MakeLuaAnimGraphEditorTabLabel(OpenedPath);
    EditorTabs.OpenOrFocusTab(TabId, TabLabel);
    EditorTabs.SetTabLabel(TabId, TabLabel);
    ActivateEditorTab(TabId);
}

void FEditorMainPanel::OpenBlueprintAsset(const FString& AssetPath)
{
    if (!EditorEngine || AssetPath.empty())
    {
        return;
    }

    if (!Widgets.BlueprintWidget.OpenAsset(AssetPath))
    {
        EditorEngine->GetNotificationService().Error("Failed to open Blueprint asset.");
        return;
    }

    const FEditorTabId TabId = MakeBlueprintEditorTabId(AssetPath);
    const FString TabLabel = MakeBlueprintEditorTabLabel(AssetPath);
    EditorTabs.OpenOrFocusTab(TabId, TabLabel);
    EditorTabs.SetTabLabel(TabId, TabLabel);
    ActivateEditorTab(TabId);

	Widgets.BlueprintWidget.SetContextObject(Widgets.PropertyWidget.GetSelectedComponent());
}
