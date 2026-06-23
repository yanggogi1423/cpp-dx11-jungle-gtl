#include "Editor/UI/EditorMainPanel.h"

#include "Component/SkinnedMeshComponent.h"
#include "Editor/EditorEngine.h"
#include "Editor/Settings/EditorSettings.h"
#include "Render/Common/ProjectRenderSettings.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include "ImGui/imgui.h"

void FEditorMainPanel::InvalidateSkinningForFocusedWorld()
{
    UWorld* World = EditorEngine ? EditorEngine->GetFocusedWorld() : nullptr;
    if (!World)
    {
        return;
    }

    for (AActor* Actor : World->GetActors())
    {
        if (!Actor)
        {
            continue;
        }

        for (UActorComponent* Component : Actor->GetComponents())
        {
            USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component);
            if (!SkinnedMeshComponent)
            {
                continue;
            }

            SkinnedMeshComponent->MarkSkinningDirty();
        }
    }
}

void FEditorMainPanel::RenderProjectRenderingSettingsSection()
{
    ImGui::TextUnformatted("Rendering");
    ImGui::Separator();

    const char* SkinningModeItems[] = { "CPU Skinning", "GPU Skinning" };
    int32 SkinningModeIndex = FProjectRenderSettings::IsGPUSkinningEnabled() ? 1 : 0;

    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::Combo("Skinning Mode", &SkinningModeIndex, SkinningModeItems, IM_ARRAYSIZE(SkinningModeItems)))
    {
        const ESkinningMode NewMode = SkinningModeIndex == 1 ? ESkinningMode::GPU : ESkinningMode::CPU;
        if (NewMode != FProjectRenderSettings::GetSkinningMode())
        {
            FProjectRenderSettings::SetSkinningMode(NewMode);
            FEditorSettings::Get().SaveToFile(FEditorSettings::GetDefaultSettingsPath());
            InvalidateSkinningForFocusedWorld();

            PushFooterLog(
                NewMode == ESkinningMode::GPU
                    ? "Editor skinning mode changed to GPU Skinning."
                    : "Editor skinning mode changed to CPU Skinning.");
        }
    }
}

void FEditorMainPanel::OpenProjectSettingsPanel()
{
    PanelVisibility.bShowProjectSettings = true;
    if (ImGuiViewport* Viewport = ImGui::GetWindowViewport())
    {
        PendingProjectSettingsWindowViewportId = Viewport->ID;
    }
}

void FEditorMainPanel::OpenWorldSettingsPanel()
{
    PanelVisibility.bShowWorldSettings = true;
    if (ImGuiViewport* Viewport = ImGui::GetWindowViewport())
    {
        PendingWorldSettingsWindowViewportId = Viewport->ID;
    }
}

void FEditorMainPanel::OpenEditorSettingsPanel()
{
    PanelVisibility.bShowEditorSettings = true;
    if (ImGuiViewport* Viewport = ImGui::GetWindowViewport())
    {
        PendingEditorSettingsWindowViewportId = Viewport->ID;
    }
}

void FEditorMainPanel::ToggleViewportSettingsPanel()
{
    const bool bVisible = Widgets.ViewportOverlayWidget.IsViewportSettingsVisible();
    Widgets.ViewportOverlayWidget.SetViewportSettingsVisible(!bVisible);
}

bool FEditorMainPanel::IsViewportSettingsPanelVisible() const
{
    return Widgets.ViewportOverlayWidget.IsViewportSettingsVisible();
}

void FEditorMainPanel::ApplyPendingSettingsWindowViewport(ImGuiID& PendingViewportId)
{
    if (PendingViewportId == 0)
    {
        return;
    }

    ImGui::SetNextWindowViewport(PendingViewportId);
    PendingViewportId = 0;
}

void FEditorMainPanel::RenderProjectSettingsPanel()
{
    if (!GameModeSettingsState.bLoaded)
    {
        LoadGameModeSettingsPanelBuffers();
    }

    ApplyPendingSettingsWindowViewport(PendingProjectSettingsWindowViewportId);
    ImGui::SetNextWindowSize(ImVec2(500.0f, 360.0f), ImGuiCond_FirstUseEver);
    bool bOpen = PanelVisibility.bShowProjectSettings;
    if (!ImGui::Begin("Project Settings", &bOpen))
    {
        PanelVisibility.bShowProjectSettings = bOpen;
        ImGui::End();
        return;
    }
    PanelVisibility.bShowProjectSettings = bOpen;

    RenderProjectGameModeSettingsSection();

    ImGui::End();
}

void FEditorMainPanel::RenderEditorSettingsPanel()
{
    ApplyPendingSettingsWindowViewport(PendingEditorSettingsWindowViewportId);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 220.0f), ImGuiCond_FirstUseEver);
    bool bOpen = PanelVisibility.bShowEditorSettings;
    if (!ImGui::Begin("Editor Settings", &bOpen))
    {
        PanelVisibility.bShowEditorSettings = bOpen;
        ImGui::End();
        return;
    }
    PanelVisibility.bShowEditorSettings = bOpen;

    RenderProjectRenderingSettingsSection();

    ImGui::Separator();
    if (ImGui::Button("Save Editor Settings", ImVec2(170.0f, 0.0f)))
    {
        FEditorSettings::Get().SaveToFile(FEditorSettings::GetDefaultSettingsPath());
        PushFooterLog("Editor settings saved.");
    }

    ImGui::End();
}
