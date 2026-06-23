#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Core/Paths.h"

#include "ImGui/imgui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace
{
    bool HasRuntimeUILayoutExtension(const FString& Path)
    {
        std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
        FString Extension = FPaths::ToUtf8(FsPath.extension().wstring());
        std::transform(Extension.begin(), Extension.end(), Extension.begin(),
            [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
        return Extension == ".uasset";
    }
}

void FEditorMainPanel::OpenRuntimeUIPreviewAsset(const FString& RmlPath)
{
    bool bOpenedLayout = false;
    if (!RmlPath.empty())
    {
        if (HasRuntimeUILayoutExtension(RmlPath))
        {
            bOpenedLayout = Widgets.RuntimeUIPreviewWidget.OpenLayoutAsset(RmlPath);
        }
        else
        {
            Widgets.RuntimeUIPreviewWidget.OpenPreviewDocument(RmlPath);
        }
    }

    const FString DocumentPath = bOpenedLayout
        ? Widgets.RuntimeUIPreviewWidget.GetLayoutAssetPath()
        : Widgets.RuntimeUIPreviewWidget.GetPreviewDocumentPath();
    const FEditorTabId TabId = MakeRuntimeUIPreviewTabId();
    const FString TabLabel = MakeRuntimeUIPreviewTabLabel(DocumentPath);
    EditorTabs.OpenOrFocusTab(TabId, TabLabel);
    EditorTabs.SetTabLabel(TabId, TabLabel);
    ActivateEditorTab(TabId);
}

void FEditorMainPanel::RefreshContentBrowser()
{
    Widgets.ContentBrowserWidget.Refresh();
}

void FEditorMainPanel::QueueRuntimeUIDrawCallback(
    ImDrawList* DrawList,
    const FRuntimeUIRenderContext& Context
)
{
    if (!DrawList || !EditorEngine)
    {
        return;
    }

    FEditorMainPanelPendingRuntimeUIDraw* PendingDraw = new FEditorMainPanelPendingRuntimeUIDraw();
    PendingDraw->Owner = this;
    PendingDraw->Context = Context;
    RuntimeUIDrawState.PendingCallbacks.push_back(PendingDraw);

    DrawList->AddCallback(&FEditorMainPanel::RenderRuntimeUIDrawCallback, PendingDraw);
    DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void FEditorMainPanel::ClearRuntimeUIDrawCallbacks()
{
    for (FEditorMainPanelPendingRuntimeUIDraw* PendingDraw : RuntimeUIDrawState.PendingCallbacks)
    {
        delete PendingDraw;
    }
    RuntimeUIDrawState.PendingCallbacks.clear();
}

void FEditorMainPanel::RenderRuntimeUIDrawCallback(
    const ImDrawList* ParentList,
    const ImDrawCmd* Cmd
)
{
    (void)ParentList;
    if (!Cmd || !Cmd->UserCallbackData)
    {
        return;
    }

    FEditorMainPanelPendingRuntimeUIDraw* PendingDraw =
        static_cast<FEditorMainPanelPendingRuntimeUIDraw*>(Cmd->UserCallbackData);
    if (!PendingDraw || !PendingDraw->Owner || !PendingDraw->Owner->EditorEngine)
    {
        return;
    }

    PendingDraw->Owner->EditorEngine->GetRmlUiSystem().Render(
        PendingDraw->Context,
        PendingDraw->Owner->EditorEngine->GetRenderer()
    );
}
