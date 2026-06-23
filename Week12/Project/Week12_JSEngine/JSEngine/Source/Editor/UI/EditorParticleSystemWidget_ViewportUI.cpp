// Renders the particle system editor chrome, viewport panel, toolbar, and preview UI menus.
#include "Editor/UI/EditorParticleSystemWidgetPrivate.h"

void FEditorParticleSystemWidget::Render(float DeltaTime)
{
	RenderEmbedded(DeltaTime);
}

void FEditorParticleSystemWidget::RenderEmbedded(float DeltaTime)
{
	LastDeltaTime = DeltaTime;
	if (CenterToastRemainingTime > 0.0f)
	{
		CenterToastRemainingTime = std::max(0.0f, CenterToastRemainingTime - DeltaTime);
		if (CenterToastRemainingTime <= 0.0f)
		{
			CenterToastMessage.clear();
		}
	}

	EnsurePreviewViewport();
	DrivePreviewPlayback(DeltaTime);
	bPreviewViewportVisible = false;
	bPreviewViewportRectValid = false;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.058f, 0.070f, 1.0f));
	const ImVec2 ToastAreaMin = ImGui::GetCursorScreenPos();
	const ImVec2 ToastAreaSize = ImGui::GetContentRegionAvail();
	DrawMainLayout();
	DrawCenterToast(ToastAreaMin, ToastAreaSize);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

void FEditorParticleSystemWidget::LoadCascadeToolbarIcons()
{
	if (bCascadeToolbarIconsLoadAttempted)
	{
		return;
	}
	bCascadeToolbarIconsLoadAttempted = true;

	if (!EditorEngine)
	{
		return;
	}

	ID3D11Device* Device = EditorEngine->GetRenderer().GetFD3DDevice().GetDevice();
	if (!Device)
	{
		return;
	}

	static constexpr const wchar_t* IconFiles[CascadeToolbarIconCount] =
	{
		L"icon_file_save_40x.png",
		L"icon_toolbar_genericfinder_40px.png",
		L"icon_Cascade_RestartSim_40x.png",
		L"icon_Cascade_RestartInLevel_40x.png",
		L"icon_Generic_Undo_40x.png",
		L"icon_Generic_Redo_40x.png",
		L"icon_Cascade_Thumbnail_40x.png",
		L"icon_Cascade_Bounds_40x.png",
		L"icon_Cascade_Axis_40x.png",
		L"icon_Cascade_Color_40x.png",
		L"icon_Cascade_RegenLOD1_40x.png",
		L"icon_Cascade_LowestLOD_40x.png",
		L"icon_Cascade_LowerLOD_40x.png",
		L"icon_Cascade_AddLOD1_40x.png",
		L"icon_Cascade_AddLOD2_40x.png",
		L"icon_Cascade_HigherLOD_40x.png",
		L"icon_Cascade_HighestLOD_40x.png",
		L"icon_Cascade_DeleteLOD_40x.png"
	};

	const std::wstring IconDir = FEditorResourcePaths::CascadeAbsoluteDir();
	for (int32 IconIndex = 0; IconIndex < CascadeToolbarIconCount; ++IconIndex)
	{
		if (CascadeToolbarIcons[IconIndex])
		{
			continue;
		}

		const std::wstring IconPath = IconDir + IconFiles[IconIndex];
		DirectX::CreateWICTextureFromFile(Device, IconPath.c_str(), nullptr, CascadeToolbarIcons[IconIndex].GetAddressOf());
	}
}

ID3D11ShaderResourceView* FEditorParticleSystemWidget::GetCascadeToolbarIcon(ECascadeToolbarIcon Icon) const
{
	const int32 IconIndex = static_cast<int32>(Icon);
	if (IconIndex < 0 || IconIndex >= CascadeToolbarIconCount)
	{
		return nullptr;
	}
	return CascadeToolbarIcons[IconIndex].Get();
}

void FEditorParticleSystemWidget::RenderDocumentToolbarControls()
{
	LoadCascadeToolbarIcons();

	constexpr float ToolbarSidePadding = 4.0f;
	constexpr float ToolbarControlHeight = 28.0f;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ToolbarSidePadding);
	if (ToolbarButton("Save", "", GetCascadeToolbarIcon(ECascadeToolbarIcon::Save), "Save particle system"))
	{
		Save();
	}
	SameLineGap();
	if (ToolbarButton("FindInContentBrowser", "", GetCascadeToolbarIcon(ECascadeToolbarIcon::Find), "Find in Content Browser"))
	{
		if (EditorEngine)
		{
			EditorEngine->GetMainPanel().RequestToggleContentBrowser();
		}
	}

	SameLineGap(21.0f);
	if (ToolbarButton("RestartSim", "Restart Sim", GetCascadeToolbarIcon(ECascadeToolbarIcon::RestartSim), "Restart simulation"))
	{
		RefreshPreviewComponent(true);
	}

	SameLineGap(7.0f);
	ImGui::BeginDisabled(!CanUndo());
	if (ToolbarButton("Undo", "Undo", GetCascadeToolbarIcon(ECascadeToolbarIcon::Undo), "Undo"))
	{
		Undo();
	}
	ImGui::EndDisabled();
	SameLineGap();
	ImGui::BeginDisabled(!CanRedo());
	if (ToolbarButton("Redo", "Redo", GetCascadeToolbarIcon(ECascadeToolbarIcon::Redo), "Redo"))
	{
		Redo();
	}
	ImGui::EndDisabled();

	SameLineGap();
	if (ToolbarButton("BackgroundColor", "Background Color", GetCascadeToolbarIcon(ECascadeToolbarIcon::BackgroundColor), "Change preview background color"))
	{
		ImGui::OpenPopup("##ParticlePreviewBackgroundColorPopup");
	}
	DrawBackgroundColorPopup();

	SameLineGap(7.0f);
	int32 MaxLODCount = GetMaxLODCount();
	ImGui::BeginDisabled(MaxLODCount <= 0 || CurrentLOD >= MaxLODCount - 1);
	if (ToolbarButton("LowestLOD", "Lowest LOD", GetCascadeToolbarIcon(ECascadeToolbarIcon::LowestLOD), "Switch to the farthest LOD"))
	{
		SelectLowestLOD();
	}
	SameLineGap();
	if (ToolbarButton("LowerLOD", "Lower LOD", GetCascadeToolbarIcon(ECascadeToolbarIcon::LowerLOD), "Switch to the next lower LOD"))
	{
		SelectLowerLOD();
	}
	ImGui::EndDisabled();
	SameLineGap();
	ImGui::BeginDisabled(!ParticleSystemAsset || ParticleSystemAsset->Emitters.empty());
	if (ToolbarButton(
		"AddLODBeforeCurrent",
		"Add LOD Before",
		GetCascadeToolbarIcon(ECascadeToolbarIcon::AddLODBeforeCurrent),
		"Add LOD before current"))
	{
		AddLODRelativeToCurrent(0);
	}
	SameLineGap();
	if (ToolbarButton(
		"AddLODAfterCurrent",
		"Add LOD After",
		GetCascadeToolbarIcon(ECascadeToolbarIcon::AddLODAfterCurrent),
		"Add LOD after current"))
	{
		AddLODRelativeToCurrent(1);
	}
	ImGui::EndDisabled();
	MaxLODCount = GetMaxLODCount();

	SameLineGap(8.0f);
	const float LODFramePaddingY = std::max(0.0f, (ToolbarControlHeight - ImGui::GetFontSize()) * 0.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, LODFramePaddingY));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
	ImGui::SetNextItemWidth(38.0f);
	const int32 PreviousLOD = CurrentLOD;
	ImGui::InputInt("LOD", &CurrentLOD, 0, 0);
	ImGui::PopStyleVar(2);
	if (CurrentLOD != PreviousLOD)
	{
		SetCurrentLOD(CurrentLOD);
	}
	else
	{
		CurrentLOD = MaxLODCount > 0 ? std::clamp(CurrentLOD, 0, MaxLODCount - 1) : std::max(0, CurrentLOD);
	}

	SameLineGap();
	ImGui::BeginDisabled(CurrentLOD <= 0);
	if (ToolbarButton("HigherLOD", "Higher LOD", GetCascadeToolbarIcon(ECascadeToolbarIcon::HigherLOD), "Switch to the next higher LOD"))
	{
		SelectHigherLOD();
	}
	SameLineGap();
	if (ToolbarButton("HighestLOD", "Highest LOD", GetCascadeToolbarIcon(ECascadeToolbarIcon::HighestLOD), "Switch to LOD 0"))
	{
		SetCurrentLOD(0);
	}
	ImGui::EndDisabled();
	SameLineGap();
	ImGui::BeginDisabled(CurrentLOD <= 0 || MaxLODCount <= 1);
	if (ToolbarButton("DeleteLOD", "Delete LOD", GetCascadeToolbarIcon(ECascadeToolbarIcon::DeleteLOD), "Delete current LOD"))
	{
		DeleteCurrentLOD();
	}
	ImGui::EndDisabled();
}

void FEditorParticleSystemWidget::DrawBackgroundColorPopup()
{
	if (!BeginParticlePopup("##ParticlePreviewBackgroundColorPopup"))
	{
		return;
	}

	FColor BackgroundColor = PreviewClient.GetBackgroundColor();
	float ColorValues[3] =
	{
		std::clamp(BackgroundColor.R, 0.0f, 1.0f),
		std::clamp(BackgroundColor.G, 0.0f, 1.0f),
		std::clamp(BackgroundColor.B, 0.0f, 1.0f)
	};

	const ImGuiColorEditFlags ColorFlags =
		ImGuiColorEditFlags_NoAlpha |
		ImGuiColorEditFlags_DisplayRGB |
		ImGuiColorEditFlags_InputRGB |
		ImGuiColorEditFlags_Uint8 |
		ImGuiColorEditFlags_PickerHueBar;

	bool bChanged = false;
	ImGui::SetNextItemWidth(260.0f);
	bChanged |= ImGui::ColorPicker3("##ParticlePreviewBackgroundPicker", ColorValues, ColorFlags);

	ImGui::Separator();
	ImGui::SetNextItemWidth(260.0f);
	bChanged |= ImGui::ColorEdit3(
		"RGB",
		ColorValues,
		ColorFlags | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoSmallPreview);

	if (ImGui::Button("Reset"))
	{
		const FColor DefaultColor = FParticleSystemViewportClient::GetDefaultBackgroundColor();
		ColorValues[0] = DefaultColor.R;
		ColorValues[1] = DefaultColor.G;
		ColorValues[2] = DefaultColor.B;
		bChanged = true;
	}

	if (bChanged)
	{
		BackgroundColor = FColor(
			std::clamp(ColorValues[0], 0.0f, 1.0f),
			std::clamp(ColorValues[1], 0.0f, 1.0f),
			std::clamp(ColorValues[2], 0.0f, 1.0f),
			1.0f);
		PreviewClient.SetBackgroundColor(BackgroundColor);
	}

	EndParticlePopup();
}

void FEditorParticleSystemWidget::RenderDocumentViewMenu()
{
	if (BeginParticleMenu("View"))
	{
		if (ImGui::MenuItem("Thumbnail", nullptr, bShowThumbnail))
		{
			bShowThumbnail = !bShowThumbnail;
		}
		if (ImGui::MenuItem("Bounds", nullptr, bShowBounds))
		{
			SetPreviewBoundsVisible(!bShowBounds);
		}
		if (ImGui::MenuItem("Origin Axis", nullptr, bShowOriginAxis))
		{
			SetPreviewOriginAxisVisible(!bShowOriginAxis);
		}
		EndParticleMenu();
	}
}

void FEditorParticleSystemWidget::RenderDocumentParticleMenu()
{
	if (BeginParticleMenu("Particle"))
	{
		if (ImGui::MenuItem("Restart Simulation"))
		{
			RefreshPreviewComponent(true);
			RestartPreviewPlayback();
		}
		ImGui::MenuItem("Restart Level", nullptr, false, false);
		ImGui::Separator();
		ImGui::BeginDisabled(!ParticleSystemAsset || ParticleSystemAsset->Emitters.empty());
		if (ImGui::MenuItem("Add LOD Before Current"))
		{
			AddLODRelativeToCurrent(0);
		}
		if (ImGui::MenuItem("Add LOD After Current"))
		{
			AddLODRelativeToCurrent(1);
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(CurrentLOD <= 0);
		if (ImGui::MenuItem("Delete Current LOD"))
		{
			DeleteCurrentLOD();
		}
		ImGui::EndDisabled();
		EndParticleMenu();
	}
}

void FEditorParticleSystemWidget::RenderDetachedDocumentChrome(bool& bDockRequested, bool& bCloseRequested)
{
	FEditorDetachedWindowChrome::RenderMenuBar(
		"Particle System Editor",
		"ParticleSystemEditor",
		[this, &bDockRequested]()
		{
			if (BeginParticleMenu("File"))
			{
				if (ImGui::MenuItem(bDirty ? "Save *" : "Save", "Ctrl+S"))
				{
					Save();
				}
				ImGui::MenuItem("Save As...", nullptr, false, false);
				EndParticleMenu();
			}
			if (BeginParticleMenu("Edit"))
			{
				if (ImGui::MenuItem("Undo", "Ctrl+Z", false, CanUndo()))
				{
					Undo();
				}
				if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, CanRedo()))
				{
					Redo();
				}
				EndParticleMenu();
			}
			RenderDocumentViewMenu();
			RenderDocumentParticleMenu();
			if (BeginParticleMenu("Window"))
			{
				if (ImGui::MenuItem("Dock Back"))
				{
					bDockRequested = true;
				}
				if (ImGui::MenuItem("Reset Layout"))
				{
					TopAreaHeight = 0.0f;
					TopLeftWidth = 0.0f;
					BottomLeftWidth = 0.0f;
				}
				EndParticleMenu();
			}
			if (BeginParticleMenu("Settings"))
			{
				if (EditorEngine)
				{
					FEditorMainPanel& MainPanel = EditorEngine->GetMainPanel();
					if (ImGui::MenuItem("Editor Settings"))
					{
						MainPanel.OpenEditorSettingsPanel();
					}
					if (ImGui::MenuItem("Project Settings"))
					{
						MainPanel.OpenProjectSettingsPanel();
					}
					if (ImGui::MenuItem("World Settings"))
					{
						MainPanel.OpenWorldSettingsPanel();
					}
				}
				EndParticleMenu();
			}
			if (BeginParticleMenu("Help"))
			{
				ImGui::TextDisabled("Particle System Editor");
				if (!DocumentPath.empty())
				{
					ImGui::Separator();
					ImGui::TextDisabled("%s", DocumentPath.c_str());
				}
				EndParticleMenu();
			}
		},
		bCloseRequested);
}

void FEditorParticleSystemWidget::DrawMainLayout()
{
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	if (Available.x <= 16.0f || Available.y <= 16.0f)
	{
		return;
	}

	if (TopAreaHeight <= 0.0f)
	{
		TopAreaHeight = Available.y * 0.54f;
	}
	if (TopLeftWidth <= 0.0f)
	{
		TopLeftWidth = Available.x * 0.41f;
	}
	if (BottomLeftWidth <= 0.0f)
	{
		BottomLeftWidth = Available.x * 0.41f;
	}

	TopAreaHeight = std::clamp(TopAreaHeight, 280.0f, std::max(280.0f, Available.y - 260.0f - SplitterThickness));
	TopLeftWidth = std::clamp(TopLeftWidth, 360.0f, std::max(360.0f, Available.x - 500.0f - SplitterThickness));
	BottomLeftWidth = std::clamp(BottomLeftWidth, 360.0f, std::max(360.0f, Available.x - 500.0f - SplitterThickness));

	const float TopRightWidth = std::max(1.0f, Available.x - TopLeftWidth - SplitterThickness);
	const float BottomHeight = std::max(1.0f, Available.y - TopAreaHeight - SplitterThickness);
	const float BottomRightWidth = std::max(1.0f, Available.x - BottomLeftWidth - SplitterThickness);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

	ImGui::BeginChild("##ParticleTopRow", ImVec2(Available.x, TopAreaHeight), false, ImGuiWindowFlags_NoScrollbar);
	{
		ImGui::BeginChild("##ParticleViewportPanel", ImVec2(TopLeftWidth, TopAreaHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		DrawViewportPanel(ImGui::GetContentRegionAvail());
		ImGui::EndChild();

		ImGui::SameLine(0.0f, 0.0f);
		DrawVerticalSplitter("##ParticleTopVerticalSplitter", TopLeftWidth, Available.x, 360.0f, 500.0f);
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::BeginChild("##ParticleEmittersPanel", ImVec2(TopRightWidth, TopAreaHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		DrawEmittersPanel(ImGui::GetContentRegionAvail());
		ImGui::EndChild();
	}
	ImGui::EndChild();

	DrawHorizontalSplitter("##ParticleMainHorizontalSplitter", TopAreaHeight, Available.y, 280.0f, 260.0f);

	ImGui::BeginChild("##ParticleBottomRow", ImVec2(Available.x, BottomHeight), false, ImGuiWindowFlags_NoScrollbar);
	{
		ImGui::BeginChild("##ParticleDetailsPanel", ImVec2(BottomLeftWidth, BottomHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		DrawDetailsPanel(ImGui::GetContentRegionAvail());
		ImGui::EndChild();

		ImGui::SameLine(0.0f, 0.0f);
		DrawVerticalSplitter("##ParticleBottomVerticalSplitter", BottomLeftWidth, Available.x, 360.0f, 500.0f);
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::BeginChild("##ParticleCurvePanel", ImVec2(BottomRightWidth, BottomHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		DrawCurveEditorPanel(ImGui::GetContentRegionAvail());
		ImGui::EndChild();
	}
	ImGui::EndChild();

	ImGui::PopStyleVar(2);
}

void FEditorParticleSystemWidget::DrawViewportPanel(const ImVec2& Size)
{
	DrawPanelHeader("Viewport");
	EnsurePreviewViewport();

	const ImVec2 CanvasSize(Size.x, std::max(1.0f, Size.y - PanelHeaderHeight));
	const ImVec2 CanvasMin = ImGui::GetCursorScreenPos();

	ImGui::Dummy(CanvasSize);
	const bool bViewportHovered = ImGui::IsItemHovered();
	const bool bViewportClicked =
		bViewportHovered &&
		(ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		 ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
		 ImGui::IsMouseClicked(ImGuiMouseButton_Middle));

	const ImVec2 CanvasMax = ImGui::GetItemRectMax();
	const POINT ClientMin = ImGuiScreenToClientPoint(EditorEngine ? EditorEngine->GetWindow() : nullptr, CanvasMin);
	const FViewportRect NewRect(
		static_cast<int32>(ClientMin.x),
		static_cast<int32>(ClientMin.y),
		static_cast<int32>(CanvasMax.x - CanvasMin.x),
		static_cast<int32>(CanvasMax.y - CanvasMin.y));

	bPreviewViewportVisible = true;
	bPreviewViewportRectValid = NewRect.Width > 0 && NewRect.Height > 0;

	if (bPreviewViewportInitialized)
	{
		PreviewViewport.SetRect(NewRect);
		PreviewClient.SetViewportSize(static_cast<float>(NewRect.Width), static_cast<float>(NewRect.Height));
	}

	if (bViewportClicked && EditorEngine && bPreviewViewportInitialized)
	{
		EditorEngine->FocusViewportInput(&PreviewViewport);
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const FColor& BackgroundColor = PreviewClient.GetBackgroundColor();
	DrawList->AddRectFilled(
		CanvasMin,
		CanvasMax,
		ImGui::GetColorU32(ImVec4(BackgroundColor.R, BackgroundColor.G, BackgroundColor.B, 1.0f)));

	ID3D11ShaderResourceView* SRV = bPreviewViewportInitialized ? PreviewViewport.GetOutSRV() : nullptr;
	if (SRV && EditorEngine)
	{
		ID3D11DeviceContext* DeviceContext = EditorEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
		DrawList->AddCallback(SetOpaqueBlendStateCallback, DeviceContext);
		DrawList->AddImage(reinterpret_cast<ImTextureID>(SRV), CanvasMin, CanvasMax);
		DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
	}
	else
	{
		DrawList->AddText(
			ImVec2(CanvasMin.x + 12.0f, CanvasMin.y + 44.0f),
			ImGui::GetColorU32(ImVec4(0.78f, 0.80f, 0.84f, 1.0f)),
			"Preview render target is not ready.");
	}

	DrawViewportMenuBar(CanvasMin);

	/*DrawList->AddText(
		ImVec2(CanvasMin.x + 8.0f, CanvasMax.y - 72.0f),
		ImGui::GetColorU32(ImVec4(0.90f, 0.90f, 0.90f, 1.0f)),
		"WARNING: This particle system has no fixed bounding box and contains a GPU emitter.");*/

	const bool bDrawOrientationAxis = bPreviewViewportInitialized
		? PreviewClient.GetParticleShowFlags().bAxis
		: bShowOriginAxis;
	if (bDrawOrientationAxis)
	{
		DrawViewportOrientationAxis(DrawList, CanvasMin, CanvasMax, PreviewClient.GetRenderCamera());
	}
}

void FEditorParticleSystemWidget::DrawViewportMenuBar(const ImVec2& CanvasMin)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 2.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.10f, 0.10f, 0.88f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.18f, 0.18f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.30f, 0.53f, 1.0f));

	ImGui::SetCursorScreenPos(ImVec2(CanvasMin.x + 8.0f, CanvasMin.y + 6.0f));
	if (ImGui::Button("View##ParticlePreviewViewButton"))
	{
		ImGui::OpenPopup("##ParticlePreviewViewPopup");
	}

	if (BeginParticlePopup("##ParticlePreviewViewPopup"))
	{
		if (bPreviewViewportInitialized && BeginParticleMenu("View Modes"))
		{
			DrawViewportModeItem("Wireframe", EViewMode::Wireframe, PreviewViewport);
			DrawViewportModeItem("Unlit", EViewMode::Unlit, PreviewViewport);
			EndParticleMenu();
		}

		if (bPreviewViewportInitialized)
		{
			ImGui::Separator();
			FParticleSystemViewportShowFlags& ShowFlags = PreviewClient.GetParticleShowFlags();
			ImGui::MenuItem("Grid", nullptr, &ShowFlags.bGrid);
			if (ImGui::MenuItem("World Axis", nullptr, &ShowFlags.bAxis))
			{
				bShowOriginAxis = ShowFlags.bAxis;
			}
		}

		EndParticlePopup();
	}

	ImGui::SameLine(0.0f, 4.0f);
	if (ImGui::Button("Time##ParticlePreviewTimeButton"))
	{
		ImGui::OpenPopup("##ParticlePreviewTimePopup");
	}

	if (BeginParticlePopup("##ParticlePreviewTimePopup"))
	{
		if (ImGui::MenuItem(bPreviewPaused ? "Play" : "Pause"))
		{
			if (bPreviewPlaybackComplete && PreviewComponent)
			{
				PreviewComponent->RecreateEmitterInstances();
				RestartPreviewPlayback();
			}
			bPreviewPaused = !bPreviewPaused;
		}
		if (ImGui::MenuItem("Loop", nullptr, &bPreviewLoop))
		{
			if (bPreviewLoop && bPreviewPlaybackComplete)
			{
				RestartPreviewPlayback();
				bPreviewPaused = false;
			}
		}

		if (BeginParticleMenu("AnimSpeed"))
		{
			ImGui::TextDisabled("ANIMSPEED");
			ImGui::Separator();

			const char* SpeedLabels[] = { "100%", "50%", "25%", "10%", "1%" };
			for (int32 SpeedIndex = 0; SpeedIndex < static_cast<int32>(IM_ARRAYSIZE(SpeedLabels)); ++SpeedIndex)
			{
				ImGui::PushID(SpeedIndex);
				if (ImGui::RadioButton(SpeedLabels[SpeedIndex], PreviewAnimSpeedIndex == SpeedIndex))
				{
					PreviewAnimSpeedIndex = SpeedIndex;
				}
				ImGui::PopID();
			}
			EndParticleMenu();
		}

		EndParticlePopup();
	}

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(2);
}

void FEditorParticleSystemWidget::ShowCenterToast(const FString& Message)
{
	if (Message.empty())
	{
		return;
	}

	CenterToastMessage = Message;
	CenterToastRemainingTime = 1.6f;
}

void FEditorParticleSystemWidget::DrawCenterToast(const ImVec2& AreaMin, const ImVec2& AreaSize)
{
	if (CenterToastRemainingTime <= 0.0f || CenterToastMessage.empty() || AreaSize.x <= 1.0f || AreaSize.y <= 1.0f)
	{
		return;
	}

	const float FadeAlpha = std::clamp(CenterToastRemainingTime / 0.25f, 0.0f, 1.0f);
	const ImVec2 ToastCenter(
		AreaMin.x + AreaSize.x * 0.5f,
		AreaMin.y + AreaSize.y * 0.5f);

	if (const ImGuiViewport* Viewport = ImGui::GetWindowViewport())
	{
		ImGui::SetNextWindowViewport(Viewport->ID);
	}
	ImGui::SetNextWindowPos(ToastCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowBgAlpha(0.92f * FadeAlpha);

	constexpr ImGuiWindowFlags ToastFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoInputs;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 12.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 7.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.09f, 0.92f * FadeAlpha));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.72f, 0.49f, 0.18f, 0.85f * FadeAlpha));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.88f, 0.64f, FadeAlpha));
	if (ImGui::Begin("##ParticleEditorCenterToast", nullptr, ToastFlags))
	{
		ImGui::TextUnformatted(CenterToastMessage.c_str());
	}
	ImGui::End();
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(3);
}
