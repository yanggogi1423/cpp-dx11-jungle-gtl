#pragma once

// Shared private includes and helper utilities for the particle system editor files.

#include "Editor/UI/EditorParticleSystemWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/UI/EditorDetachedWindowChrome.h"
#include "Editor/UI/EditorMainPanelViewportToolbarHelpers.h"
#include "Engine/Core/EditorResourcePaths.h"
#include "Core/ResourceManager.h"
#include "Core/Paths.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "GameFramework/AActor.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "Object/Class.h"
#include "Object/Property.h"
#include "Particle/ParticleSystemComponent.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleModuleBeamNoise.h"
#include "Particle/ParticleModuleBeamSource.h"
#include "Particle/ParticleModuleBeamTarget.h"
#include "Render/Resource/Material.h"
#include "Component/PostProcess/Light/AmbientLightComponent.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "WICTextureLoader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace
{
	constexpr float SplitterThickness = 5.0f;
	constexpr float PanelHeaderHeight = 24.0f;
	constexpr const char* ParticleEmitterDragPayloadType = "PS_EMITTER";
	constexpr const char* ParticleModuleDragPayloadType = "PS_MODULE";
	constexpr int32 NoParticleModuleSelection = -1;
	constexpr int32 RequiredParticleModuleSelection = -2;
	constexpr int32 RendererPropertiesSelection = -3;
	constexpr ImGuiDragDropFlags ParticleDragDropTargetFlags =
		ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

	struct FEmitterDragPayload
	{
		int32 SourceEmitterIndex = -1;
	};

	struct FModuleDragPayload
	{
		int32 SourceEmitterIndex = -1;
		int32 SourceModuleIndex = -1;
	};

	template <typename ItemType>
	bool MoveArrayItemToInsertIndex(TArray<ItemType>& Items, int32 SourceIndex, int32 InsertIndex, int32& OutNewIndex)
	{
		const int32 Count = static_cast<int32>(Items.size());
		if (SourceIndex < 0 || SourceIndex >= Count)
		{
			return false;
		}

		InsertIndex = std::clamp(InsertIndex, 0, Count);
		if (InsertIndex == SourceIndex || InsertIndex == SourceIndex + 1)
		{
			return false;
		}

		ItemType Item = Items[SourceIndex];
		Items.erase(Items.begin() + SourceIndex);
		if (SourceIndex < InsertIndex)
		{
			--InsertIndex;
		}
		InsertIndex = std::clamp(InsertIndex, 0, static_cast<int32>(Items.size()));
		Items.insert(Items.begin() + InsertIndex, Item);
		OutNewIndex = InsertIndex;
		return true;
	}

	void SetOpaqueBlendStateCallback(const ImDrawList*, const ImDrawCmd* Cmd)
	{
		ID3D11DeviceContext* DeviceContext = static_cast<ID3D11DeviceContext*>(Cmd->UserCallbackData);
		if (!DeviceContext)
		{
			return;
		}

		const float BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
		DeviceContext->OMSetBlendState(nullptr, BlendFactor, 0xffffffff);
	}

	bool UsesAbsoluteImGuiCoordinates()
	{
		return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
	}

	POINT ImGuiScreenToClientPoint(FWindowsWindow* Window, const ImVec2& Point)
	{
		POINT Result =
		{
			static_cast<LONG>(std::lround(Point.x)),
			static_cast<LONG>(std::lround(Point.y))
		};
		if (Window && Window->GetHWND() && UsesAbsoluteImGuiCoordinates())
		{
			::ScreenToClient(Window->GetHWND(), &Result);
		}
		return Result;
	}

	constexpr float ToolbarButtonHeight = 28.0f;
	constexpr float ToolbarIconSize = 18.0f;
	constexpr float ToolbarButtonPaddingX = 8.0f;
	constexpr float ToolbarIconTextGap = 6.0f;

	ImVec2 CalcToolbarButtonSize(const char* Label, ID3D11ShaderResourceView* Icon)
	{
		const char* DisplayLabel = Label ? Label : "";
		const ImVec2 TextSize = ImGui::CalcTextSize(DisplayLabel);
		const bool bHasText = DisplayLabel[0] != '\0';
		const float ButtonContentWidth =
			ToolbarButtonPaddingX * 2.0f +
			(Icon ? ToolbarIconSize : 0.0f) +
			(Icon && bHasText ? ToolbarIconTextGap : 0.0f) +
			(bHasText ? TextSize.x : 0.0f);
		return ImVec2(std::max(ToolbarButtonHeight, ButtonContentWidth), ToolbarButtonHeight);
	}

	bool ToolbarButton(
		const char* Id,
		const char* Label,
		ID3D11ShaderResourceView* Icon,
		const char* Tooltip = nullptr,
		bool bSelected = false)
	{
		const char* DisplayLabel = Label ? Label : "";
		const ImVec2 TextSize = ImGui::CalcTextSize(DisplayLabel);
		const bool bHasText = DisplayLabel[0] != '\0';
		const ImVec2 ButtonSize = CalcToolbarButtonSize(DisplayLabel, Icon);
		const float ButtonContentWidth =
			ToolbarButtonPaddingX * 2.0f +
			(Icon ? ToolbarIconSize : 0.0f) +
			(Icon && bHasText ? ToolbarIconTextGap : 0.0f) +
			(bHasText ? TextSize.x : 0.0f);

		ImGui::PushID(Id);
		const bool bClicked = ImGui::InvisibleButton("##CascadeToolbarButton", ButtonSize);
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		const bool bHovered = ImGui::IsItemHovered();
		const bool bActive = ImGui::IsItemActive();
		const float Alpha = ImGui::GetStyle().Alpha;
		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		const ImVec4 BgColor =
			bSelected
				? (bHovered ? ImVec4(0.22f, 0.29f, 0.38f, 1.0f) : ImVec4(0.18f, 0.24f, 0.32f, 1.0f))
				: (bActive ? ImVec4(0.28f, 0.31f, 0.38f, 1.0f)
						   : bHovered ? ImVec4(0.23f, 0.25f, 0.30f, 1.0f)
									  : ImVec4(0.14f, 0.15f, 0.17f, 1.0f));
		const ImU32 Bg = ImGui::GetColorU32(ImVec4(BgColor.x, BgColor.y, BgColor.z, BgColor.w * Alpha));
		const ImU32 Border = ImGui::GetColorU32(
			bSelected
				? ImVec4(0.42f, 0.55f, 0.75f, Alpha)
				: bHovered ? ImVec4(0.33f, 0.36f, 0.42f, Alpha)
						   : ImVec4(0.18f, 0.19f, 0.22f, Alpha));
		DrawList->AddRectFilled(Min, Max, Bg, 3.0f);
		DrawList->AddRect(Min, Max, Border, 3.0f);

		float CursorX = Min.x + (ButtonSize.x - ButtonContentWidth) * 0.5f + ToolbarButtonPaddingX;
		if (Icon)
		{
			const float IconY = Min.y + (ToolbarButtonHeight - ToolbarIconSize) * 0.5f;
			DrawList->AddImage(
				reinterpret_cast<ImTextureID>(Icon),
				ImVec2(CursorX, IconY),
				ImVec2(CursorX + ToolbarIconSize, IconY + ToolbarIconSize),
				ImVec2(0.0f, 0.0f),
				ImVec2(1.0f, 1.0f),
				ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, Alpha)));
			CursorX += ToolbarIconSize + (bHasText ? ToolbarIconTextGap : 0.0f);
		}
		if (bHasText)
		{
			DrawList->AddText(
				ImVec2(CursorX, Min.y + (ToolbarButtonHeight - TextSize.y) * 0.5f),
				ImGui::GetColorU32(ImVec4(0.86f, 0.88f, 0.92f, Alpha)),
				DisplayLabel);
		}

		if (Tooltip && ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", Tooltip);
		}
		ImGui::PopID();
		return bClicked;
	}

	void SameLineGap(float Gap = 4.0f)
	{
		ImGui::SameLine(0.0f, Gap);
	}

	void DrawVerticalSplitter(const char* Id, float& LeftWidth, float TotalWidth, float MinLeft, float MinRight)
	{
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton(Id, ImVec2(SplitterThickness, ImGui::GetContentRegionAvail().y));
		const ImVec2 Max = ImGui::GetItemRectMax();
		if (ImGui::IsItemActive())
		{
			LeftWidth += ImGui::GetIO().MouseDelta.x;
			LeftWidth = std::clamp(LeftWidth, MinLeft, std::max(MinLeft, TotalWidth - MinRight - SplitterThickness));
		}
		const bool bHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
		ImGui::GetWindowDrawList()->AddRectFilled(
			Min,
			Max,
			ImGui::GetColorU32(bHot ? ImVec4(0.28f, 0.30f, 0.35f, 1.0f) : ImVec4(0.09f, 0.09f, 0.10f, 1.0f)));
		if (bHot)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}
	}

	void DrawHorizontalSplitter(const char* Id, float& TopHeight, float TotalHeight, float MinTop, float MinBottom)
	{
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton(Id, ImVec2(ImGui::GetContentRegionAvail().x, SplitterThickness));
		const ImVec2 Max = ImGui::GetItemRectMax();
		if (ImGui::IsItemActive())
		{
			TopHeight += ImGui::GetIO().MouseDelta.y;
			TopHeight = std::clamp(TopHeight, MinTop, std::max(MinTop, TotalHeight - MinBottom - SplitterThickness));
		}
		const bool bHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
		ImGui::GetWindowDrawList()->AddRectFilled(
			Min,
			Max,
			ImGui::GetColorU32(bHot ? ImVec4(0.28f, 0.30f, 0.35f, 1.0f) : ImVec4(0.09f, 0.09f, 0.10f, 1.0f)));
		if (bHot)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		}
	}

	void DrawPanelHeader(const char* Title)
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + ImGui::GetContentRegionAvail().x, Min.y + PanelHeaderHeight);
		DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.15f, 1.0f)));
		DrawList->AddRect(Min, Max, ImGui::GetColorU32(ImVec4(0.07f, 0.07f, 0.07f, 1.0f)));
		ImGui::SetCursorScreenPos(ImVec2(Min.x + 8.0f, Min.y + 4.0f));
		ImGui::TextUnformatted(Title);
		ImGui::SetCursorScreenPos(ImVec2(Min.x, Max.y));
	}

	void DrawViewportModeItem(const char* Label, EViewMode Mode, FSceneViewport& Viewport)
	{
		FEditorViewportState& State = Viewport.GetState();
		if (ImGui::MenuItem(Label, nullptr, State.ViewMode == Mode))
		{
			State.ViewMode = Mode;
		}
	}

	bool IsPointInsideRect(const ImVec2& Point, const ImVec2& Min, const ImVec2& Max)
	{
		return Point.x >= Min.x && Point.x <= Max.x && Point.y >= Min.y && Point.y <= Max.y;
	}

	bool BeginParticleDetailsTable(const char* TableId, float LabelWidth)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 4.0f));
		if (ImGui::BeginTable(
			TableId,
			2,
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, LabelWidth);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			return true;
		}

		ImGui::PopStyleVar();
		return false;
	}

	void EndParticleDetailsTable()
	{
		ImGui::EndTable();
		ImGui::PopStyleVar();
	}

	void BeginParticleDetailsRow(const char* Label, float RowHeight = 28.0f)
	{
		ImGui::TableNextRow(ImGuiTableRowFlags_None, RowHeight);
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
	}

	void PushParticlePopupStyle()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));
	}

	void PopParticlePopupStyle()
	{
		ImGui::PopStyleVar(2);
	}

	bool BeginParticlePopup(const char* Id)
	{
		PushParticlePopupStyle();
		if (ImGui::BeginPopup(Id))
		{
			return true;
		}
		PopParticlePopupStyle();
		return false;
	}

	void EndParticlePopup()
	{
		ImGui::EndPopup();
		PopParticlePopupStyle();
	}

	bool BeginParticlePopupModal(const char* Id, bool* Open, ImGuiWindowFlags Flags)
	{
		PushParticlePopupStyle();
		if (ImGui::BeginPopupModal(Id, Open, Flags))
		{
			return true;
		}
		PopParticlePopupStyle();
		return false;
	}

	void EndParticlePopupModal()
	{
		ImGui::EndPopup();
		PopParticlePopupStyle();
	}

	bool BeginParticleMenu(const char* Label, bool bEnabled = true)
	{
		PushParticlePopupStyle();
		if (ImGui::BeginMenu(Label, bEnabled))
		{
			return true;
		}
		PopParticlePopupStyle();
		return false;
	}

	void EndParticleMenu()
	{
		ImGui::EndMenu();
		PopParticlePopupStyle();
	}

	bool BeginParticleCombo(const char* Label, const char* PreviewValue, ImGuiComboFlags Flags = ImGuiComboFlags_None)
	{
		PushParticlePopupStyle();
		if (ImGui::BeginCombo(Label, PreviewValue, Flags))
		{
			return true;
		}
		PopParticlePopupStyle();
		return false;
	}

	void EndParticleCombo()
	{
		ImGui::EndCombo();
		PopParticlePopupStyle();
	}

	bool ParticleCombo(const char* Label, int* CurrentItem, const char* const Items[], int ItemsCount)
	{
		PushParticlePopupStyle();
		const bool bChanged = ImGui::Combo(Label, CurrentItem, Items, ItemsCount);
		PopParticlePopupStyle();
		return bChanged;
	}

	bool ParticleCombo(
		const char* Label,
		int* CurrentItem,
		const char* (*Getter)(void*, int),
		void* UserData,
		int ItemsCount)
	{
		PushParticlePopupStyle();
		const bool bChanged = ImGui::Combo(Label, CurrentItem, Getter, UserData, ItemsCount);
		PopParticlePopupStyle();
		return bChanged;
	}

	float SmoothStep(float Edge0, float Edge1, float Value)
	{
		const float T = std::clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	FString MakeSeparatedClassName(const char* ClassName, const char* Prefix)
	{
		FString Name = ClassName ? ClassName : "";
		if (Prefix)
		{
			const FString PrefixText = Prefix;
			if (Name.rfind(PrefixText, 0) == 0)
			{
				Name = Name.substr(PrefixText.size());
			}
		}

		FString Result;
		Result.reserve(Name.size() + 8);
		for (size_t Index = 0; Index < Name.size(); ++Index)
		{
			const unsigned char Current = static_cast<unsigned char>(Name[Index]);
			const unsigned char Previous = Index > 0 ? static_cast<unsigned char>(Name[Index - 1]) : 0;
			if (Index > 0 && std::isupper(Current) && (std::islower(Previous) || std::isdigit(Previous)))
			{
				Result.push_back(' ');
			}
			Result.push_back(Name[Index]);
		}
		return Result.empty() ? "Module" : Result;
	}

	FString TrimCopy(const FString& Value)
	{
		size_t First = 0;
		while (First < Value.size() && std::isspace(static_cast<unsigned char>(Value[First])))
		{
			++First;
		}

		size_t Last = Value.size();
		while (Last > First && std::isspace(static_cast<unsigned char>(Value[Last - 1])))
		{
			--Last;
		}

		return Value.substr(First, Last - First);
	}

	bool IsParticleSystemAssetDocumentPath(const FString& Path)
	{
		FString NormalizedPath = FPaths::Normalize(Path);
		std::transform(
			NormalizedPath.begin(),
			NormalizedPath.end(),
			NormalizedPath.begin(),
			[](unsigned char Ch)
			{
				return static_cast<char>(std::tolower(Ch));
			});

		const FString Extension = ".uasset";
		return NormalizedPath.size() >= Extension.size() &&
			NormalizedPath.compare(NormalizedPath.size() - Extension.size(), Extension.size(), Extension) == 0;
	}

	FString GetEmitterDisplayName(const UParticleEmitter* Emitter, int32 EmitterIndex)
	{
		if (Emitter)
		{
			const FString Name = Emitter->GetName();
			if (!Name.empty())
			{
				return Name;
			}
		}
		return "Emitter " + std::to_string(EmitterIndex + 1);
	}

	FString GetModuleDisplayName(const UParticleModule* Module, bool bRequired)
	{
		if (bRequired)
		{
			return "Required";
		}
		if (Cast<UParticleModuleSpawn>(Module))
		{
			return "Spawn";
		}
		if (const UParticleModuleTypeDataBase* TypeData = Cast<UParticleModuleTypeDataBase>(Module))
		{
			switch (TypeData->GetRenderMode())
			{
			case EParticleEmitterRenderMode::Sprite:
				return "Sprite";
			case EParticleEmitterRenderMode::Mesh:
				return "Mesh";
			case EParticleEmitterRenderMode::Beam:
				return "Beam";
			case EParticleEmitterRenderMode::Ribbon:
				return "Ribbon";
			default:
				return "Type Data";
			}
		}
		if (Cast<UParticleModuleLifetime>(Module))
		{
			return "Lifetime";
		}
		if (Cast<UParticleModuleBurst>(Module))
		{
			return "Burst";
		}
		if (Cast<UParticleModuleLocation>(Module))
		{
			return "Initial Location";
		}
		if (Cast<UParticleModuleLocationShape>(Module))
		{
			return "Shape Location";
		}
		if (Cast<UParticleModuleVelocity>(Module))
		{
			return "Initial Velocity";
		}
		if (Cast<UParticleModuleAcceleration>(Module))
		{
			return "Acceleration";
		}
		if (Cast<UParticleModuleDrag>(Module))
		{
			return "Drag";
		}
		if (Cast<UParticleModuleRotationRate>(Module))
		{
			return "Initial Rotation Rate";
		}
		if (Cast<UParticleModuleColor>(Module))
		{
			return "Color Over Life";
		}
		if (Cast<UParticleModuleLight>(Module))
		{
			return "Light";
		}
		if (Cast<UParticleModuleSize>(Module))
		{
			return "Size By Life";
		}
		if (Cast<UParticleModuleCollision>(Module))
		{
			return "Collision";
		}
		if (Cast<USubUVModule>(Module))
		{
			return "SubUV";
		}
		if (Cast<UParticleModuleEventGenerator>(Module))
		{
			return "Event Generator";
		}
		if (!Module || !Module->GetClass())
		{
			return "Module";
		}
		return MakeSeparatedClassName(Module->GetClass()->GetName(), "UParticleModule");
	}

	const char* GetPropertyDisplayName(const FProperty& Property)
	{
		return (Property.DisplayName && Property.DisplayName[0] != '\0') ? Property.DisplayName : Property.Name;
	}

	FString MakeParticlePropertyLabel(const FProperty& Property)
	{
		const char* DisplayName = GetPropertyDisplayName(Property);
		if (!DisplayName)
		{
			return "";
		}
		if (!Property.Name || std::strcmp(DisplayName, Property.Name) == 0)
		{
			return DisplayName;
		}
		return FString(DisplayName) + "##" + Property.Name;
	}

	bool IsInternalParticleModuleProperty(const FProperty& Property)
	{
		return Property.Name &&
			(std::strcmp(Property.Name, "bSpawnModule") == 0 ||
			 std::strcmp(Property.Name, "bUpdateModule") == 0);
	}

	const char* GetRenderModeLabel(EParticleEmitterRenderMode RenderMode)
	{
		switch (RenderMode)
		{
		case EParticleEmitterRenderMode::Sprite:
			return "Sprite";
		case EParticleEmitterRenderMode::Mesh:
			return "Mesh";
		case EParticleEmitterRenderMode::Beam:
			return "Beam";
		case EParticleEmitterRenderMode::Ribbon:
			return "Ribbon";
		default:
			return "Emitter";
		}
	}

	const char* GetRenderModeLabel(const UParticleLODLevel* LODLevel)
	{
		return LODLevel ? GetRenderModeLabel(LODLevel->GetEffectiveRenderMode()) : "Emitter";
	}

	bool IsCurveDrivenModule(const UParticleModule* Module)
	{
		return Module && (Module->IsSpawnModule() || Module->IsUpdateModule());
	}

	constexpr const char* InheritedLODModuleMarker = "__LOD_INHERITED__";
	constexpr const char* EditableLODModuleMarker = "__LOD_EDITABLE__";

	FString StripLODModuleMarker(const FString& Name)
	{
		FString Result = Name;
		const char* Markers[] = { InheritedLODModuleMarker, EditableLODModuleMarker };
		for (const char* Marker : Markers)
		{
			const size_t Pos = Result.find(Marker);
			if (Pos != FString::npos)
			{
				Result.erase(Pos);
			}
		}
		return Result;
	}

	void MarkModuleInheritedFromHigherLOD(UParticleModule* Module)
	{
		if (!Module)
		{
			return;
		}
		const FString BaseName = StripLODModuleMarker(Module->GetName());
		Module->SetFName(FName(BaseName + InheritedLODModuleMarker));
	}

	void MarkModuleEditableInLOD(UParticleModule* Module)
	{
		if (!Module)
		{
			return;
		}
		const FString BaseName = StripLODModuleMarker(Module->GetName());
		Module->SetFName(FName(BaseName + EditableLODModuleMarker));
	}

	bool IsInheritedLODModule(const UParticleModule* Module)
	{
		return Module && Module->GetName().find(InheritedLODModuleMarker) != FString::npos;
	}

	UParticleModule* DuplicateParticleModuleForLOD(UParticleModule* SourceModule, bool bInherited)
	{
		UParticleModule* DuplicatedModule = SourceModule
			? Cast<UParticleModule>(SourceModule->Duplicate())
			: nullptr;
		if (bInherited)
		{
			MarkModuleInheritedFromHigherLOD(DuplicatedModule);
		}
		else
		{
			MarkModuleEditableInLOD(DuplicatedModule);
		}
		return DuplicatedModule;
	}

	UParticleModuleRequired* DuplicateRequiredModuleForLOD(UParticleModuleRequired* SourceModule, bool bInherited)
	{
		UParticleModuleRequired* DuplicatedModule = SourceModule
			? Cast<UParticleModuleRequired>(SourceModule->Duplicate())
			: nullptr;
		if (bInherited)
		{
			MarkModuleInheritedFromHigherLOD(DuplicatedModule);
		}
		else
		{
			MarkModuleEditableInLOD(DuplicatedModule);
		}
		return DuplicatedModule;
	}

	void MarkLODModulesInheritedFromHigherLOD(UParticleLODLevel* LODLevel)
	{
		if (!LODLevel)
		{
			return;
		}
		MarkModuleInheritedFromHigherLOD(LODLevel->GetRequiredModule());
		for (UParticleModule* Module : LODLevel->Modules)
		{
			MarkModuleInheritedFromHigherLOD(Module);
		}
	}

	void MarkLODModulesInheritedFromHigherLODExceptSpawn(UParticleLODLevel* LODLevel)
	{
		if (!LODLevel)
		{
			return;
		}
		MarkModuleInheritedFromHigherLOD(LODLevel->GetRequiredModule());
		for (UParticleModule* Module : LODLevel->Modules)
		{
			if (Cast<UParticleModuleSpawn>(Module))
			{
				MarkModuleEditableInLOD(Module);
				continue;
			}
			MarkModuleInheritedFromHigherLOD(Module);
		}
	}

	void MarkLODModulesEditableInLOD(UParticleLODLevel* LODLevel)
	{
		if (!LODLevel)
		{
			return;
		}
		MarkModuleEditableInLOD(LODLevel->GetRequiredModule());
		for (UParticleModule* Module : LODLevel->Modules)
		{
			MarkModuleEditableInLOD(Module);
		}
	}

	ImVec4 GetModuleRowColor(const UParticleModule* Module, bool bRequired)
	{
		if (bRequired)
		{
			return ImVec4(0.76f, 0.75f, 0.30f, 1.0f);
		}
		if (Cast<UParticleModuleSpawn>(Module))
		{
			return ImVec4(0.72f, 0.32f, 0.32f, 1.0f);
		}
		return ImVec4(0.15f, 0.15f, 0.19f, 1.0f);
	}

	void DrawMiniCheck(ImDrawList* DrawList, const ImVec2& Min, bool bChecked)
	{
		const ImVec2 Max(Min.x + 13.0f, Min.y + 13.0f);
		DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(ImVec4(0.52f, 0.57f, 0.60f, 1.0f)));
		DrawList->AddRect(Min, Max, ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.02f, 1.0f)));
		if (bChecked)
		{
			DrawList->AddLine(ImVec2(Min.x + 3.0f, Min.y + 7.0f), ImVec2(Min.x + 6.0f, Min.y + 10.0f), ImGui::GetColorU32(ImVec4(0.01f, 0.01f, 0.01f, 1.0f)), 1.4f);
			DrawList->AddLine(ImVec2(Min.x + 6.0f, Min.y + 10.0f), ImVec2(Min.x + 11.0f, Min.y + 3.0f), ImGui::GetColorU32(ImVec4(0.01f, 0.01f, 0.01f, 1.0f)), 1.4f);
		}
	}

	void DrawMiniEmitterRenderToggle(ImDrawList* DrawList, const ImVec2& Min, bool bEnabled)
	{
		const ImVec2 Max(Min.x + 13.0f, Min.y + 13.0f);
		DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(bEnabled ? ImVec4(0.52f, 0.57f, 0.60f, 1.0f) : ImVec4(0.42f, 0.18f, 0.18f, 1.0f)));
		DrawList->AddRect(Min, Max, ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.02f, 1.0f)));
		if (bEnabled)
		{
			DrawList->AddLine(ImVec2(Min.x + 3.0f, Min.y + 7.0f), ImVec2(Min.x + 6.0f, Min.y + 10.0f), ImGui::GetColorU32(ImVec4(0.01f, 0.01f, 0.01f, 1.0f)), 1.4f);
			DrawList->AddLine(ImVec2(Min.x + 6.0f, Min.y + 10.0f), ImVec2(Min.x + 11.0f, Min.y + 3.0f), ImGui::GetColorU32(ImVec4(0.01f, 0.01f, 0.01f, 1.0f)), 1.4f);
		}
		else
		{
			DrawList->AddLine(ImVec2(Min.x + 3.0f, Min.y + 3.0f), ImVec2(Min.x + 10.0f, Min.y + 10.0f), ImGui::GetColorU32(ImVec4(0.98f, 0.95f, 0.95f, 1.0f)), 1.5f);
			DrawList->AddLine(ImVec2(Min.x + 10.0f, Min.y + 3.0f), ImVec2(Min.x + 3.0f, Min.y + 10.0f), ImGui::GetColorU32(ImVec4(0.98f, 0.95f, 0.95f, 1.0f)), 1.5f);
		}
	}

	void DrawMiniSoloButton(ImDrawList* DrawList, const ImVec2& Min, bool bSolo)
	{
		const ImVec2 Max(Min.x + 17.0f, Min.y + 13.0f);
		const ImVec4 Fill = bSolo ? ImVec4(0.96f, 0.68f, 0.24f, 1.0f) : ImVec4(0.28f, 0.30f, 0.34f, 1.0f);
		const ImVec4 Text = bSolo ? ImVec4(0.05f, 0.04f, 0.03f, 1.0f) : ImVec4(0.86f, 0.88f, 0.92f, 1.0f);
		DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(Fill), 2.0f);
		DrawList->AddRect(Min, Max, ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.02f, 1.0f)), 2.0f);
		const ImVec2 TextSize = ImGui::CalcTextSize("S");
		DrawList->AddText(
			ImVec2(Min.x + (17.0f - TextSize.x) * 0.5f, Min.y + (13.0f - TextSize.y) * 0.5f),
			ImGui::GetColorU32(Text),
			"S");
	}

	void DrawMiniCurveIcon(ImDrawList* DrawList, const ImVec2& Min, bool bEnabled)
	{
		const ImVec2 Max(Min.x + 13.0f, Min.y + 13.0f);
		const ImU32 BorderColor = ImGui::GetColorU32(bEnabled ? ImVec4(0.58f, 0.86f, 0.40f, 1.0f) : ImVec4(0.25f, 0.28f, 0.25f, 1.0f));
		DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(ImVec4(0.07f, 0.09f, 0.07f, 1.0f)));
		DrawList->AddRect(Min, Max, BorderColor);
		DrawList->AddLine(ImVec2(Min.x + 2.0f, Min.y + 10.0f), ImVec2(Min.x + 5.0f, Min.y + 5.0f), BorderColor, 1.2f);
		DrawList->AddLine(ImVec2(Min.x + 5.0f, Min.y + 5.0f), ImVec2(Min.x + 8.0f, Min.y + 8.0f), BorderColor, 1.2f);
		DrawList->AddLine(ImVec2(Min.x + 8.0f, Min.y + 8.0f), ImVec2(Min.x + 11.0f, Min.y + 3.0f), BorderColor, 1.2f);
	}

	void DrawEmitterThumbnail(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max, int32 EmitterIndex)
	{
		DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.025f, 1.0f)));
		DrawList->AddRect(Min, Max, ImGui::GetColorU32(ImVec4(0.50f, 0.55f, 0.60f, 1.0f)));

		const ImVec2 Center((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
		const ImU32 CoreColor = ImGui::GetColorU32(ImVec4(0.86f, 0.88f, 0.88f, 0.92f));
		const ImU32 GlowColor = ImGui::GetColorU32(ImVec4(0.42f, 0.55f, 0.68f, 0.28f));
		const float Radius = 8.0f + static_cast<float>((EmitterIndex * 3) % 8);
		DrawList->AddCircleFilled(Center, Radius + 7.0f, GlowColor, 24);
		DrawList->AddCircleFilled(Center, Radius, CoreColor, 24);
		for (int32 Dot = 0; Dot < 9; ++Dot)
		{
			const float Angle = static_cast<float>(Dot) * 0.72f + static_cast<float>(EmitterIndex) * 0.37f;
			const float Distance = 8.0f + static_cast<float>((Dot * 5 + EmitterIndex * 3) % 17);
			const ImVec2 DotCenter(Center.x + std::cos(Angle) * Distance, Center.y + std::sin(Angle) * Distance);
			DrawList->AddCircleFilled(DotCenter, 1.2f + static_cast<float>(Dot % 3) * 0.4f, CoreColor, 8);
		}
	}

	void AddModule(UParticleLODLevel* LODLevel, UParticleModule* Module)
	{
		if (LODLevel && Module)
		{
			LODLevel->Modules.push_back(Module);
		}
	}

	template <typename ModuleType>
	ModuleType* CreateParticleModule(const char* Name)
	{
		ModuleType* Module = UObjectManager::Get().CreateObject<ModuleType>();
		if (Module && Name)
		{
			Module->SetFName(FName(Name));
		}
		return Module;
	}

	template <typename ModuleType>
	bool HasParticleModule(const UParticleLODLevel* LODLevel)
	{
		if (!LODLevel)
		{
			return false;
		}
		for (UParticleModule* Module : LODLevel->Modules)
		{
			if (Cast<ModuleType>(Module))
			{
				return true;
			}
		}
		return false;
	}

	template <typename ModuleType>
	void RemoveParticleModules(UParticleLODLevel* LODLevel)
	{
		if (!LODLevel)
		{
			return;
		}
		for (auto It = LODLevel->Modules.begin(); It != LODLevel->Modules.end();)
		{
			if (ModuleType* Module = Cast<ModuleType>(*It))
			{
				It = LODLevel->Modules.erase(It);
				UObjectManager::Get().DestroyObject(Module);
				continue;
			}
			++It;
		}
	}

	template <typename ModuleType>
	void AddModuleIfMissing(UParticleLODLevel* LODLevel, const char* Name)
	{
		if (LODLevel && !HasParticleModule<ModuleType>(LODLevel))
		{
			AddModule(LODLevel, CreateParticleModule<ModuleType>(Name));
		}
	}

	void EnsureBeamSupportModules(UParticleLODLevel* LODLevel)
	{
		AddModuleIfMissing<UParticleModuleBeamSource>(LODLevel, "Beam Source");
		AddModuleIfMissing<UParticleModuleBeamTarget>(LODLevel, "Beam Target");
		AddModuleIfMissing<UParticleModuleBeamNoise>(LODLevel, "Beam Noise");
	}

	void RemoveBeamSupportModules(UParticleLODLevel* LODLevel)
	{
		RemoveParticleModules<UParticleModuleBeamSource>(LODLevel);
		RemoveParticleModules<UParticleModuleBeamTarget>(LODLevel);
		RemoveParticleModules<UParticleModuleBeamNoise>(LODLevel);
	}

	void DrawDisabledParticleModuleMenu(const char* MenuLabel)
	{
		(void)MenuLabel;
	}

	template <typename ModuleType, typename AddModuleFunc>
	void DrawParticleModuleAddMenu(
		const char* MenuLabel,
		const char* ItemLabel,
		bool bEnabled,
		AddModuleFunc AddModule)
	{
		if (BeginParticleMenu(MenuLabel, bEnabled))
		{
			if (ImGui::MenuItem(ItemLabel))
			{
				AddModule(CreateParticleModule<ModuleType>(ItemLabel));
			}
			EndParticleMenu();
		}
	}

	const char* GetTypeDataMenuItemLabel(EParticleEmitterRenderMode RenderMode)
	{
		switch (RenderMode)
		{
		case EParticleEmitterRenderMode::Sprite:
			return "New Sprite Data";
		case EParticleEmitterRenderMode::Mesh:
			return "New Mesh Data";
		case EParticleEmitterRenderMode::Beam:
			return "New Beam Data";
		case EParticleEmitterRenderMode::Ribbon:
			return "New Ribbon Data";
		default:
			return "New Type Data";
		}
	}

	UParticleEmitter* CreateDefaultParticleEmitter(const FString& Name)
	{
        UParticleEmitter* Emitter = UObjectManager::Get().CreateObject<UParticleEmitter>();
        if (!Emitter)
            return nullptr;

        UParticleLODLevel* LODLevel = Emitter->AddLODLevel(0, 100.0f);
        if (!LODLevel)
            return Emitter;

		const FString LODName = Name + "_LOD0";
		Emitter->SetFName(FName(Name));
		LODLevel->SetFName(FName(LODName));
		LODLevel->Level = 0;
		LODLevel->RequiredModule = CreateParticleModule<UParticleModuleRequired>("Required");
		LODLevel->EnsureRendererProperties(EParticleEmitterRenderMode::Sprite);
		AddModule(LODLevel, CreateParticleModule<UParticleModuleSpawn>("Spawn"));
		AddModule(LODLevel, CreateParticleModule<UParticleModuleLifetime>("Lifetime"));
		AddModule(LODLevel, CreateParticleModule<UParticleModuleLocation>("Initial Location"));
		AddModule(LODLevel, CreateParticleModule<UParticleModuleVelocity>("Initial Velocity"));
		AddModule(LODLevel, CreateParticleModule<UParticleModuleColor>("Color"));
		AddModule(LODLevel, CreateParticleModule<UParticleModuleSize>("Size"));

		Emitter->CacheEmitterModuleInfo();
		return Emitter;
	}

	FString MakeUniqueEmitterName(const UParticleSystem* ParticleSystem)
	{
		int32 CandidateIndex = ParticleSystem
			? static_cast<int32>(ParticleSystem->GetEmitters().size()) + 1
			: 1;

		while (true)
		{
			const FString CandidateName = "Emitter " + std::to_string(CandidateIndex);
			bool bExists = false;
			if (ParticleSystem)
			{
				for (const UParticleEmitter* Emitter : ParticleSystem->GetEmitters())
				{
					if (Emitter && Emitter->GetName() == CandidateName)
					{
						bExists = true;
						break;
					}
				}
			}
			if (!bExists)
			{
				return CandidateName;
			}
			++CandidateIndex;
		}
	}

	void DrawAxisLabel(
		ImDrawList* DrawList,
		const ImVec2& AxisEnd,
		const ImVec2& AxisDirection,
		const char* Label,
		const ImVec4& BaseColor,
		float Alpha)
	{
		const ImVec2 TextSize = ImGui::CalcTextSize(Label);
		const ImVec2 LabelCenter(
			AxisEnd.x + AxisDirection.x * 7.0f,
			AxisEnd.y + AxisDirection.y * 7.0f);
		const ImVec2 TextPosition(
			LabelCenter.x - TextSize.x * 0.5f,
			LabelCenter.y - TextSize.y * 0.5f);

		const ImU32 ShadowColor = ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.02f, 0.95f * Alpha));
		const ImU32 TextColor = ImGui::GetColorU32(ImVec4(BaseColor.x, BaseColor.y, BaseColor.z, BaseColor.w * Alpha));
		DrawList->AddText(ImVec2(TextPosition.x + 1.0f, TextPosition.y + 1.0f), ShadowColor, Label);
		DrawList->AddText(TextPosition, TextColor, Label);
	}

	void DrawViewportOrientationAxis(
		ImDrawList* DrawList,
		const ImVec2& CanvasMin,
		const ImVec2& CanvasMax,
		const FViewportCamera* Camera)
	{
		if (!DrawList || !Camera)
		{
			return;
		}

		const FVector CameraForward = Camera->GetForwardVector().GetSafeNormal();
		const FVector CameraRight = Camera->GetRightVector().GetSafeNormal();
		const FVector CameraUp = Camera->GetUpVector().GetSafeNormal();

		struct FAxisDrawItem
		{
			const char* Label;
			FVector Axis;
			ImVec4 Color;
			ImVec2 End;
			ImVec2 Direction;
			float Alpha;
			float Depth;
		};

		const ImVec2 Origin(CanvasMin.x + 46.0f, CanvasMax.y - 46.0f);
		constexpr float AxisLength = 28.0f;
		constexpr float MinProjectedLength = 0.35f;
		constexpr float LabelFadeStart = 0.10f;
		constexpr float LabelFadeEnd = 0.28f;

		std::array<FAxisDrawItem, 3> Axes =
		{ {
			{ "X", FVector::XAxisVector, ImVec4(0.95f, 0.12f, 0.04f, 1.0f), Origin, ImVec2(1.0f, 0.0f), 1.0f, 0.0f },
			{ "Y", FVector::YAxisVector, ImVec4(0.42f, 0.86f, 0.12f, 1.0f), Origin, ImVec2(0.0f, 1.0f), 1.0f, 0.0f },
			{ "Z", FVector::ZAxisVector, ImVec4(0.10f, 0.45f, 1.0f, 1.0f), Origin, ImVec2(0.0f, -1.0f), 1.0f, 0.0f }
		} };

		for (FAxisDrawItem& Item : Axes)
		{
			const float ScreenX = FVector::DotProduct(Item.Axis, CameraRight);
			const float ScreenY = -FVector::DotProduct(Item.Axis, CameraUp);
			const float ProjectedLength = std::sqrt(ScreenX * ScreenX + ScreenY * ScreenY);
			Item.Alpha = SmoothStep(LabelFadeStart, LabelFadeEnd, ProjectedLength);
			if (ProjectedLength > 1.0e-4f)
			{
				Item.Direction = ImVec2(ScreenX / ProjectedLength, ScreenY / ProjectedLength);
				const float VisualLength = AxisLength * std::clamp(ProjectedLength, MinProjectedLength, 1.0f);
				Item.End = ImVec2(
					Origin.x + Item.Direction.x * VisualLength,
					Origin.y + Item.Direction.y * VisualLength);
			}
			Item.Depth = FVector::DotProduct(Item.Axis, CameraForward);
		}

		std::sort(Axes.begin(), Axes.end(), [](const FAxisDrawItem& A, const FAxisDrawItem& B)
		{
			return A.Depth > B.Depth;
		});

		const ImU32 ShadowColor = ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.02f, 0.95f));
		for (const FAxisDrawItem& Item : Axes)
		{
			if (Item.Alpha <= 0.01f)
			{
				continue;
			}

			const ImU32 AxisColor = ImGui::GetColorU32(ImVec4(Item.Color.x, Item.Color.y, Item.Color.z, Item.Color.w * Item.Alpha));
			DrawList->AddLine(Origin, Item.End, ShadowColor, 3.0f);
			DrawList->AddLine(Origin, Item.End, AxisColor, 1.6f);
			DrawAxisLabel(DrawList, Item.End, Item.Direction, Item.Label, Item.Color, Item.Alpha);
		}

		DrawList->AddCircleFilled(Origin, 2.4f, ShadowColor, 12);
		DrawList->AddCircleFilled(Origin, 1.6f, ImGui::GetColorU32(ImVec4(0.84f, 0.84f, 0.84f, 1.0f)), 12);
	}
}
