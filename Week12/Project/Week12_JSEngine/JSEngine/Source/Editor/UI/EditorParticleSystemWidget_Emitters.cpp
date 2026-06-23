// Manages emitter/module list UI, context menus, add/delete, rename, and reorder operations.
#include "Editor/UI/EditorParticleSystemWidgetPrivate.h"

// Cycle 14 (M2): Mesh emitter 전용 RotationRate module — add-module 메뉴에서 사용자 노출.
#include "Particle/ParticleModuleMeshRotationRate.h"

void FEditorParticleSystemWidget::DrawEmittersPanel(const ImVec2& Size)
{
	DrawPanelHeader("Emitters");

	ResetPendingReorders();

	const ImVec2 BodySize(Size.x, std::max(1.0f, Size.y - PanelHeaderHeight));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.045f, 0.046f, 0.055f, 1.0f));
	ImGui::BeginChild("##ParticleEmitterColumnScroller", BodySize, false, ImGuiWindowFlags_HorizontalScrollbar);

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		!ImGui::GetIO().WantTextInput &&
		ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		DeleteSelectedEmitter();
	}

	const TArray<UParticleEmitter*>* Emitters = ParticleSystemAsset ? &ParticleSystemAsset->GetEmitters() : nullptr;
	bool bMouseInsideEmitterColumnArea = false;
	if (!Emitters || Emitters->empty())
	{
		const ImVec2 Cursor = ImGui::GetCursorScreenPos();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Max(Cursor.x + BodySize.x, Cursor.y + BodySize.y);
		DrawList->AddRectFilled(Cursor, Max, ImGui::GetColorU32(ImVec4(0.055f, 0.056f, 0.066f, 1.0f)));
		DrawList->AddText(
			ImVec2(Cursor.x + 14.0f, Cursor.y + 14.0f),
			ImGui::GetColorU32(ImVec4(0.58f, 0.61f, 0.66f, 1.0f)),
			"No emitters");
		ImGui::Dummy(BodySize);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			SelectParticleSystem();
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			SelectParticleSystem();
			OpenEmitterContextMenu(-1, NoParticleModuleSelection);
		}
	}
	else
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		const float ColumnHeight = std::max(1.0f, BodySize.y - ImGui::GetStyle().ScrollbarSize);
		bool bHasEmitterColumnBounds = false;
		ImVec2 EmitterColumnAreaMin(0.0f, 0.0f);
		ImVec2 EmitterColumnAreaMax(0.0f, 0.0f);
		for (int32 EmitterIndex = 0; EmitterIndex < static_cast<int32>(Emitters->size()); ++EmitterIndex)
		{
			if (EmitterIndex > 0)
			{
				ImGui::SameLine(0.0f, 0.0f);
			}
			ImGui::BeginGroup();
			DrawEmitterColumn((*Emitters)[EmitterIndex], EmitterIndex, ColumnHeight);
			ImGui::EndGroup();

			const ImVec2 ColumnItemMin = ImGui::GetItemRectMin();
			const ImVec2 ColumnItemMax = ImGui::GetItemRectMax();
			if (!bHasEmitterColumnBounds)
			{
				EmitterColumnAreaMin = ColumnItemMin;
				EmitterColumnAreaMax = ColumnItemMax;
				bHasEmitterColumnBounds = true;
			}
			else
			{
				EmitterColumnAreaMin.x = std::min(EmitterColumnAreaMin.x, ColumnItemMin.x);
				EmitterColumnAreaMin.y = std::min(EmitterColumnAreaMin.y, ColumnItemMin.y);
				EmitterColumnAreaMax.x = std::max(EmitterColumnAreaMax.x, ColumnItemMax.x);
				EmitterColumnAreaMax.y = std::max(EmitterColumnAreaMax.y, ColumnItemMax.y);
			}
		}
		if (bHasEmitterColumnBounds)
		{
			const ImVec2 MousePos = ImGui::GetIO().MousePos;
			bMouseInsideEmitterColumnArea = IsPointInsideRect(MousePos, EmitterColumnAreaMin, EmitterColumnAreaMax);
		}
		ImGui::PopStyleVar();
	}

	ApplyPendingReorders();

	if (ImGui::IsWindowHovered() &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		!bMouseInsideEmitterColumnArea &&
		!ImGui::IsAnyItemHovered())
	{
		SelectParticleSystem();
	}

	if (ImGui::IsWindowHovered() &&
		ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
		!bOpenEmitterContextMenu &&
		!bMouseInsideEmitterColumnArea &&
		!ImGui::IsAnyItemHovered())
	{
		SelectParticleSystem();
		OpenEmitterContextMenu(-1, NoParticleModuleSelection);
	}

	if (bOpenEmitterContextMenu)
	{
		ImGui::OpenPopup("##ParticleEmitterContextMenu");
		bOpenEmitterContextMenu = false;
	}
	DrawEmitterContextMenu();
	DrawEmitterRenamePopup();
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void FEditorParticleSystemWidget::DrawEmitterContextMenu()
{
	if (!BeginParticlePopup("##ParticleEmitterContextMenu"))
	{
		return;
	}

	const int32 TargetEmitterIndex = ContextEmitterIndex >= 0 ? ContextEmitterIndex : SelectedEmitterIndex;
	const bool bHasTargetEmitter =
		ParticleSystemAsset &&
		TargetEmitterIndex >= 0 &&
		TargetEmitterIndex < static_cast<int32>(ParticleSystemAsset->Emitters.size());
	UParticleEmitter* TargetEmitter = bHasTargetEmitter ? ParticleSystemAsset->Emitters[TargetEmitterIndex] : nullptr;
	UParticleLODLevel* TargetLODLevel = GetEmitterLODLevel(TargetEmitter);
	const bool bHasSpawnModule = TargetLODLevel && TargetLODLevel->GetSpawnModule();
	UParticleModule* TargetModule =
		TargetLODLevel &&
		ContextModuleIndex >= 0 &&
		ContextModuleIndex < static_cast<int32>(TargetLODLevel->Modules.size())
			? TargetLODLevel->Modules[ContextModuleIndex]
			: nullptr;
	const EParticleEmitterRenderMode CurrentRenderMode = TargetLODLevel
		? TargetLODLevel->GetEffectiveRenderMode()
		: EParticleEmitterRenderMode::Sprite;
	const bool bTargetModuleInherited = CurrentLOD > 0 && IsInheritedLODModule(TargetModule);

	auto DrawTypeDataItems = [&](bool bUseCreateLabels)
	{
		constexpr EParticleEmitterRenderMode RenderModes[] =
		{
			EParticleEmitterRenderMode::Sprite,
			EParticleEmitterRenderMode::Mesh,
			EParticleEmitterRenderMode::Beam,
			EParticleEmitterRenderMode::Ribbon,
		};
		for (EParticleEmitterRenderMode RenderMode : RenderModes)
		{
			const char* Label = bUseCreateLabels ? GetTypeDataMenuItemLabel(RenderMode) : GetRenderModeLabel(RenderMode);
			if (ImGui::MenuItem(Label, nullptr, CurrentRenderMode == RenderMode, bHasTargetEmitter && !bTargetModuleInherited))
			{
				ChangeEmitterRenderMode(TargetEmitterIndex, RenderMode);
			}
		}
	};

	if (ContextEmitterIndex < 0 && ContextModuleIndex == NoParticleModuleSelection)
	{
		if (ImGui::MenuItem("New Particle Sprite Emitter"))
		{
			AddDefaultEmitter();
		}
		EndParticlePopup();
		return;
	}

	if (ContextModuleIndex != NoParticleModuleSelection)
	{
		const bool bCanDuplicateFromHigher = bHasTargetEmitter && CurrentLOD > 0;
		const bool bRendererPropertiesTarget = ContextModuleIndex == RendererPropertiesSelection;
		if (bRendererPropertiesTarget || Cast<UParticleModuleTypeDataBase>(TargetModule))
		{
			ImGui::TextDisabled("EMITTER TYPE");
			ImGui::Separator();
			DrawTypeDataItems(false);
			if (!bRendererPropertiesTarget)
			{
				ImGui::Separator();
				if (ImGui::MenuItem("Duplicate From Higher", nullptr, false, bCanDuplicateFromHigher))
				{
					DuplicateModuleFromHigherLOD(TargetEmitterIndex, ContextModuleIndex, false);
				}
				if (ImGui::MenuItem("Duplicate From Highest", nullptr, false, bCanDuplicateFromHigher))
				{
					DuplicateModuleFromHigherLOD(TargetEmitterIndex, ContextModuleIndex, true);
				}
			}
			EndParticlePopup();
			return;
		}
		if (ImGui::MenuItem("Delete Module"))
		{
			DeleteModule(TargetEmitterIndex, ContextModuleIndex);
		}
		if (ImGui::MenuItem("Duplicate From Higher", nullptr, false, bCanDuplicateFromHigher))
		{
			DuplicateModuleFromHigherLOD(TargetEmitterIndex, ContextModuleIndex, false);
		}
		if (ImGui::MenuItem("Duplicate From Highest", nullptr, false, bCanDuplicateFromHigher))
		{
			DuplicateModuleFromHigherLOD(TargetEmitterIndex, ContextModuleIndex, true);
		}
		EndParticlePopup();
		return;
	}

	if (BeginParticleMenu("Emitter", bHasTargetEmitter))
	{
		ImGui::TextDisabled("EMITTER");
		ImGui::Separator();
		if (ImGui::MenuItem("Rename Emitter"))
		{
			BeginRenameEmitter(TargetEmitterIndex);
		}
		if (ImGui::MenuItem("Duplicate Emitter", nullptr, false, bHasTargetEmitter))
		{
			DuplicateEmitter(TargetEmitterIndex);
		}
		if (ImGui::MenuItem("Delete Emitter"))
		{
			DeleteEmitter(TargetEmitterIndex);
		}
		EndParticleMenu();
	}

	if (BeginParticleMenu("Particle System"))
	{
		ImGui::TextDisabled("PARTICLE SYSTEM");
		ImGui::Separator();
		if (ImGui::MenuItem("Select Particle System"))
		{
			SelectParticleSystem();
		}
		if (ImGui::MenuItem("Add New Emitter Before", nullptr, false, bHasTargetEmitter))
		{
			AddDefaultEmitterAt(TargetEmitterIndex);
		}
		if (ImGui::MenuItem("Add New Emitter After", nullptr, false, bHasTargetEmitter))
		{
			AddDefaultEmitterAt(TargetEmitterIndex + 1);
		}
		if (!bHasTargetEmitter && ImGui::MenuItem("Add Emitter"))
		{
			AddDefaultEmitter();
		}
		ImGui::MenuItem("Remove Duplicate Modules", nullptr, false, false);
		EndParticleMenu();
	}

	auto AddModuleToTargetEmitter = [&](UParticleModule* Module)
	{
		AddModuleToEmitter(TargetEmitterIndex, Module);
	};

	if (BeginParticleMenu("TypeData", bHasTargetEmitter))
	{
		ImGui::TextDisabled("TYPEDATA");
		ImGui::Separator();
		DrawTypeDataItems(true);
		EndParticleMenu();
	}
	DrawParticleModuleAddMenu<UParticleModuleAcceleration>("Acceleration", "Acceleration", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawDisabledParticleModuleMenu("Attraction");
	DrawParticleModuleAddMenu<UParticleModuleBurst>("Burst", "Burst", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawDisabledParticleModuleMenu("Camera");
	DrawParticleModuleAddMenu<UParticleModuleCollision>("Collision", "Collision", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawParticleModuleAddMenu<UParticleModuleColor>("Color", "Color Over Life", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawParticleModuleAddMenu<UParticleModuleEventGenerator>("Event", "Event Generator", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawDisabledParticleModuleMenu("Kill");
	DrawParticleModuleAddMenu<UParticleModuleLifetime>("Lifetime", "Lifetime", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawParticleModuleAddMenu<UParticleModuleLight>("Light", "Light", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawParticleModuleAddMenu<UParticleModuleLocation>("Location", "Initial Location", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawParticleModuleAddMenu<UParticleModuleLocationShape>("Location", "Shape Location", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawParticleModuleAddMenu<UParticleModuleRotationRate>("Rotation", "Initial Rotation Rate", bHasTargetEmitter, AddModuleToTargetEmitter);
	// Cycle 14 (M2): disabled placeholder → enabled menu 교체.
	// UParticleModuleMeshRotationRate 는 Mesh emitter 전용 — Mesh 가 아닌 emitter 에 추가하면 runtime 에 Cast nullptr → no-op (위험 13 방어).
	DrawParticleModuleAddMenu<UParticleModuleMeshRotationRate>("Rotation Rate", "Mesh Rotation Rate", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawParticleModuleAddMenu<UParticleModuleDrag>("Drag", "Drag", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawDisabledParticleModuleMenu("Orbit");
	DrawDisabledParticleModuleMenu("Orientation");
	DrawDisabledParticleModuleMenu("Parameter");
	DrawParticleModuleAddMenu<UParticleModuleSize>("Size", "Size By Life", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawParticleModuleAddMenu<UParticleModuleSpawn>("Spawn", "Spawn", bHasTargetEmitter && !bHasSpawnModule, AddModuleToTargetEmitter);
	DrawParticleModuleAddMenu<USubUVModule>("SubUV", "SubUV", bHasTargetEmitter, AddModuleToTargetEmitter);
	DrawParticleModuleAddMenu<UParticleModuleVelocity>("Velocity", "Initial Velocity", bHasTargetEmitter, AddModuleToTargetEmitter);

	EndParticlePopup();
}

void FEditorParticleSystemWidget::AddDefaultEmitter()
{
	const int32 InsertIndex = ParticleSystemAsset ? static_cast<int32>(ParticleSystemAsset->Emitters.size()) : 0;
	AddDefaultEmitterAt(InsertIndex);
}

void FEditorParticleSystemWidget::AddDefaultEmitterAt(int32 InsertIndex)
{
	if (!ParticleSystemAsset)
	{
		ParticleSystemAsset = UObjectManager::Get().CreateObject<UParticleSystem>();
		if (!ParticleSystemAsset)
		{
			return;
		}

		const FString SystemName = DocumentPath.empty() ? "Particle System" : DocumentPath;
		ParticleSystemAsset->SetFName(FName(SystemName));
	}

	UParticleEmitter* NewEmitter = CreateDefaultParticleEmitter(MakeUniqueEmitterName(ParticleSystemAsset));
	if (!NewEmitter)
	{
		return;
	}
	if (CurrentLOD > 0)
	{
		UParticleLODLevel* SourceLOD = NewEmitter->GetLODLevel(0);
		for (int32 LODIndex = 1; LODIndex <= CurrentLOD && SourceLOD; ++LODIndex)
		{
			UParticleLODLevel* NewLOD = Cast<UParticleLODLevel>(SourceLOD->Duplicate());
			if (!NewLOD)
			{
				continue;
			}
			NewLOD->Level = LODIndex;
			NewLOD->DistanceThreshold = SourceLOD->GetDistanceThreshold() + static_cast<float>(LODIndex) * 1000.0f;
			MarkLODModulesInheritedFromHigherLODExceptSpawn(NewLOD);
			NewEmitter->LODLevels.push_back(NewLOD);
		}
		NewEmitter->CacheEmitterModuleInfo();
	}

	CaptureUndoSnapshot("Add Emitter");
	InsertIndex = std::clamp(InsertIndex, 0, static_cast<int32>(ParticleSystemAsset->Emitters.size()));
	ParticleSystemAsset->Emitters.insert(ParticleSystemAsset->Emitters.begin() + InsertIndex, NewEmitter);
	SelectEmitter(InsertIndex);
	ClearEmitterContext();
	bDirty = true;
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::AddLODToSelectedEmitter()
{
	UParticleEmitter* Emitter = GetSelectedEmitter();
	const int32 InsertIndex = Emitter ? static_cast<int32>(Emitter->GetLODLevels().size()) : 0;
	AddLODToSelectedEmitterAt(InsertIndex);
}

void FEditorParticleSystemWidget::AddLODToSelectedEmitterAt(int32 InsertIndex)
{
	UParticleEmitter* Emitter = GetSelectedEmitter();
	if (!Emitter)
	{
		return;
	}

	const TArray<UParticleLODLevel*>& LODLevels = Emitter->GetLODLevels();
	const int32 OldLODCount = static_cast<int32>(LODLevels.size());
	InsertIndex = std::clamp(InsertIndex, 0, OldLODCount);

	float NewThreshold = 100000.0f;
	if (OldLODCount > 0)
	{
		if (InsertIndex <= 0)
		{
			const float FirstThreshold = LODLevels[0] ? LODLevels[0]->GetDistanceThreshold() : 1000.0f;
			NewThreshold = FirstThreshold > 1000.0f ? FirstThreshold - 1000.0f : FirstThreshold * 0.5f;
		}
		else if (InsertIndex >= OldLODCount)
		{
			float MaxThreshold = 0.0f;
			for (const UParticleLODLevel* LODLevel : LODLevels)
			{
				if (LODLevel)
				{
					MaxThreshold = std::max(MaxThreshold, LODLevel->GetDistanceThreshold());
				}
			}
			NewThreshold = MaxThreshold + 1000.0f;
		}
		else
		{
			const float PrevThreshold = LODLevels[InsertIndex - 1] ? LODLevels[InsertIndex - 1]->GetDistanceThreshold() : 0.0f;
			const float NextThreshold = LODLevels[InsertIndex] ? LODLevels[InsertIndex]->GetDistanceThreshold() : PrevThreshold + 1000.0f;
			NewThreshold = (PrevThreshold + NextThreshold) * 0.5f;
			if (std::abs(NewThreshold - PrevThreshold) < 0.01f || std::abs(NewThreshold - NextThreshold) < 0.01f)
			{
				NewThreshold = PrevThreshold + 1.0f;
			}
		}
	}

	CaptureUndoSnapshot("Add Particle LOD");

	UParticleLODLevel* NewLOD = nullptr;
	UParticleLODLevel* SourceLOD = Emitter->GetLODLevel(std::clamp(CurrentLOD, 0, std::max(0, OldLODCount - 1)));
	if (SourceLOD)
	{
		NewLOD = Cast<UParticleLODLevel>(SourceLOD->Duplicate());
		if (NewLOD)
		{
			NewLOD->Level = InsertIndex;
			NewLOD->bEnabled = true;
			NewLOD->DistanceThreshold = NewThreshold;
			NewLOD->SetFName(FName("LOD" + std::to_string(InsertIndex)));
			Emitter->LODLevels.insert(Emitter->LODLevels.begin() + InsertIndex, NewLOD);
		}
	}

	if (!NewLOD)
	{
		NewLOD = UObjectManager::Get().CreateObject<UParticleLODLevel>();
		if (NewLOD)
		{
			NewLOD->Level = InsertIndex;
			NewLOD->bEnabled = true;
			NewLOD->DistanceThreshold = NewThreshold;
			NewLOD->SetFName(FName("LOD" + std::to_string(InsertIndex)));
			NewLOD->EnsureRequiredModule();
			NewLOD->EnsureRendererProperties(EParticleEmitterRenderMode::Sprite);
			NewLOD->EnsureSpawnModule();
			NewLOD->AddModule<UParticleModuleLifetime>();
			NewLOD->AddModule<UParticleModuleLocation>();
			NewLOD->AddModule<UParticleModuleVelocity>();
			NewLOD->AddModule<UParticleModuleColor>();
			NewLOD->AddModule<UParticleModuleSize>();
			Emitter->LODLevels.insert(Emitter->LODLevels.begin() + InsertIndex, NewLOD);
		}
	}

	if (!NewLOD)
	{
		return;
	}

	for (int32 LODIndex = 0; LODIndex < static_cast<int32>(Emitter->LODLevels.size()); ++LODIndex)
	{
		if (UParticleLODLevel* LODLevel = Emitter->LODLevels[LODIndex])
		{
			LODLevel->Level = LODIndex;
			LODLevel->SetFName(FName("LOD" + std::to_string(LODIndex)));
		}
	}

	Emitter->CacheEmitterModuleInfo();
	auto It = std::find(Emitter->LODLevels.begin(), Emitter->LODLevels.end(), NewLOD);
	CurrentLOD = It != Emitter->LODLevels.end()
		? static_cast<int32>(std::distance(Emitter->LODLevels.begin(), It))
		: InsertIndex;

	SelectEmitter(SelectedEmitterIndex);
	ClearEmitterContext();
	bDirty = true;
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::SelectLowerLOD()
{
	SetCurrentLOD(CurrentLOD + 1);
}

void FEditorParticleSystemWidget::SelectHigherLOD()
{
	SetCurrentLOD(CurrentLOD - 1);
}

void FEditorParticleSystemWidget::SelectLowestLOD()
{
	const int32 MaxLODCount = GetMaxLODCount();
	SetCurrentLOD(MaxLODCount > 0 ? MaxLODCount - 1 : 0);
}

void FEditorParticleSystemWidget::DeleteSelectedEmitter()
{
	if (SelectedModuleIndex != NoParticleModuleSelection)
	{
		DeleteModule(SelectedEmitterIndex, SelectedModuleIndex);
		return;
	}
	DeleteEmitter(SelectedEmitterIndex);
}

void FEditorParticleSystemWidget::DeleteEmitter(int32 EmitterIndex)
{
	if (!ParticleSystemAsset || EmitterIndex < 0 || EmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return;
	}

	CaptureUndoSnapshot("Delete Emitter");
	UParticleEmitter* RemovedEmitter = ParticleSystemAsset->Emitters[EmitterIndex];
	ParticleSystemAsset->Emitters.erase(ParticleSystemAsset->Emitters.begin() + EmitterIndex);
	if (RemovedEmitter)
	{
		UObjectManager::Get().DestroyObject(RemovedEmitter);
	}
	for (auto It = SoloEmitterIndices.begin(); It != SoloEmitterIndices.end();)
	{
		if (*It == EmitterIndex)
		{
			It = SoloEmitterIndices.erase(It);
			continue;
		}
		if (*It > EmitterIndex)
		{
			--(*It);
		}
		++It;
	}
	ClearEmitterContext();
	if (ParticleSystemAsset->Emitters.empty())
	{
		SelectParticleSystem();
	}
	else
	{
		SelectEmitter(std::clamp(EmitterIndex, 0, static_cast<int32>(ParticleSystemAsset->Emitters.size()) - 1));
	}
	bDirty = true;
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::DuplicateEmitter(int32 EmitterIndex)
{
	if (!ParticleSystemAsset ||
		EmitterIndex < 0 ||
		EmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* SourceEmitter = ParticleSystemAsset->Emitters[EmitterIndex];
	SyncParticleDistributionRuntimeDataToAsset();
	CaptureUndoSnapshot("Duplicate Emitter");
	UParticleEmitter* NewEmitter = SourceEmitter
		? Cast<UParticleEmitter>(SourceEmitter->Duplicate())
		: nullptr;
	if (!NewEmitter)
	{
		return;
	}

	const FString NewName = MakeUniqueEmitterName(ParticleSystemAsset);
	NewEmitter->SetFName(FName(NewName));
	for (int32 LODIndex = 0; LODIndex < static_cast<int32>(NewEmitter->LODLevels.size()); ++LODIndex)
	{
		if (NewEmitter->LODLevels[LODIndex])
		{
			NewEmitter->LODLevels[LODIndex]->Level = LODIndex;
		}
	}
	NewEmitter->CacheEmitterModuleInfo();

	const int32 InsertIndex = std::clamp(EmitterIndex + 1, 0, static_cast<int32>(ParticleSystemAsset->Emitters.size()));
	ParticleSystemAsset->Emitters.insert(ParticleSystemAsset->Emitters.begin() + InsertIndex, NewEmitter);
	for (int32& SoloEmitterIndex : SoloEmitterIndices)
	{
		if (SoloEmitterIndex >= InsertIndex)
		{
			++SoloEmitterIndex;
		}
	}
	SelectEmitter(InsertIndex);
	ClearEmitterContext();
	bDirty = true;
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::AddModuleToEmitter(int32 EmitterIndex, UParticleModule* Module)
{
	if (!ParticleSystemAsset ||
		!Module ||
		EmitterIndex < 0 ||
		EmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* Emitter = ParticleSystemAsset->Emitters[EmitterIndex];
	if (!Emitter)
	{
		return;
	}

	UParticleLODLevel* LODLevel = GetEmitterLODLevel(Emitter);
	if (!LODLevel)
	{
		return;
	}

	CaptureUndoSnapshot("Add Particle Module");
	LODLevel->Modules.push_back(Module);
	Emitter->CacheEmitterModuleInfo();
	SelectModule(EmitterIndex, static_cast<int32>(LODLevel->Modules.size()) - 1);
	ClearEmitterContext();
	bDirty = true;
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::DeleteModule(int32 EmitterIndex, int32 ModuleIndex)
{
	if (!ParticleSystemAsset ||
		EmitterIndex < 0 ||
		EmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* Emitter = ParticleSystemAsset->Emitters[EmitterIndex];
	if (!Emitter)
	{
		return;
	}

	UParticleLODLevel* LODLevel = GetEmitterLODLevel(Emitter);
	if (!LODLevel)
	{
		return;
	}

	if (ModuleIndex == RequiredParticleModuleSelection)
	{
		ShowCenterToast("Required module cannot be deleted.");
		return;
	}
	if (ModuleIndex < 0 || ModuleIndex >= static_cast<int32>(LODLevel->Modules.size()))
	{
		return;
	}

	UParticleModule* Module = LODLevel->Modules[ModuleIndex];
	if (Cast<UParticleModuleSpawn>(Module))
	{
		ShowCenterToast("Spawn module cannot be deleted.");
		return;
	}
	if (Cast<UParticleModuleTypeDataBase>(Module))
	{
		ShowCenterToast("Type data module cannot be deleted.");
		return;
	}

	CaptureUndoSnapshot("Delete Particle Module");
	LODLevel->RemoveModule(Module);
	if (LODLevel->Modules.empty())
	{
		SelectEmitter(EmitterIndex);
	}
	else
	{
		SelectModule(EmitterIndex, std::clamp(ModuleIndex, 0, static_cast<int32>(LODLevel->Modules.size()) - 1));
	}
	ClearEmitterContext();
	bDirty = true;
	RefreshPreviewComponent(true);
}

int32 FEditorParticleSystemWidget::GetMaxLODCount() const
{
	int32 MaxLODCount = 0;
	if (!ParticleSystemAsset)
	{
		return MaxLODCount;
	}
	for (const UParticleEmitter* Emitter : ParticleSystemAsset->Emitters)
	{
		if (Emitter)
		{
			MaxLODCount = std::max(MaxLODCount, static_cast<int32>(Emitter->GetLODLevels().size()));
		}
	}
	return MaxLODCount;
}

void FEditorParticleSystemWidget::SetCurrentLOD(int32 NewLOD)
{
	const int32 MaxLODCount = GetMaxLODCount();
	CurrentLOD = MaxLODCount > 0
		? std::clamp(NewLOD, 0, MaxLODCount - 1)
		: std::max(0, NewLOD);
	ClampSelectionToParticleSystem();
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::AddLODRelativeToCurrent(int32 Offset)
{
	if (!ParticleSystemAsset || ParticleSystemAsset->Emitters.empty())
	{
		ShowCenterToast("Add an emitter before adding LOD.");
		return;
	}

	const bool bAddAfterCurrent = Offset > 0;
	const int32 InsertIndex = std::max(0, CurrentLOD + (bAddAfterCurrent ? 1 : 0));
	CaptureUndoSnapshot(bAddAfterCurrent ? "Add LOD After" : "Add LOD Before");

	for (UParticleEmitter* Emitter : ParticleSystemAsset->Emitters)
	{
		if (!Emitter)
		{
			continue;
		}

		const int32 LODCount = static_cast<int32>(Emitter->LODLevels.size());
		const int32 SourceIndex = LODCount > 0 ? std::clamp(CurrentLOD, 0, LODCount - 1) : 0;
		UParticleLODLevel* SourceLOD = Emitter->GetLODLevel(SourceIndex);
		const int32 ClampedInsertIndex = std::clamp(InsertIndex, 0, LODCount);

		float NewThreshold = 100000.0f;
		if (LODCount > 0)
		{
			const UParticleLODLevel* PrevLOD = ClampedInsertIndex > 0 ? Emitter->LODLevels[ClampedInsertIndex - 1] : nullptr;
			const UParticleLODLevel* NextLOD = ClampedInsertIndex < LODCount ? Emitter->LODLevels[ClampedInsertIndex] : nullptr;
			if (PrevLOD && NextLOD)
			{
				NewThreshold = (PrevLOD->GetDistanceThreshold() + NextLOD->GetDistanceThreshold()) * 0.5f;
			}
			else if (PrevLOD)
			{
				NewThreshold = PrevLOD->GetDistanceThreshold() + 1000.0f;
			}
			else if (NextLOD)
			{
				const float NextThreshold = NextLOD->GetDistanceThreshold();
				NewThreshold = NextThreshold > 1000.0f ? NextThreshold - 1000.0f : NextThreshold * 0.5f;
			}
		}

		UParticleLODLevel* NewLOD = SourceLOD
			? Cast<UParticleLODLevel>(SourceLOD->Duplicate())
			: UObjectManager::Get().CreateObject<UParticleLODLevel>();
		if (!NewLOD)
		{
			continue;
		}

		MarkLODModulesInheritedFromHigherLOD(NewLOD);
		NewLOD->Level = ClampedInsertIndex;
		NewLOD->DistanceThreshold = NewThreshold;
		NewLOD->SetFName(FName("LOD" + std::to_string(ClampedInsertIndex)));
		if (!SourceLOD)
		{
			NewLOD->EnsureRequiredModule();
			NewLOD->EnsureRendererProperties(EParticleEmitterRenderMode::Sprite);
			NewLOD->EnsureSpawnModule();
		}
		Emitter->LODLevels.insert(Emitter->LODLevels.begin() + ClampedInsertIndex, NewLOD);
		for (int32 LODIndex = 0; LODIndex < static_cast<int32>(Emitter->LODLevels.size()); ++LODIndex)
		{
			if (Emitter->LODLevels[LODIndex])
			{
				Emitter->LODLevels[LODIndex]->Level = LODIndex;
				Emitter->LODLevels[LODIndex]->SetFName(FName("LOD" + std::to_string(LODIndex)));
			}
		}
		Emitter->CacheEmitterModuleInfo();
	}

	ParticleSystemAsset->CacheEmitterModuleInfo();
	SyncParticleSystemLODPropertiesFromEmitters();
	SetCurrentLOD(InsertIndex);
	ClearEmitterContext();
	bDirty = true;
}

void FEditorParticleSystemWidget::DeleteCurrentLOD()
{
	if (!ParticleSystemAsset || CurrentLOD <= 0)
	{
		ShowCenterToast("LOD 0 cannot be deleted.");
		return;
	}

	bool bRemovedAny = false;
	CaptureUndoSnapshot("Delete LOD");
	for (UParticleEmitter* Emitter : ParticleSystemAsset->Emitters)
	{
		if (!Emitter || CurrentLOD >= static_cast<int32>(Emitter->LODLevels.size()))
		{
			continue;
		}
		Emitter->RemoveLODLevel(CurrentLOD);
		for (int32 LODIndex = 0; LODIndex < static_cast<int32>(Emitter->LODLevels.size()); ++LODIndex)
		{
			if (Emitter->LODLevels[LODIndex])
			{
				Emitter->LODLevels[LODIndex]->Level = LODIndex;
			}
		}
		bRemovedAny = true;
	}

	if (!bRemovedAny)
	{
		return;
	}

	ParticleSystemAsset->CacheEmitterModuleInfo();
	SyncParticleSystemLODPropertiesFromEmitters();
	SetCurrentLOD(CurrentLOD - 1);
	ClearEmitterContext();
	bDirty = true;
}

void FEditorParticleSystemWidget::DuplicateModuleFromHigherLOD(int32 EmitterIndex, int32 ModuleIndex, bool bHighest)
{
	if (!ParticleSystemAsset ||
		CurrentLOD <= 0 ||
		EmitterIndex < 0 ||
		EmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* Emitter = ParticleSystemAsset->Emitters[EmitterIndex];
	if (!Emitter ||
		CurrentLOD >= static_cast<int32>(Emitter->LODLevels.size()))
	{
		return;
	}

	const int32 SourceLODIndex = bHighest ? 0 : CurrentLOD - 1;
	UParticleLODLevel* SourceLOD = Emitter->GetLODLevel(SourceLODIndex);
	UParticleLODLevel* TargetLOD = Emitter->GetLODLevel(CurrentLOD);
	if (!SourceLOD || !TargetLOD)
	{
		return;
	}

	CaptureUndoSnapshot(bHighest ? "Duplicate Module From Highest LOD" : "Duplicate Module From Higher LOD");
	if (ModuleIndex == RequiredParticleModuleSelection)
	{
		UParticleModuleRequired* NewRequired = DuplicateRequiredModuleForLOD(SourceLOD->GetRequiredModule(), false);
		if (!NewRequired)
		{
			return;
		}
		if (TargetLOD->RequiredModule)
		{
			UObjectManager::Get().DestroyObject(TargetLOD->RequiredModule);
		}
		TargetLOD->RequiredModule = NewRequired;
	}
	else
	{
		if (ModuleIndex < 0 || ModuleIndex >= static_cast<int32>(SourceLOD->Modules.size()))
		{
			return;
		}
		UParticleModule* NewModule = DuplicateParticleModuleForLOD(SourceLOD->Modules[ModuleIndex], false);
		if (!NewModule)
		{
			return;
		}
		if (ModuleIndex < static_cast<int32>(TargetLOD->Modules.size()) && TargetLOD->Modules[ModuleIndex])
		{
			UObjectManager::Get().DestroyObject(TargetLOD->Modules[ModuleIndex]);
			TargetLOD->Modules[ModuleIndex] = NewModule;
		}
		else
		{
			TargetLOD->Modules.push_back(NewModule);
			ModuleIndex = static_cast<int32>(TargetLOD->Modules.size()) - 1;
		}
	}

	TargetLOD->CacheModuleLists();
	Emitter->CacheEmitterModuleInfo();
	ParticleSystemAsset->CacheEmitterModuleInfo();
	SelectModule(EmitterIndex, ModuleIndex);
	ClearEmitterContext();
	bDirty = true;
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::SyncInheritedModuleFromHigherLOD(UParticleEmitter* OwnerEmitter, UParticleModule* SourceModule)
{
	if (!OwnerEmitter || !SourceModule)
	{
		return;
	}

	int32 SourceLODIndex = -1;
	int32 SourceModuleIndex = NoParticleModuleSelection;
	for (int32 LODIndex = 0; LODIndex < static_cast<int32>(OwnerEmitter->LODLevels.size()); ++LODIndex)
	{
		UParticleLODLevel* LODLevel = OwnerEmitter->LODLevels[LODIndex];
		if (!LODLevel)
		{
			continue;
		}

		if (LODLevel->GetRequiredModule() == SourceModule)
		{
			SourceLODIndex = LODIndex;
			SourceModuleIndex = RequiredParticleModuleSelection;
			break;
		}

		for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(LODLevel->Modules.size()); ++ModuleIndex)
		{
			if (LODLevel->Modules[ModuleIndex] == SourceModule)
			{
				SourceLODIndex = LODIndex;
				SourceModuleIndex = ModuleIndex;
				break;
			}
		}
		if (SourceLODIndex >= 0)
		{
			break;
		}
	}

	if (SourceLODIndex < 0)
	{
		return;
	}

	for (int32 LODIndex = SourceLODIndex + 1; LODIndex < static_cast<int32>(OwnerEmitter->LODLevels.size()); ++LODIndex)
	{
		UParticleLODLevel* TargetLOD = OwnerEmitter->LODLevels[LODIndex];
		if (!TargetLOD)
		{
			continue;
		}

		if (SourceModuleIndex == RequiredParticleModuleSelection)
		{
			UParticleModuleRequired* TargetRequired = TargetLOD->GetRequiredModule();
			if (!IsInheritedLODModule(TargetRequired))
			{
				continue;
			}

			UParticleModuleRequired* NewRequired = DuplicateRequiredModuleForLOD(Cast<UParticleModuleRequired>(SourceModule), true);
			if (!NewRequired)
			{
				continue;
			}
			UObjectManager::Get().DestroyObject(TargetLOD->RequiredModule);
			TargetLOD->RequiredModule = NewRequired;
			TargetLOD->CacheModuleLists();
			continue;
		}

		if (SourceModuleIndex < 0 || SourceModuleIndex >= static_cast<int32>(TargetLOD->Modules.size()))
		{
			continue;
		}

		UParticleModule* TargetModule = TargetLOD->Modules[SourceModuleIndex];
		if (!IsInheritedLODModule(TargetModule))
		{
			continue;
		}

		UParticleModule* NewModule = DuplicateParticleModuleForLOD(SourceModule, true);
		if (!NewModule)
		{
			continue;
		}
		UObjectManager::Get().DestroyObject(TargetModule);
		TargetLOD->Modules[SourceModuleIndex] = NewModule;
		TargetLOD->CacheModuleLists();
	}

	OwnerEmitter->CacheEmitterModuleInfo();
	if (ParticleSystemAsset)
	{
		ParticleSystemAsset->CacheEmitterModuleInfo();
	}
}

void FEditorParticleSystemWidget::ChangeEmitterRenderMode(int32 EmitterIndex, EParticleEmitterRenderMode RenderMode)
{
	if (!ParticleSystemAsset ||
		EmitterIndex < 0 ||
		EmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* Emitter = ParticleSystemAsset->Emitters[EmitterIndex];
	UParticleLODLevel* LODLevel = GetEmitterLODLevel(Emitter);
	UParticleModuleRequired* Required = LODLevel ? LODLevel->GetRequiredModule() : nullptr;
	if (!Emitter || !LODLevel || !Required)
	{
		return;
	}

	const bool bHasMatchingTypeData =
		LODLevel->GetTypeDataModule() &&
		LODLevel->GetTypeDataModule()->GetRenderMode() == RenderMode;
	bool bHasLegacyTypeDataModule = LODLevel->GetTypeDataModule() != nullptr;
	for (UParticleModule* Module : LODLevel->GetModules())
	{
		if (Cast<UParticleModuleTypeDataBase>(Module))
		{
			bHasLegacyTypeDataModule = true;
			break;
		}
	}
	UParticleRendererProperties* CurrentRenderer = LODLevel->GetEffectiveRendererProperties();
	if (CurrentRenderer && CurrentRenderer->GetRenderMode() == RenderMode && !bHasMatchingTypeData && !bHasLegacyTypeDataModule)
	{
		return;
	}

	CaptureUndoSnapshot("Change Emitter Type");
	for (UParticleLODLevel* TargetLOD : Emitter->LODLevels)
	{
		if (!TargetLOD)
		{
			continue;
		}

		UParticleRendererProperties* NewRenderer = TargetLOD->EnsureRendererProperties(RenderMode);
		if (!NewRenderer)
		{
			ShowCenterToast("Unsupported emitter type.");
			continue;
		}

		if (UParticleMeshRendererProperties* MeshRenderer = Cast<UParticleMeshRendererProperties>(NewRenderer))
		{
			if (!MeshRenderer->GetMesh())
			{
				MeshRenderer->SetMesh(FResourceManager::Get().LoadStaticMesh("Asset/Mesh/apple_mid/apple_mid.uasset"));
				const FString DemoMatPath = "Asset/Material/Auto/apple_mid_Mat_0.uasset";
				FResourceManager::Get().DeserializeMaterial(DemoMatPath);
				UMaterial* DemoMaterial = FResourceManager::Get().GetMaterial(DemoMatPath);
				if (!DemoMaterial)
				{
					DemoMaterial = FResourceManager::Get().GetMaterial("apple_mid_Mat_0");
				}
				if (DemoMaterial)
				{
					MeshRenderer->SetOverrideMaterial(true, DemoMaterial);
				}
			}
		}

		if (UParticleModuleRequired* TargetRequired = TargetLOD->GetRequiredModule())
		{
			TargetRequired->SetRenderMode(RenderMode);
		}

		if (RenderMode == EParticleEmitterRenderMode::Beam)
		{
			EnsureBeamSupportModules(TargetLOD);
		}
		else
		{
			RemoveBeamSupportModules(TargetLOD);
		}
		TargetLOD->SetTypeDataModule(nullptr);
		TargetLOD->CacheModuleLists();
	}

	Emitter->CacheEmitterModuleInfo();
	ParticleSystemAsset->CacheEmitterModuleInfo();
	SyncInheritedModuleFromHigherLOD(Emitter, Required);
	SelectEmitter(EmitterIndex);
	ClearEmitterContext();
	bDirty = true;
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::BeginRenameEmitter(int32 EmitterIndex)
{
	if (!ParticleSystemAsset ||
		EmitterIndex < 0 ||
		EmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return;
	}

	RenameEmitterIndex = EmitterIndex;
	const FString EmitterName = GetEmitterDisplayName(ParticleSystemAsset->Emitters[EmitterIndex], EmitterIndex);
	std::snprintf(RenameEmitterBuffer, sizeof(RenameEmitterBuffer), "%s", EmitterName.c_str());
	bOpenRenameEmitterPopup = true;
}

bool FEditorParticleSystemWidget::ApplyEmitterName(int32 EmitterIndex, const FString& NewName, bool bCaptureUndo, bool bWarnOnEmpty)
{
	if (!ParticleSystemAsset ||
		EmitterIndex < 0 ||
		EmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return false;
	}

	const FString TrimmedName = TrimCopy(NewName);
	if (TrimmedName.empty())
	{
		if (bWarnOnEmpty && EditorEngine)
		{
			EditorEngine->GetNotificationService().Warning("Emitter name cannot be empty.");
		}
		return false;
	}

	UParticleEmitter* Emitter = ParticleSystemAsset->Emitters[EmitterIndex];
	if (!Emitter)
	{
		return false;
	}

	if (Emitter->GetFName().ToString() == TrimmedName)
	{
		return false;
	}

	if (bCaptureUndo)
	{
		CaptureUndoSnapshot("Rename Emitter");
	}
	Emitter->SetFName(FName(TrimmedName));
	SelectedEmitterIndex = EmitterIndex;
	SelectedModuleIndex = NoParticleModuleSelection;
	bDirty = true;
	return true;
}

void FEditorParticleSystemWidget::RenameEmitter(int32 EmitterIndex, const FString& NewName)
{
	ApplyEmitterName(EmitterIndex, NewName, true, true);
}

void FEditorParticleSystemWidget::DrawEmitterRenamePopup()
{
	if (bOpenRenameEmitterPopup)
	{
		ImGui::OpenPopup("Rename Emitter##ParticleEmitterRenamePopup");
		bOpenRenameEmitterPopup = false;
	}

	if (!BeginParticlePopupModal("Rename Emitter##ParticleEmitterRenamePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	ImGui::SetNextItemWidth(260.0f);
	const bool bEnterPressed = ImGui::InputText("Name", RenameEmitterBuffer, sizeof(RenameEmitterBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
	const bool bApply = bEnterPressed || ImGui::Button("OK", ImVec2(82.0f, 0.0f));
	if (bApply)
	{
		RenameEmitter(RenameEmitterIndex, RenameEmitterBuffer);
		RenameEmitterIndex = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(82.0f, 0.0f)))
	{
		RenameEmitterIndex = -1;
		ImGui::CloseCurrentPopup();
	}

	EndParticlePopupModal();
}

void FEditorParticleSystemWidget::SelectParticleSystem()
{
	SelectedEmitterIndex = -1;
	SelectedModuleIndex = NoParticleModuleSelection;
	bEmitterNameEditUndoCaptured = false;
}

void FEditorParticleSystemWidget::SelectEmitter(int32 EmitterIndex)
{
	SelectedEmitterIndex = EmitterIndex;
	SelectedModuleIndex = NoParticleModuleSelection;
	bEmitterNameEditUndoCaptured = false;
}

void FEditorParticleSystemWidget::SelectModule(int32 EmitterIndex, int32 ModuleIndex)
{
	SelectedEmitterIndex = EmitterIndex;
	SelectedModuleIndex = ModuleIndex;
	bEmitterNameEditUndoCaptured = false;
}

bool FEditorParticleSystemWidget::IsEmitterSolo(int32 EmitterIndex) const
{
	return std::find(SoloEmitterIndices.begin(), SoloEmitterIndices.end(), EmitterIndex) != SoloEmitterIndices.end();
}

bool FEditorParticleSystemWidget::HasSoloEmitters() const
{
	return !SoloEmitterIndices.empty();
}

void FEditorParticleSystemWidget::ToggleEmitterSolo(int32 EmitterIndex)
{
	if (!ParticleSystemAsset ||
		EmitterIndex < 0 ||
		EmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return;
	}

	auto It = std::find(SoloEmitterIndices.begin(), SoloEmitterIndices.end(), EmitterIndex);
	if (It != SoloEmitterIndices.end())
	{
		SoloEmitterIndices.erase(It);
	}
	else
	{
		SoloEmitterIndices.push_back(EmitterIndex);
	}
	ApplyPreviewSoloEmitters();
	RefreshPreviewComponent(false);
}

void FEditorParticleSystemWidget::ClearInvalidSoloEmitters()
{
	const int32 EmitterCount = ParticleSystemAsset ? static_cast<int32>(ParticleSystemAsset->Emitters.size()) : 0;
	for (auto It = SoloEmitterIndices.begin(); It != SoloEmitterIndices.end();)
	{
		if (*It < 0 || *It >= EmitterCount)
		{
			It = SoloEmitterIndices.erase(It);
			continue;
		}
		++It;
	}
}

void FEditorParticleSystemWidget::ApplyPreviewSoloEmitters()
{
	if (!PreviewComponent)
	{
		return;
	}

	ClearInvalidSoloEmitters();
	if (SoloEmitterIndices.empty())
	{
		PreviewComponent->ClearEditorPreviewSoloEmitters();
		return;
	}
	PreviewComponent->SetEditorPreviewSoloEmitters(SoloEmitterIndices);
}

void FEditorParticleSystemWidget::OpenEmitterContextMenu(int32 EmitterIndex, int32 ModuleIndex)
{
	ContextEmitterIndex = EmitterIndex;
	ContextModuleIndex = ModuleIndex;
	bOpenEmitterContextMenu = true;
}

void FEditorParticleSystemWidget::ClearEmitterContext()
{
	ContextEmitterIndex = -1;
	ContextModuleIndex = NoParticleModuleSelection;
}

void FEditorParticleSystemWidget::ClampSelectionToParticleSystem()
{
	if (!ParticleSystemAsset || ParticleSystemAsset->Emitters.empty())
	{
		SelectParticleSystem();
		CurrentLOD = std::max(0, CurrentLOD);
		return;
	}

	if (SelectedEmitterIndex < 0)
	{
		SelectParticleSystem();
		CurrentLOD = std::max(0, CurrentLOD);
		return;
	}

	SelectedEmitterIndex = std::clamp(
		SelectedEmitterIndex,
		0,
		static_cast<int32>(ParticleSystemAsset->Emitters.size()) - 1);

	UParticleEmitter* Emitter = ParticleSystemAsset->Emitters[SelectedEmitterIndex];
	if (!Emitter || Emitter->GetLODLevels().empty())
	{
		CurrentLOD = 0;
		SelectEmitter(SelectedEmitterIndex);
		return;
	}

	CurrentLOD = std::clamp(CurrentLOD, 0, static_cast<int32>(Emitter->GetLODLevels().size()) - 1);
	UParticleLODLevel* LODLevel = Emitter->GetLODLevel(CurrentLOD);
	if (!LODLevel)
	{
		SelectEmitter(SelectedEmitterIndex);
		return;
	}

	if (SelectedModuleIndex == RequiredParticleModuleSelection)
	{
		if (!LODLevel->GetRequiredModule())
		{
			SelectEmitter(SelectedEmitterIndex);
		}
		return;
	}

	if (SelectedModuleIndex == RendererPropertiesSelection)
	{
		if (!LODLevel->GetEffectiveRendererProperties())
		{
			SelectEmitter(SelectedEmitterIndex);
		}
		return;
	}

	if (SelectedModuleIndex < 0 || SelectedModuleIndex >= static_cast<int32>(LODLevel->Modules.size()))
	{
		SelectEmitter(SelectedEmitterIndex);
	}
}

void FEditorParticleSystemWidget::ResetPendingReorders()
{
	PendingEmitterMoveSource = -1;
	PendingEmitterMoveInsertIndex = -1;
	PendingModuleMoveEmitterIndex = -1;
	PendingModuleMoveTargetEmitterIndex = -1;
	PendingModuleMoveSource = -1;
	PendingModuleMoveInsertIndex = -1;
}

void FEditorParticleSystemWidget::ApplyPendingReorders()
{
	if (PendingModuleMoveEmitterIndex >= 0)
	{
		ReorderModule(PendingModuleMoveEmitterIndex, PendingModuleMoveSource, PendingModuleMoveTargetEmitterIndex, PendingModuleMoveInsertIndex);
	}

	if (PendingEmitterMoveSource >= 0)
	{
		ReorderEmitter(PendingEmitterMoveSource, PendingEmitterMoveInsertIndex);
	}

	ResetPendingReorders();
}

void FEditorParticleSystemWidget::ReorderEmitter(int32 SourceIndex, int32 InsertIndex)
{
	if (!ParticleSystemAsset)
	{
		return;
	}

	const int32 EmitterCount = static_cast<int32>(ParticleSystemAsset->Emitters.size());
	if (SourceIndex < 0 || SourceIndex >= EmitterCount)
	{
		return;
	}

	const int32 ClampedInsertIndex = std::clamp(InsertIndex, 0, EmitterCount);
	if (ClampedInsertIndex == SourceIndex || ClampedInsertIndex == SourceIndex + 1)
	{
		return;
	}

	CaptureUndoSnapshot("Reorder Emitter");
	SyncParticleDistributionRuntimeDataToAsset();
	int32 NewEmitterIndex = SourceIndex;
	if (!MoveArrayItemToInsertIndex(ParticleSystemAsset->Emitters, SourceIndex, ClampedInsertIndex, NewEmitterIndex))
	{
		return;
	}
	for (int32& SoloEmitterIndex : SoloEmitterIndices)
	{
		if (SoloEmitterIndex == SourceIndex)
		{
			SoloEmitterIndex = NewEmitterIndex;
		}
		else if (SourceIndex < NewEmitterIndex && SoloEmitterIndex > SourceIndex && SoloEmitterIndex <= NewEmitterIndex)
		{
			--SoloEmitterIndex;
		}
		else if (NewEmitterIndex < SourceIndex && SoloEmitterIndex >= NewEmitterIndex && SoloEmitterIndex < SourceIndex)
		{
			++SoloEmitterIndex;
		}
	}

	SelectEmitter(NewEmitterIndex);
	ClearEmitterContext();
	bDirty = true;
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::ReorderModule(int32 SourceEmitterIndex, int32 SourceModuleIndex, int32 TargetEmitterIndex, int32 InsertIndex)
{
	if (!ParticleSystemAsset ||
		SourceEmitterIndex < 0 ||
		SourceEmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()) ||
		TargetEmitterIndex < 0 ||
		TargetEmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return;
	}

	UParticleEmitter* SourceEmitter = ParticleSystemAsset->Emitters[SourceEmitterIndex];
	UParticleEmitter* TargetEmitter = ParticleSystemAsset->Emitters[TargetEmitterIndex];
	if (!SourceEmitter || !TargetEmitter || SourceModuleIndex < 0)
	{
		return;
	}

	UParticleLODLevel* SourceLODLevel = GetEmitterLODLevel(SourceEmitter);
	UParticleLODLevel* TargetLODLevel = GetEmitterLODLevel(TargetEmitter);
	if (!SourceLODLevel || !TargetLODLevel)
	{
		return;
	}

	if (SourceEmitterIndex == TargetEmitterIndex)
	{
		const int32 ModuleCount = static_cast<int32>(SourceLODLevel->Modules.size());
		if (SourceModuleIndex < 0 || SourceModuleIndex >= ModuleCount)
		{
			return;
		}

		const int32 ClampedInsertIndex = std::clamp(InsertIndex, 0, ModuleCount);
		if (ClampedInsertIndex == SourceModuleIndex || ClampedInsertIndex == SourceModuleIndex + 1)
		{
			return;
		}

		CaptureUndoSnapshot("Reorder Particle Module");
		SyncParticleDistributionRuntimeDataToAsset();
		int32 NewModuleIndex = SourceModuleIndex;
		if (!MoveArrayItemToInsertIndex(SourceLODLevel->Modules, SourceModuleIndex, ClampedInsertIndex, NewModuleIndex))
		{
			return;
		}

		SourceEmitter->CacheEmitterModuleInfo();
		SelectModule(SourceEmitterIndex, NewModuleIndex);
		ClearEmitterContext();
		bDirty = true;
		RefreshPreviewComponent(true);
		return;
	}

	if (SourceModuleIndex >= static_cast<int32>(SourceLODLevel->Modules.size()))
	{
		return;
	}

	CaptureUndoSnapshot("Reorder Particle Module");
	SyncParticleDistributionRuntimeDataToAsset();
	UParticleModule* Module = SourceLODLevel->Modules[SourceModuleIndex];
	SourceLODLevel->Modules.erase(SourceLODLevel->Modules.begin() + SourceModuleIndex);
	const int32 NewModuleIndex = std::clamp(InsertIndex, 0, static_cast<int32>(TargetLODLevel->Modules.size()));
	TargetLODLevel->Modules.insert(TargetLODLevel->Modules.begin() + NewModuleIndex, Module);

	SourceEmitter->CacheEmitterModuleInfo();
	TargetEmitter->CacheEmitterModuleInfo();
	SelectModule(TargetEmitterIndex, NewModuleIndex);
	ClearEmitterContext();
	bDirty = true;
	RefreshPreviewComponent(true);
}

void FEditorParticleSystemWidget::DrawEmitterColumn(UParticleEmitter* Emitter, int32 EmitterIndex, float ColumnHeight)
{
	constexpr float ColumnWidth = 180.0f;
	constexpr float HeaderHeight = 62.0f;
	constexpr float ModuleRowHeight = 24.0f;

	UParticleLODLevel* LODLevel = GetEmitterLODLevel(Emitter);

	const ImVec2 ColumnMin = ImGui::GetCursorScreenPos();
	const ImVec2 ColumnMax(ColumnMin.x + ColumnWidth, ColumnMin.y + ColumnHeight);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(ColumnMin, ColumnMax, ImGui::GetColorU32(ImVec4(0.075f, 0.076f, 0.088f, 1.0f)));
	DrawList->AddRect(ColumnMin, ColumnMax, ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.025f, 1.0f)));

	ImGui::PushID(EmitterIndex);

	ImGui::InvisibleButton("##EmitterHeader", ImVec2(ColumnWidth, HeaderHeight));
	const bool bHeaderHovered = ImGui::IsItemHovered();
	const bool bHeaderClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	if (bHeaderHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
	{
		SelectEmitter(EmitterIndex);
		OpenEmitterContextMenu(EmitterIndex, NoParticleModuleSelection);
	}

	const ImVec2 HeaderMin = ImGui::GetItemRectMin();
	const ImVec2 HeaderMax = ImGui::GetItemRectMax();
	const FString EmitterName = GetEmitterDisplayName(Emitter, EmitterIndex);
	const float ControlsY = HeaderMin.y + 31.0f;
	const ImVec2 ToggleMin(HeaderMin.x + 10.0f, ControlsY);
	const ImVec2 ToggleMax(ToggleMin.x + 13.0f, ToggleMin.y + 13.0f);
	const ImVec2 SoloMin(HeaderMin.x + 29.0f, ControlsY);
	const ImVec2 SoloMax(SoloMin.x + 17.0f, SoloMin.y + 13.0f);
	const bool bToggleClicked =
		bHeaderClicked &&
		ImGui::GetIO().MousePos.x >= ToggleMin.x &&
		ImGui::GetIO().MousePos.x <= ToggleMax.x &&
		ImGui::GetIO().MousePos.y >= ToggleMin.y &&
		ImGui::GetIO().MousePos.y <= ToggleMax.y;
	const bool bSoloClicked =
		bHeaderClicked &&
		ImGui::GetIO().MousePos.x >= SoloMin.x &&
		ImGui::GetIO().MousePos.x <= SoloMax.x &&
		ImGui::GetIO().MousePos.y >= SoloMin.y &&
		ImGui::GetIO().MousePos.y <= SoloMax.y;
	if (bToggleClicked && LODLevel)
	{
		CaptureUndoSnapshot(LODLevel->IsEnabled() ? "Disable Emitter" : "Enable Emitter");
		LODLevel->bEnabled = !LODLevel->bEnabled;
		if (Emitter)
		{
			Emitter->CacheEmitterModuleInfo();
		}
		if (ParticleSystemAsset)
		{
			ParticleSystemAsset->CacheEmitterModuleInfo();
		}
		bDirty = true;
		RefreshPreviewComponent(true);
	}
	else if (bSoloClicked)
	{
		ToggleEmitterSolo(EmitterIndex);
	}
	else if (bHeaderClicked)
	{
		SelectEmitter(EmitterIndex);
	}
	const bool bHeaderSelected = SelectedEmitterIndex == EmitterIndex && SelectedModuleIndex == NoParticleModuleSelection;
	bool bShowEmitterInsertMarker = false;
	float EmitterInsertMarkerX = HeaderMin.x;
	if (bHeaderSelected && ImGui::BeginDragDropSource())
	{
		const FEmitterDragPayload Payload{ EmitterIndex };
		ImGui::SetDragDropPayload(ParticleEmitterDragPayloadType, &Payload, sizeof(Payload));
		ImGui::TextUnformatted(EmitterName.c_str());
		ImGui::EndDragDropSource();
	}
	if (ImGui::BeginDragDropTarget())
	{
		const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(ParticleEmitterDragPayloadType, ParticleDragDropTargetFlags);
		if (Payload && Payload->DataSize == sizeof(FEmitterDragPayload))
		{
			const FEmitterDragPayload* DragPayload = static_cast<const FEmitterDragPayload*>(Payload->Data);
			const bool bDropAfter = ImGui::GetIO().MousePos.x > (HeaderMin.x + HeaderMax.x) * 0.5f;
			const int32 InsertIndex = EmitterIndex + (bDropAfter ? 1 : 0);
			const bool bNoMove = DragPayload->SourceEmitterIndex == InsertIndex || DragPayload->SourceEmitterIndex + 1 == InsertIndex;
			if (!bNoMove)
			{
				bShowEmitterInsertMarker = true;
				EmitterInsertMarkerX = bDropAfter ? HeaderMax.x - 1.0f : HeaderMin.x + 1.0f;
			}
			if (Payload->Delivery)
			{
				PendingEmitterMoveSource = DragPayload->SourceEmitterIndex;
				PendingEmitterMoveInsertIndex = InsertIndex;
			}
		}
		ImGui::EndDragDropTarget();
	}

	const ImVec4 AccentColors[] =
	{
		ImVec4(0.86f, 0.09f, 0.48f, 1.0f),
		ImVec4(0.14f, 0.67f, 0.92f, 1.0f),
		ImVec4(0.95f, 0.18f, 0.12f, 1.0f),
		ImVec4(0.64f, 0.30f, 0.91f, 1.0f)
	};
	constexpr int32 AccentColorCount = static_cast<int32>(sizeof(AccentColors) / sizeof(AccentColors[0]));
	const ImVec4 Accent = AccentColors[EmitterIndex % AccentColorCount];
	DrawList->AddRectFilled(HeaderMin, HeaderMax, ImGui::GetColorU32(bHeaderHovered ? ImVec4(0.18f, 0.20f, 0.24f, 1.0f) : ImVec4(0.13f, 0.14f, 0.16f, 1.0f)));
	DrawList->AddRectFilled(HeaderMin, ImVec2(HeaderMin.x + 4.0f, HeaderMax.y), ImGui::GetColorU32(Accent));
	if (bHeaderSelected)
	{
		DrawList->AddRect(HeaderMin, HeaderMax, ImGui::GetColorU32(ImVec4(0.38f, 0.58f, 0.92f, 1.0f)), 0.0f, 0, 1.5f);
	}
	if (bShowEmitterInsertMarker)
	{
		DrawList->AddLine(
			ImVec2(EmitterInsertMarkerX, HeaderMin.y),
			ImVec2(EmitterInsertMarkerX, ColumnMax.y),
			ImGui::GetColorU32(ImVec4(0.35f, 0.70f, 1.0f, 1.0f)),
			2.0f);
	}

	DrawList->AddText(ImVec2(HeaderMin.x + 10.0f, HeaderMin.y + 8.0f), ImGui::GetColorU32(ImVec4(0.88f, 0.90f, 0.94f, 1.0f)), EmitterName.c_str());

	DrawMiniEmitterRenderToggle(DrawList, ToggleMin, LODLevel ? LODLevel->IsEnabled() : true);
	DrawMiniSoloButton(DrawList, SoloMin, IsEmitterSolo(EmitterIndex));

	if (LODLevel)
	{
		const TArray<UParticleModule*>& Modules = LODLevel->GetModules();
		bool bDrewLegacyTypeData = false;
		for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(Modules.size()); ++ModuleIndex)
		{
			if (Cast<UParticleModuleTypeDataBase>(Modules[ModuleIndex]))
			{
				DrawEmitterModuleRow(Modules[ModuleIndex], EmitterIndex, ModuleIndex, false, ModuleRowHeight);
				bDrewLegacyTypeData = true;
				break;
			}
		}
		if (!bDrewLegacyTypeData)
		{
			DrawEmitterRendererRow(LODLevel, EmitterIndex, ModuleRowHeight);
		}
	}

	if (LODLevel && LODLevel->GetRequiredModule())
	{
		DrawEmitterModuleRow(LODLevel->GetRequiredModule(), EmitterIndex, RequiredParticleModuleSelection, true, ModuleRowHeight);
	}

	if (LODLevel)
	{
		const TArray<UParticleModule*>& Modules = LODLevel->GetModules();
		for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(Modules.size()); ++ModuleIndex)
		{
			if (Cast<UParticleModuleTypeDataBase>(Modules[ModuleIndex]))
			{
				continue;
			}
			DrawEmitterModuleRow(Modules[ModuleIndex], EmitterIndex, ModuleIndex, false, ModuleRowHeight);
		}
	}

	const float DrawnHeight = ImGui::GetCursorScreenPos().y - ColumnMin.y;
	if (DrawnHeight < ColumnHeight)
	{
		ImGui::Dummy(ImVec2(ColumnWidth, ColumnHeight - DrawnHeight));
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			SelectEmitter(EmitterIndex);
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			SelectEmitter(EmitterIndex);
			OpenEmitterContextMenu(EmitterIndex, NoParticleModuleSelection);
		}
		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(ParticleModuleDragPayloadType, ParticleDragDropTargetFlags);
			if (Payload && Payload->DataSize == sizeof(FModuleDragPayload) && LODLevel)
			{
				const FModuleDragPayload* DragPayload = static_cast<const FModuleDragPayload*>(Payload->Data);
				const int32 InsertIndex = static_cast<int32>(LODLevel->Modules.size());
				const bool bSameEmitter = DragPayload->SourceEmitterIndex == EmitterIndex;
				const bool bNoMove = bSameEmitter &&
					(DragPayload->SourceModuleIndex == InsertIndex || DragPayload->SourceModuleIndex + 1 == InsertIndex);
				if (!bNoMove)
				{
					const ImVec2 EmptyMin = ImGui::GetItemRectMin();
					const ImVec2 EmptyMax = ImGui::GetItemRectMax();
					DrawList->AddLine(
						ImVec2(EmptyMin.x, EmptyMin.y + 1.0f),
						ImVec2(EmptyMax.x, EmptyMin.y + 1.0f),
						ImGui::GetColorU32(ImVec4(0.35f, 0.70f, 1.0f, 1.0f)),
						2.0f);
					if (Payload->Delivery)
					{
						PendingModuleMoveEmitterIndex = DragPayload->SourceEmitterIndex;
						PendingModuleMoveTargetEmitterIndex = EmitterIndex;
						PendingModuleMoveSource = DragPayload->SourceModuleIndex;
						PendingModuleMoveInsertIndex = InsertIndex;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	ImGui::PopID();
}

void FEditorParticleSystemWidget::DrawEmitterRendererRow(UParticleLODLevel* LODLevel, int32 EmitterIndex, float RowHeight)
{
	constexpr float ColumnWidth = 180.0f;

	ImGui::PushID("RendererProperties");
	ImGui::InvisibleButton("##RendererRow", ImVec2(ColumnWidth, RowHeight));
	const bool bHovered = ImGui::IsItemHovered();
	const bool bLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	if (bHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
	{
		SelectModule(EmitterIndex, RendererPropertiesSelection);
		OpenEmitterContextMenu(EmitterIndex, RendererPropertiesSelection);
	}
	else if (bLeftClicked)
	{
		SelectModule(EmitterIndex, RendererPropertiesSelection);
	}

	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	ImVec4 RowColor = ImVec4(0.15f, 0.15f, 0.19f, 1.0f);
	if (bHovered)
	{
		RowColor.x = std::min(RowColor.x + 0.07f, 1.0f);
		RowColor.y = std::min(RowColor.y + 0.07f, 1.0f);
		RowColor.z = std::min(RowColor.z + 0.07f, 1.0f);
	}

	const bool bSelected = SelectedEmitterIndex == EmitterIndex && SelectedModuleIndex == RendererPropertiesSelection;
	DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(RowColor));
	DrawList->AddLine(ImVec2(Min.x, Max.y - 1.0f), ImVec2(Max.x, Max.y - 1.0f), ImGui::GetColorU32(ImVec4(0.03f, 0.03f, 0.035f, 1.0f)));
	if (bSelected)
	{
		DrawList->AddRect(Min, Max, ImGui::GetColorU32(ImVec4(0.38f, 0.58f, 0.92f, 1.0f)), 0.0f, 0, 1.5f);
	}

	const char* RendererName = LODLevel ? GetRenderModeLabel(LODLevel->GetEffectiveRenderMode()) : "Sprite";
	DrawList->AddText(
		ImVec2(Min.x + 10.0f, Min.y + 4.0f),
		ImGui::GetColorU32(ImVec4(0.95f, 0.96f, 0.98f, 1.0f)),
		RendererName);
	ImGui::PopID();
}

void FEditorParticleSystemWidget::DrawEmitterModuleRow(UParticleModule* Module, int32 EmitterIndex, int32 ModuleIndex, bool bRequired, float RowHeight)
{
	constexpr float ColumnWidth = 180.0f;

	ImGui::PushID(bRequired ? -1000 : ModuleIndex);
	ImGui::InvisibleButton("##ModuleRow", ImVec2(ColumnWidth, RowHeight));
	const bool bHovered = ImGui::IsItemHovered();
	const bool bLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	if (bHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
	{
		SelectModule(EmitterIndex, ModuleIndex);
		OpenEmitterContextMenu(EmitterIndex, ModuleIndex);
	}

	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const FString ModuleName = GetModuleDisplayName(Module, bRequired);
	const bool bInheritedModule = CurrentLOD > 0 && IsInheritedLODModule(Module);
	const bool bHasModuleToggle = Module && !bInheritedModule && !bRequired && !Cast<UParticleModuleTypeDataBase>(Module);
	bool bHasModuleCurves = false;
	if (Module && !bRequired && Module->GetClass())
	{
		TArray<const FProperty*> Properties;
		Module->GetClass()->GetAllProperties(Properties);
		for (const FProperty* Property : Properties)
		{
			if (Property && IsParticleDistributionProperty(Module, *Property))
			{
				bHasModuleCurves = true;
				break;
			}
		}
	}
	const ImVec2 ToggleMin(Max.x - 38.0f, Min.y + 5.0f);
	const ImVec2 ToggleMax(ToggleMin.x + 13.0f, ToggleMin.y + 13.0f);
	const ImVec2 CurveMin(Max.x - 19.0f, Min.y + 5.0f);
	const ImVec2 CurveMax(CurveMin.x + 13.0f, CurveMin.y + 13.0f);
	const ImVec2 MousePos = ImGui::GetIO().MousePos;
	const bool bToggleClicked =
		bLeftClicked &&
		bHasModuleToggle &&
		MousePos.x >= ToggleMin.x && MousePos.x <= ToggleMax.x &&
		MousePos.y >= ToggleMin.y && MousePos.y <= ToggleMax.y;
	const bool bCurveClicked =
		bLeftClicked &&
		bHasModuleCurves &&
		MousePos.x >= CurveMin.x && MousePos.x <= CurveMax.x &&
		MousePos.y >= CurveMin.y && MousePos.y <= CurveMax.y;

	if (bCurveClicked && !bInheritedModule)
	{
		SelectModule(EmitterIndex, ModuleIndex);
		OpenParticleModuleCurves(EmitterIndex, ModuleIndex);
	}
	else if (bToggleClicked && !bInheritedModule)
	{
		CaptureUndoSnapshot(Module->IsEnabled() ? "Disable Particle Module" : "Enable Particle Module");
		Module->SetEnabled(!Module->IsEnabled());

		UParticleEmitter* OwnerEmitter = nullptr;
		if (ParticleSystemAsset && EmitterIndex >= 0 && EmitterIndex < static_cast<int32>(ParticleSystemAsset->Emitters.size()))
		{
			OwnerEmitter = ParticleSystemAsset->Emitters[EmitterIndex];
		}
		if (UParticleLODLevel* LODLevel = GetEmitterLODLevel(OwnerEmitter))
		{
			LODLevel->CacheModuleLists();
		}
		if (OwnerEmitter)
		{
			OwnerEmitter->CacheEmitterModuleInfo();
		}
		if (ParticleSystemAsset)
		{
			ParticleSystemAsset->CacheEmitterModuleInfo();
		}

		bDirty = true;
		RefreshPreviewComponent(true);
	}
	else if (bLeftClicked)
	{
		SelectModule(EmitterIndex, ModuleIndex);
	}

	const bool bSelected = SelectedEmitterIndex == EmitterIndex && SelectedModuleIndex == ModuleIndex;
	bool bShowModuleInsertMarker = false;
	float ModuleInsertMarkerY = Min.y;
	if (!bInheritedModule && !bRequired && bSelected && ImGui::BeginDragDropSource())
	{
		const FModuleDragPayload Payload{ EmitterIndex, ModuleIndex };
		ImGui::SetDragDropPayload(ParticleModuleDragPayloadType, &Payload, sizeof(Payload));
		ImGui::TextUnformatted(ModuleName.c_str());
		ImGui::EndDragDropSource();
	}
	if (!bInheritedModule && ImGui::BeginDragDropTarget())
	{
		const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(ParticleModuleDragPayloadType, ParticleDragDropTargetFlags);
		if (Payload && Payload->DataSize == sizeof(FModuleDragPayload))
		{
			const FModuleDragPayload* DragPayload = static_cast<const FModuleDragPayload*>(Payload->Data);
			const bool bDropAfter = !bRequired && ImGui::GetIO().MousePos.y > (Min.y + Max.y) * 0.5f;
			const int32 InsertIndex = bRequired ? 0 : ModuleIndex + (bDropAfter ? 1 : 0);
			const bool bSameEmitter = DragPayload->SourceEmitterIndex == EmitterIndex;
			const bool bNoMove = bSameEmitter &&
				(DragPayload->SourceModuleIndex == InsertIndex || DragPayload->SourceModuleIndex + 1 == InsertIndex);
			if (!bNoMove)
			{
				bShowModuleInsertMarker = true;
				ModuleInsertMarkerY = (bRequired || bDropAfter) ? Max.y - 1.0f : Min.y + 1.0f;
				if (Payload->Delivery)
				{
					PendingModuleMoveEmitterIndex = DragPayload->SourceEmitterIndex;
					PendingModuleMoveTargetEmitterIndex = EmitterIndex;
					PendingModuleMoveSource = DragPayload->SourceModuleIndex;
					PendingModuleMoveInsertIndex = InsertIndex;
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	ImVec4 RowColor = GetModuleRowColor(Module, bRequired);
	if (Module && !Module->IsEnabled())
	{
		RowColor = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
	}
	if (bHovered)
	{
		RowColor.x = std::min(RowColor.x + 0.07f, 1.0f);
		RowColor.y = std::min(RowColor.y + 0.07f, 1.0f);
		RowColor.z = std::min(RowColor.z + 0.07f, 1.0f);
	}

	DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(RowColor));
	if (bInheritedModule)
	{
		DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(ImVec4(0.50f, 0.52f, 0.56f, 0.22f)));
	}
	DrawList->AddLine(ImVec2(Min.x, Max.y - 1.0f), ImVec2(Max.x, Max.y - 1.0f), ImGui::GetColorU32(ImVec4(0.03f, 0.03f, 0.035f, 1.0f)));
	if (bInheritedModule)
	{
		const ImU32 HatchColor = ImGui::GetColorU32(ImVec4(0.78f, 0.80f, 0.84f, 0.46f));
		for (float X = Max.x - 1.0f; X > Min.x - RowHeight; X -= 14.0f)
		{
			DrawList->AddLine(
				ImVec2(X, Min.y + 3.0f),
				ImVec2(X - RowHeight, Max.y - 3.0f),
				HatchColor,
				1.0f);
		}
	}
	if (bSelected)
	{
		DrawList->AddRect(Min, Max, ImGui::GetColorU32(ImVec4(0.38f, 0.58f, 0.92f, 1.0f)), 0.0f, 0, 1.5f);
	}
	if (bShowModuleInsertMarker)
	{
		DrawList->AddLine(
			ImVec2(Min.x, ModuleInsertMarkerY),
			ImVec2(Max.x, ModuleInsertMarkerY),
			ImGui::GetColorU32(ImVec4(0.35f, 0.70f, 1.0f, 1.0f)),
			2.0f);
	}

	DrawList->AddText(
		ImVec2(Min.x + 10.0f, Min.y + 4.0f),
		ImGui::GetColorU32(ImVec4(0.95f, 0.96f, 0.98f, 1.0f)),
		ModuleName.c_str());
	if (bHasModuleToggle)
	{
		DrawMiniCheck(DrawList, ToggleMin, Module ? Module->IsEnabled() : true);
		if (bHasModuleCurves)
		{
			DrawMiniCurveIcon(DrawList, CurveMin, true);
		}
	}

	ImGui::PopID();
}
