// Renders particle system details, module details, and reflection-backed property editors.
#include "Editor/UI/EditorParticleSystemWidgetPrivate.h"

void FEditorParticleSystemWidget::DrawDetailsPanel(const ImVec2& Size)
{
	DrawPanelHeader("Details");

	const ImVec2 BodySize(Size.x, std::max(1.0f, Size.y - PanelHeaderHeight));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
	ImGui::BeginChild(
		"##ParticleDetailsBody",
		BodySize,
		false,
		ImGuiWindowFlags_AlwaysVerticalScrollbar);
	ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 8.0f, ImGui::GetCursorPosY() + 8.0f));
	ImGui::BeginGroup();

	UParticleEmitter* SelectedEmitter = GetSelectedEmitter();
	UParticleModule* SelectedModule = GetSelectedModule();
	if (!ParticleSystemAsset)
	{
		ImGui::TextDisabled("No particle system.");
	}
	else if (SelectedEmitterIndex < 0 || !SelectedEmitter)
	{
		DrawParticleSystemDetails(ParticleSystemAsset);
	}
	else if (SelectedModuleIndex == RendererPropertiesSelection)
	{
		UParticleLODLevel* LODLevel = GetEmitterLODLevel(SelectedEmitter);
		DrawRendererPropertiesDetails(LODLevel ? LODLevel->GetEffectiveRendererProperties() : nullptr);
	}
	else if (!SelectedModule)
	{
		DrawEmitterDetails(SelectedEmitter, SelectedEmitterIndex);
	}
	else
	{
		DrawParticleModuleDetails(SelectedModule, SelectedEmitter);
	}

	ImGui::EndGroup();
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}
UParticleEmitter* FEditorParticleSystemWidget::GetSelectedEmitter() const
{
	if (!ParticleSystemAsset ||
		SelectedEmitterIndex < 0 ||
		SelectedEmitterIndex >= static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		return nullptr;
	}
	return ParticleSystemAsset->Emitters[SelectedEmitterIndex];
}

UParticleLODLevel* FEditorParticleSystemWidget::GetEmitterLODLevel(UParticleEmitter* Emitter) const
{
	if (!Emitter)
	{
		return nullptr;
	}

	UParticleLODLevel* LODLevel = Emitter->GetLODLevel(CurrentLOD);
	if (!LODLevel)
	{
		LODLevel = Emitter->GetLODLevel(0);
	}
	return LODLevel;
}

UParticleLODLevel* FEditorParticleSystemWidget::GetSelectedLODLevel() const
{
	return GetEmitterLODLevel(GetSelectedEmitter());
}

UParticleModule* FEditorParticleSystemWidget::GetSelectedModule() const
{
	UParticleLODLevel* LODLevel = GetSelectedLODLevel();
	if (!LODLevel)
	{
		return nullptr;
	}

	if (SelectedModuleIndex == RequiredParticleModuleSelection)
	{
		return LODLevel->GetRequiredModule();
	}
	if (SelectedModuleIndex >= 0 && SelectedModuleIndex < static_cast<int32>(LODLevel->Modules.size()))
	{
		return LODLevel->Modules[SelectedModuleIndex];
	}
	return nullptr;
}

void FEditorParticleSystemWidget::SyncParticleSystemLODPropertiesFromEmitters()
{
	if (!ParticleSystemAsset)
	{
		return;
	}

	const int32 LODCount = GetMaxLODCount();
	if (LODCount <= 0)
	{
		ParticleSystemAsset->LODDistances.clear();
		return;
	}

	const int32 OldDistanceCount = static_cast<int32>(ParticleSystemAsset->LODDistances.size());
	ParticleSystemAsset->LODDistances.resize(LODCount);

	for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		bool bFoundDistance = false;
		for (UParticleEmitter* Emitter : ParticleSystemAsset->Emitters)
		{
			UParticleLODLevel* LODLevel = Emitter ? Emitter->GetLODLevel(LODIndex) : nullptr;
			if (LODLevel)
			{
				ParticleSystemAsset->LODDistances[LODIndex] = LODLevel->GetDistanceThreshold();
				bFoundDistance = true;
				break;
			}
		}

		if (!bFoundDistance && LODIndex >= OldDistanceCount)
		{
			ParticleSystemAsset->LODDistances[LODIndex] = LODIndex == 0
				? 100.0f
				: ParticleSystemAsset->LODDistances[LODIndex - 1] + 1000.0f;
		}
	}
}

void FEditorParticleSystemWidget::ApplyParticleSystemLODPropertiesToEmitters()
{
	if (!ParticleSystemAsset)
	{
		return;
	}

	const int32 DistanceCount = static_cast<int32>(ParticleSystemAsset->LODDistances.size());
	for (UParticleEmitter* Emitter : ParticleSystemAsset->Emitters)
	{
		if (!Emitter)
		{
			continue;
		}

		for (int32 LODIndex = 0; LODIndex < DistanceCount; ++LODIndex)
		{
			if (UParticleLODLevel* LODLevel = Emitter->GetLODLevel(LODIndex))
			{
				LODLevel->DistanceThreshold = std::max(0.0f, ParticleSystemAsset->LODDistances[LODIndex]);
			}
		}
		Emitter->CacheEmitterModuleInfo();
	}

	ParticleSystemAsset->CacheEmitterModuleInfo();
	RefreshPreviewComponent(false);
	bDirty = true;
}

void FEditorParticleSystemWidget::DrawEmitterDetails(UParticleEmitter* Emitter, int32 EmitterIndex)
{
	if (!Emitter)
	{
		return;
	}

	ImGui::PushID(Emitter);

	const float AvailableWidth = ImGui::GetContentRegionAvail().x;
	const float LabelWidth = std::clamp(AvailableWidth * 0.38f, 180.0f, 300.0f);

	auto SyncEmitterNameBuffer = [&]()
	{
		const FString EmitterName = GetEmitterDisplayName(Emitter, EmitterIndex);
		std::snprintf(DetailEmitterNameEditBuffer, sizeof(DetailEmitterNameEditBuffer), "%s", EmitterName.c_str());
		DetailEmitterNameEditIndex = EmitterIndex;
	};

	auto WarnAndSyncEmptyEmitterName = [&]()
	{
		if (EditorEngine)
		{
			EditorEngine->GetNotificationService().Warning("Emitter name cannot be empty.");
		}
		SyncEmitterNameBuffer();
	};

	auto ApplyLiveEmitterNameBuffer = [&](bool bWarnOnEmpty)
	{
		const FString TrimmedName = TrimCopy(DetailEmitterNameEditBuffer);
		if (TrimmedName.empty())
		{
			if (bWarnOnEmpty)
			{
				WarnAndSyncEmptyEmitterName();
			}
			return;
		}

		if (Emitter->GetFName().ToString() == TrimmedName)
		{
			return;
		}

		if (!bEmitterNameEditUndoCaptured)
		{
			CaptureUndoSnapshot("Rename Emitter");
			bEmitterNameEditUndoCaptured = true;
		}
		ApplyEmitterName(EmitterIndex, TrimmedName, false, false);
	};

	if (DetailEmitterNameEditIndex != EmitterIndex)
	{
		SyncEmitterNameBuffer();
		bEmitterNameEditUndoCaptured = false;
	}

	if (ImGui::CollapsingHeader("Particle", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (BeginParticleDetailsTable("##ParticleEmitterDetailsTable", LabelWidth))
		{
			BeginParticleDetailsRow("Emitter Name", 30.0f);

			ImGui::SetNextItemWidth(std::min(220.0f, ImGui::GetContentRegionAvail().x));
			const bool bNameChanged = ImGui::InputText(
				"##EmitterName",
				DetailEmitterNameEditBuffer,
				sizeof(DetailEmitterNameEditBuffer));
			const bool bNameActive = ImGui::IsItemActive();
			const bool bNameDeactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
			if (bNameChanged)
			{
				ApplyLiveEmitterNameBuffer(false);
			}
			if (bNameDeactivatedAfterEdit)
			{
				ApplyLiveEmitterNameBuffer(true);
				SyncEmitterNameBuffer();
				bEmitterNameEditUndoCaptured = false;
			}
			else if (!bNameActive)
			{
				const FString CurrentName = GetEmitterDisplayName(Emitter, EmitterIndex);
				if (CurrentName != DetailEmitterNameEditBuffer)
				{
					SyncEmitterNameBuffer();
				}
			}

			EndParticleDetailsTable();
		}
	}

	ImGui::PopID();
}

void FEditorParticleSystemWidget::DrawRendererPropertiesDetails(UParticleRendererProperties* RendererProperties)
{
	if (!RendererProperties)
	{
		ImGui::TextDisabled("No renderer properties.");
		return;
	}

	ImGui::PushID(RendererProperties);
	ImGui::TextUnformatted(GetRenderModeLabel(RendererProperties->GetRenderMode()));
	if (RendererProperties->GetClass())
	{
		ImGui::TextDisabled("%s", RendererProperties->GetClass()->GetName());
	}
	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0.0f, 8.0f));

	TArray<const FProperty*> Properties;
	if (RendererProperties->GetClass())
	{
		RendererProperties->GetClass()->GetAllProperties(Properties);
	}

	const float AvailableWidth = ImGui::GetContentRegionAvail().x;
	const float LabelWidth = std::clamp(AvailableWidth * 0.38f, 128.0f, 190.0f);
	int32 RenderedPropertyCount = 0;
	if (BeginParticleDetailsTable("##ParticleRendererPropertiesTable", LabelWidth))
	{
		for (const FProperty* Property : Properties)
		{
			if (!Property || !Property->Name || !Property->IsEditable())
			{
				continue;
			}

			ImGui::PushID(Property->Name);
			ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.0f);
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(GetPropertyDisplayName(*Property));
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			if (DrawParticleObjectProperty(RendererProperties, *Property))
			{
				if (UParticleEmitter* Emitter = GetSelectedEmitter())
				{
					Emitter->CacheEmitterModuleInfo();
				}
				RefreshPreviewComponent(true);
			}
			ImGui::PopID();
			++RenderedPropertyCount;
		}
		EndParticleDetailsTable();
	}

	if (RenderedPropertyCount == 0)
	{
		ImGui::TextDisabled("No editable properties.");
	}
	ImGui::PopID();
}

void FEditorParticleSystemWidget::DrawParticleSystemDetails(UParticleSystem* ParticleSystem)
{
	if (!ParticleSystem)
	{
		return;
	}

	ImGui::PushID(ParticleSystem);

	const float AvailableWidth = ImGui::GetContentRegionAvail().x;
	const float LabelWidth = std::clamp(AvailableWidth * 0.38f, 180.0f, 300.0f);
	TArray<const FProperty*> Properties;
	if (ParticleSystem->GetClass())
	{
		ParticleSystem->GetClass()->GetAllProperties(Properties);
	}

	auto FindProperty = [&](const char* Name) -> const FProperty*
	{
		for (const FProperty* Property : Properties)
		{
			if (Property && Property->Name && std::strcmp(Property->Name, Name) == 0)
			{
				return Property;
			}
		}
		return nullptr;
	};

	auto DrawPropertyByName = [&](const char* Name) -> bool
	{
		const FProperty* Property = FindProperty(Name);
		if (!Property || !Property->IsEditable())
		{
			return false;
		}

		BeginParticleDetailsRow(GetPropertyDisplayName(*Property));
		ImGui::SetNextItemWidth(-1.0f);
		DrawParticleObjectProperty(ParticleSystem, *Property);
		return true;
	};

	auto DrawPropertyGroup = [&](const char* TableId, std::initializer_list<const char*> Names)
	{
		if (BeginParticleDetailsTable(TableId, LabelWidth))
		{
			for (const char* Name : Names)
			{
				DrawPropertyByName(Name);
			}
			EndParticleDetailsTable();
		}
	};

	if (ImGui::CollapsingHeader("Particle System", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawPropertyGroup(
			"##ParticleSystemDetailsTable",
			{ "UpdateTimeFPS" });
	}

	if (ImGui::CollapsingHeader("LOD", ImGuiTreeNodeFlags_DefaultOpen))
	{
		SyncParticleSystemLODPropertiesFromEmitters();

		if (BeginParticleDetailsTable("##ParticleSystemLODValuesTable", LabelWidth))
		{
			ImGui::TableNextRow(ImGuiTableRowFlags_None, 28.0f);
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			const bool bDistanceOpen = ImGui::TreeNodeEx("Distance", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted("");
			if (bDistanceOpen)
			{
				for (int32 LODIndex = 0; LODIndex < static_cast<int32>(ParticleSystem->LODDistances.size()); ++LODIndex)
				{
					FString DistanceLabel = FString("[") + std::to_string(LODIndex) + "]";
					BeginParticleDetailsRow(DistanceLabel.c_str());
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::PushID(LODIndex);
					float& Distance = ParticleSystem->LODDistances[LODIndex];
					if (ImGui::DragFloat("##LODDistance", &Distance, 10.0f, 0.0f, 0.0f))
					{
						if (!bPropertyEditUndoCaptured)
						{
							CaptureUndoSnapshot("Edit Particle System LOD");
							bPropertyEditUndoCaptured = true;
						}
						Distance = std::max(0.0f, Distance);
						ApplyParticleSystemLODPropertiesToEmitters();
						ParticleSystem->PostEditProperty("LODDistances");
					}
					if (ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsAnyItemActive())
					{
						bPropertyEditUndoCaptured = false;
					}
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
			EndParticleDetailsTable();
		}
	}

	if (!ImGui::IsAnyItemActive())
	{
		bPropertyEditUndoCaptured = false;
	}

	ImGui::PopID();
}

void FEditorParticleSystemWidget::DrawParticleModuleDetails(UParticleModule* Module, UParticleEmitter* OwnerEmitter)
{
	if (!Module)
	{
		return;
	}

	const bool bRequired = SelectedModuleIndex == RequiredParticleModuleSelection;
	const bool bInheritedModule = CurrentLOD > 0 && IsInheritedLODModule(Module);
	ImGui::PushID(Module);
	ImGui::TextUnformatted(GetModuleDisplayName(Module, bRequired).c_str());
	if (Module->GetClass())
	{
		ImGui::TextDisabled("%s", Module->GetClass()->GetName());
	}
	if (bInheritedModule)
	{
		ImGui::TextDisabled("Inherited from a higher LOD. Use Duplicate From Higher to edit.");
	}
	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0.0f, 8.0f));

	TArray<const FProperty*> Properties;
	if (Module->GetClass())
	{
		Module->GetClass()->GetAllProperties(Properties);
	}
	if (bInheritedModule)
	{
		ImGui::BeginDisabled();
	}

	const float AvailableWidth = ImGui::GetContentRegionAvail().x;
	const float LabelWidth = std::clamp(AvailableWidth * 0.38f, 128.0f, 190.0f);

	auto DrawPropertyTable = [&](const char* TableId, const char* CategoryFilter, bool bIncludeUncategorized, bool bIncludeAllCategories = false) -> int32
	{
		int32 RenderedPropertyCount = 0;
		auto IsVisibleProperty = [&](const FProperty* Property) -> bool
		{
			if (!Property || !Property->Name || !Property->IsEditable() || IsInternalParticleModuleProperty(*Property))
			{
				return false;
			}

			const bool bHasCategory = Property->Category && Property->Category[0] != '\0';
			const bool bCategoryMatch = CategoryFilter && bHasCategory && std::strcmp(Property->Category, CategoryFilter) == 0;
			const bool bUncategorizedMatch = bIncludeUncategorized && !bHasCategory;
			const bool bAllCategoryMatch = bIncludeAllCategories && !CategoryFilter;
			return bAllCategoryMatch || bCategoryMatch || bUncategorizedMatch;
		};

		auto FindPropertyByName = [&](const FString& Name) -> const FProperty*
		{
			for (const FProperty* Candidate : Properties)
			{
				if (Candidate && Candidate->Name && Name == Candidate->Name && IsVisibleProperty(Candidate))
				{
					return Candidate;
				}
			}
			return nullptr;
		};

		auto EndsWith = [](const FString& Value, const char* Suffix) -> bool
		{
			const size_t SuffixLength = std::strlen(Suffix);
			return Value.size() >= SuffixLength && Value.compare(Value.size() - SuffixLength, SuffixLength, Suffix) == 0;
		};

		auto IsColorOverLifeProperty = [&](const FProperty& ValueProperty) -> bool
		{
			return Cast<UParticleModuleColor>(Module) &&
				ValueProperty.Name &&
				std::strcmp(ValueProperty.Name, "ColorOverLife") == 0;
		};

		auto DrawColorOverLifeEditor = [](const char* Label, FVector& Value) -> bool
		{
			float Color[3] =
			{
				std::clamp(Value.X, 0.0f, 255.0f) / 255.0f,
				std::clamp(Value.Y, 0.0f, 255.0f) / 255.0f,
				std::clamp(Value.Z, 0.0f, 255.0f) / 255.0f
			};
			if (ImGui::ColorEdit3(Label, Color, ImGuiColorEditFlags_Uint8))
			{
				Value.X = std::clamp(Color[0], 0.0f, 1.0f) * 255.0f;
				Value.Y = std::clamp(Color[1], 0.0f, 1.0f) * 255.0f;
				Value.Z = std::clamp(Color[2], 0.0f, 1.0f) * 255.0f;
				return true;
			}
			return false;
		};

		auto DrawDistributionValueRow = [&](const char* RowLabel, const FProperty& ValueProperty) -> bool
		{
			bool bChanged = false;
			ImGui::TableNextRow(ImGuiTableRowFlags_None, 26.0f);
			ImGui::TableSetColumnIndex(0);
			ImGui::Indent(40.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(RowLabel);
			ImGui::Unindent(40.0f);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(std::min(140.0f, ImGui::GetContentRegionAvail().x));
			ImGui::PushID(ValueProperty.Name);
			if (IsColorOverLifeProperty(ValueProperty))
			{
				if (FVector* Value = static_cast<FVector*>(ValueProperty.GetValuePtr(Module)))
				{
					ImGui::SetNextItemWidth(std::min(180.0f, ImGui::GetContentRegionAvail().x));
					bChanged = DrawColorOverLifeEditor("##ColorOverLife", *Value);
					if (bChanged)
					{
						NotifyParticleModulePropertyChanged(Module, GetSelectedEmitter(), ValueProperty);
						RefreshPreviewComponent(false);
					}
				}
			}
			else
			{
				bChanged = DrawParticleModuleProperty(Module, ValueProperty);
			}
			ImGui::PopID();
			return bChanged;
		};

		auto DrawDistributionStoredMaxRow = [&](const FProperty& ValueProperty, const FString& Key) -> bool
		{
			bool bChanged = false;
			ImGui::TableNextRow(ImGuiTableRowFlags_None, 26.0f);
			ImGui::TableSetColumnIndex(0);
			ImGui::Indent(40.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Max");
			ImGui::Unindent(40.0f);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(std::min(140.0f, ImGui::GetContentRegionAvail().x));

			if (ValueProperty.Type == EPropertyType::Float)
			{
				float CurrentValue = ValueProperty.GetValuePtr(Module) ? *static_cast<float*>(ValueProperty.GetValuePtr(Module)) : 0.0f;
				auto Iter = ParticleDistributionFloatMaxValues.find(Key);
				if (Iter == ParticleDistributionFloatMaxValues.end())
				{
					Iter = ParticleDistributionFloatMaxValues.emplace(Key, CurrentValue).first;
				}
				bChanged = ImGui::DragFloat("##Max", &Iter->second, ValueProperty.Speed);
				if (bChanged)
				{
					bDirty = true;
					RefreshPreviewComponent(false);
				}
				return bChanged;
			}

			const char* Hint = ValueProperty.EditorHint;
			if ((!Hint || Hint[0] == '\0') && ValueProperty.ScriptStruct)
			{
				Hint = ValueProperty.ScriptStruct->GetName();
			}
			if (Hint && std::strcmp(Hint, "FVector") == 0)
			{
				FVector CurrentValue = ValueProperty.GetValuePtr(Module) ? *static_cast<FVector*>(ValueProperty.GetValuePtr(Module)) : FVector::ZeroVector;
				auto Iter = ParticleDistributionVectorMaxValues.find(Key);
				if (Iter == ParticleDistributionVectorMaxValues.end())
				{
					Iter = ParticleDistributionVectorMaxValues.emplace(Key, CurrentValue).first;
				}
				if (IsColorOverLifeProperty(ValueProperty))
				{
					bChanged = DrawColorOverLifeEditor("##Max", Iter->second);
				}
				else
				{
					bChanged = ImGui::DragFloat3("##Max", &Iter->second.X, ValueProperty.Speed);
				}
				if (bChanged)
				{
					bDirty = true;
					RefreshPreviewComponent(false);
				}
				return bChanged;
			}

			ImGui::TextDisabled("<unsupported>");
			return false;
		};

		auto GetVectorHint = [](const FProperty& ValueProperty) -> const char*
		{
			const char* Hint = ValueProperty.EditorHint;
			if ((!Hint || Hint[0] == '\0') && ValueProperty.ScriptStruct)
			{
				Hint = ValueProperty.ScriptStruct->GetName();
			}
			return Hint;
		};

		auto IsVectorProperty = [&](const FProperty& ValueProperty) -> bool
		{
			const char* Hint = GetVectorHint(ValueProperty);
			return ValueProperty.Type == EPropertyType::Struct && Hint && std::strcmp(Hint, "FVector") == 0;
		};

		auto GetInitialChannelValue = [&](const FProperty& ValueProperty, const char* ChannelName) -> float
		{
			void* ValuePtr = ValueProperty.GetValuePtr(Module);
			if (!ValuePtr)
			{
				return 0.0f;
			}
			if (ValueProperty.Type == EPropertyType::Float)
			{
				return *static_cast<float*>(ValuePtr);
			}
			if (IsVectorProperty(ValueProperty))
			{
				const FVector* Vector = static_cast<FVector*>(ValuePtr);
				if (std::strcmp(ChannelName, "Y") == 0) { return Vector->Y; }
				if (std::strcmp(ChannelName, "Z") == 0) { return Vector->Z; }
				return Vector->X;
			}
			return 0.0f;
		};

		auto DrawCurvePointRows = [&](const char* Label, const FProperty& ValueProperty) -> bool
		{
			bool bChanged = false;
			ImGui::PushID(Label);
			ImGui::PushID(ValueProperty.Name);
			const bool bVector = IsVectorProperty(ValueProperty);
			const char* Channels[] = { "X", "Y", "Z" };
			const int32 ChannelCount = bVector ? 3 : 1;
			FFloatCurve* PrimaryCurve = nullptr;
			for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
			{
				const char* ChannelName = bVector ? Channels[ChannelIndex] : "Value";
				FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, ChannelName, GetInitialChannelValue(ValueProperty, ChannelName));
				if (!PrimaryCurve)
				{
					PrimaryCurve = &Curve;
				}
			}
			if (!PrimaryCurve)
			{
				ImGui::PopID();
				ImGui::PopID();
				return false;
			}

			ImGui::TableNextRow(ImGuiTableRowFlags_None, 26.0f);
			ImGui::TableSetColumnIndex(0);
			ImGui::Indent(40.0f);
			ImGui::AlignTextToFramePadding();
			const bool bCurveOpen = ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
			ImGui::Unindent(40.0f);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d Array elements", static_cast<int32>(PrimaryCurve->Keys.size()));
			ImGui::SameLine();
			if (ImGui::SmallButton("+"))
			{
				CaptureUndoSnapshot("Add Particle Curve Key");
				const float NewTime = PrimaryCurve->Keys.empty() ? 0.0f : std::min(1.0f, PrimaryCurve->Keys.back().Time + 0.1f);
				for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
				{
					const char* ChannelName = bVector ? Channels[ChannelIndex] : "Value";
					FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, ChannelName, GetInitialChannelValue(ValueProperty, ChannelName));
					FCurveKey Key;
					Key.Time = NewTime;
					Key.Value = Curve.Keys.empty() ? GetInitialChannelValue(ValueProperty, ChannelName) : Curve.Keys.back().Value;
					Key.InterpMode = ECurveInterpMode::Cubic;
					Key.TangentMode = ECurveTangentMode::Auto;
					Curve.Keys.push_back(Key);
					Curve.SortKeys();
				}
				bChanged = true;
			}
			if (!bCurveOpen)
			{
				ImGui::PopID();
				ImGui::PopID();
				return bChanged;
			}

			for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(PrimaryCurve->Keys.size()); ++KeyIndex)
			{
				ImGui::PushID(KeyIndex);
				ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
				ImGui::TableSetColumnIndex(0);
				ImGui::Indent(60.0f);
				ImGui::AlignTextToFramePadding();
				char IndexLabel[32];
				std::snprintf(IndexLabel, sizeof(IndexLabel), "Index [%d]", KeyIndex);
				const bool bIndexOpen = ImGui::TreeNodeEx(IndexLabel, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
				ImGui::Unindent(60.0f);
				ImGui::TableSetColumnIndex(1);
				ImGui::SameLine();
				if (PrimaryCurve->Keys.size() > 1 && ImGui::SmallButton("Delete"))
				{
					CaptureUndoSnapshot("Delete Particle Curve Key");
					for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
					{
						const char* ChannelName = bVector ? Channels[ChannelIndex] : "Value";
						FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, ChannelName, GetInitialChannelValue(ValueProperty, ChannelName));
						if (KeyIndex < static_cast<int32>(Curve.Keys.size()))
						{
							Curve.Keys.erase(Curve.Keys.begin() + KeyIndex);
						}
					}
					bChanged = true;
					if (bIndexOpen)
					{
						ImGui::TreePop();
					}
					ImGui::PopID();
					break;
				}
				if (!bIndexOpen)
				{
					ImGui::PopID();
					continue;
				}

				float Time = PrimaryCurve->Keys[KeyIndex].Time;
				ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
				ImGui::TableSetColumnIndex(0);
				ImGui::Indent(80.0f);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("In Val");
				ImGui::Unindent(80.0f);
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::DragFloat("##Time", &Time, 0.01f, 0.0f, 1.0f))
				{
					if (!bPropertyEditUndoCaptured)
					{
						CaptureUndoSnapshot("Edit Particle Curve Key");
						bPropertyEditUndoCaptured = true;
					}
					for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
					{
						const char* ChannelName = bVector ? Channels[ChannelIndex] : "Value";
						FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, ChannelName, GetInitialChannelValue(ValueProperty, ChannelName));
						if (KeyIndex < static_cast<int32>(Curve.Keys.size()))
						{
							Curve.Keys[KeyIndex].Time = std::clamp(Time, 0.0f, 1.0f);
							Curve.SortKeys();
						}
					}
					bChanged = true;
				}

				ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
				ImGui::TableSetColumnIndex(0);
				ImGui::Indent(80.0f);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Out Val");
				ImGui::Unindent(80.0f);
				ImGui::TableSetColumnIndex(1);
				if (bVector)
				{
					float Values[3] = {};
					for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
					{
						FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, Channels[ChannelIndex], GetInitialChannelValue(ValueProperty, Channels[ChannelIndex]));
						Values[ChannelIndex] = KeyIndex < static_cast<int32>(Curve.Keys.size()) ? Curve.Keys[KeyIndex].Value : 0.0f;
					}
					ImGui::SetNextItemWidth(IsColorOverLifeProperty(ValueProperty) ? 180.0f : 320.0f);
					bool bEditedVector = false;
					if (IsColorOverLifeProperty(ValueProperty))
					{
						FVector ColorValue(Values[0], Values[1], Values[2]);
						bEditedVector = DrawColorOverLifeEditor("##Value", ColorValue);
						Values[0] = ColorValue.X;
						Values[1] = ColorValue.Y;
						Values[2] = ColorValue.Z;
					}
					else
					{
						bEditedVector = ImGui::DragFloat3("##Value", Values, 0.1f);
					}
					if (bEditedVector)
					{
						if (!bPropertyEditUndoCaptured)
						{
							CaptureUndoSnapshot("Edit Particle Curve Key");
							bPropertyEditUndoCaptured = true;
						}
						for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
						{
							FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, Channels[ChannelIndex], GetInitialChannelValue(ValueProperty, Channels[ChannelIndex]));
							if (KeyIndex < static_cast<int32>(Curve.Keys.size()))
							{
								Curve.Keys[KeyIndex].Value = Values[ChannelIndex];
							}
						}
						bChanged = true;
					}
				}
				else
				{
					float Value = PrimaryCurve->Keys[KeyIndex].Value;
					ImGui::SetNextItemWidth(140.0f);
					if (ImGui::DragFloat("##Value", &Value, 0.1f))
					{
						if (!bPropertyEditUndoCaptured)
						{
							CaptureUndoSnapshot("Edit Particle Curve Key");
							bPropertyEditUndoCaptured = true;
						}
						PrimaryCurve->Keys[KeyIndex].Value = Value;
						bChanged = true;
					}
				}

				ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
				ImGui::TableSetColumnIndex(0);
				ImGui::Indent(56.0f);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Interp Mode");
				ImGui::Unindent(56.0f);
				ImGui::TableSetColumnIndex(1);
				const char* InterpItems[] = { "Constant", "Linear", "Cubic" };
				int32 InterpIndex = 2;
				if (PrimaryCurve->Keys[KeyIndex].InterpMode == ECurveInterpMode::Constant)
				{
					InterpIndex = 0;
				}
				else if (PrimaryCurve->Keys[KeyIndex].InterpMode == ECurveInterpMode::Linear)
				{
					InterpIndex = 1;
				}
				ImGui::SetNextItemWidth(std::min(140.0f, ImGui::GetContentRegionAvail().x));
				if (ParticleCombo("##InterpMode", &InterpIndex, InterpItems, IM_ARRAYSIZE(InterpItems)))
				{
					CaptureUndoSnapshot("Edit Particle Curve Interp");
					const ECurveInterpMode NewInterpMode =
						InterpIndex == 0 ? ECurveInterpMode::Constant :
						InterpIndex == 1 ? ECurveInterpMode::Linear :
						ECurveInterpMode::Cubic;
					for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
					{
						const char* ChannelName = bVector ? Channels[ChannelIndex] : "Value";
						FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, ChannelName, GetInitialChannelValue(ValueProperty, ChannelName));
						if (KeyIndex < static_cast<int32>(Curve.Keys.size()))
						{
							Curve.Keys[KeyIndex].InterpMode = NewInterpMode;
						}
					}
					bChanged = true;
				}
				if (ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsAnyItemActive())
				{
					bPropertyEditUndoCaptured = false;
				}
				ImGui::TreePop();
				ImGui::PopID();
			}
			ImGui::TreePop();
			ImGui::PopID();
			ImGui::PopID();
			return bChanged;
		};

		auto DrawStoredMaxCurvePointRows = [&](const char* Label, const FProperty& ValueProperty, const FString& DistributionKey) -> bool
		{
			bool bChanged = false;
			ImGui::PushID(Label);
			ImGui::PushID(ValueProperty.Name);
			const bool bVector = IsVectorProperty(ValueProperty);
			const char* Channels[] = { "X", "Y", "Z" };
			const int32 ChannelCount = bVector ? 3 : 1;
			auto MakeStoredMaxChannelName = [&](const char* ChannelName) -> FString
			{
				return bVector ? FString("Max") + ChannelName : FString("MaxValue");
			};
			auto GetStoredInitialValue = [&](const char* ChannelName) -> float
			{
				if (ValueProperty.Type == EPropertyType::Float)
				{
					auto Iter = ParticleDistributionFloatMaxValues.find(DistributionKey);
					if (Iter == ParticleDistributionFloatMaxValues.end())
					{
						const float CurrentValue = ValueProperty.GetValuePtr(Module) ? *static_cast<float*>(ValueProperty.GetValuePtr(Module)) : 0.0f;
						Iter = ParticleDistributionFloatMaxValues.emplace(DistributionKey, CurrentValue).first;
					}
					return Iter->second;
				}
				if (bVector)
				{
					auto Iter = ParticleDistributionVectorMaxValues.find(DistributionKey);
					if (Iter == ParticleDistributionVectorMaxValues.end())
					{
						const FVector CurrentValue = ValueProperty.GetValuePtr(Module) ? *static_cast<FVector*>(ValueProperty.GetValuePtr(Module)) : FVector::ZeroVector;
						Iter = ParticleDistributionVectorMaxValues.emplace(DistributionKey, CurrentValue).first;
					}
					if (std::strcmp(ChannelName, "Y") == 0) { return Iter->second.Y; }
					if (std::strcmp(ChannelName, "Z") == 0) { return Iter->second.Z; }
					return Iter->second.X;
				}
				return GetInitialChannelValue(ValueProperty, ChannelName);
			};

			FFloatCurve* PrimaryCurve = nullptr;
			for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
			{
				const char* ChannelName = bVector ? Channels[ChannelIndex] : "Value";
				const FString StoredChannelName = MakeStoredMaxChannelName(ChannelName);
				FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, StoredChannelName.c_str(), GetStoredInitialValue(ChannelName));
				if (!PrimaryCurve)
				{
					PrimaryCurve = &Curve;
				}
			}
			if (!PrimaryCurve)
			{
				ImGui::PopID();
				ImGui::PopID();
				return false;
			}

			ImGui::TableNextRow(ImGuiTableRowFlags_None, 26.0f);
			ImGui::TableSetColumnIndex(0);
			ImGui::Indent(40.0f);
			ImGui::AlignTextToFramePadding();
			const bool bCurveOpen = ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
			ImGui::Unindent(40.0f);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d Array elements", static_cast<int32>(PrimaryCurve->Keys.size()));
			ImGui::SameLine();
			if (ImGui::SmallButton("+"))
			{
				CaptureUndoSnapshot("Add Particle Curve Key");
				const float NewTime = PrimaryCurve->Keys.empty() ? 0.0f : std::min(1.0f, PrimaryCurve->Keys.back().Time + 0.1f);
				for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
				{
					const char* ChannelName = bVector ? Channels[ChannelIndex] : "Value";
					const FString StoredChannelName = MakeStoredMaxChannelName(ChannelName);
					FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, StoredChannelName.c_str(), GetStoredInitialValue(ChannelName));
					FCurveKey Key;
					Key.Time = NewTime;
					Key.Value = Curve.Keys.empty() ? GetStoredInitialValue(ChannelName) : Curve.Keys.back().Value;
					Key.InterpMode = ECurveInterpMode::Cubic;
					Key.TangentMode = ECurveTangentMode::Auto;
					Curve.Keys.push_back(Key);
					Curve.SortKeys();
				}
				bChanged = true;
			}
			if (!bCurveOpen)
			{
				ImGui::PopID();
				ImGui::PopID();
				return bChanged;
			}

			for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(PrimaryCurve->Keys.size()); ++KeyIndex)
			{
				ImGui::PushID(KeyIndex);
				ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
				ImGui::TableSetColumnIndex(0);
				ImGui::Indent(60.0f);
				ImGui::AlignTextToFramePadding();
				char IndexLabel[32];
				std::snprintf(IndexLabel, sizeof(IndexLabel), "Index [%d]", KeyIndex);
				const bool bIndexOpen = ImGui::TreeNodeEx(IndexLabel, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
				ImGui::Unindent(60.0f);
				ImGui::TableSetColumnIndex(1);
				ImGui::SameLine();
				if (PrimaryCurve->Keys.size() > 1 && ImGui::SmallButton("Delete"))
				{
					CaptureUndoSnapshot("Delete Particle Curve Key");
					for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
					{
						const char* ChannelName = bVector ? Channels[ChannelIndex] : "Value";
						const FString StoredChannelName = MakeStoredMaxChannelName(ChannelName);
						FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, StoredChannelName.c_str(), GetStoredInitialValue(ChannelName));
						if (KeyIndex < static_cast<int32>(Curve.Keys.size()))
						{
							Curve.Keys.erase(Curve.Keys.begin() + KeyIndex);
						}
					}
					bChanged = true;
					if (bIndexOpen)
					{
						ImGui::TreePop();
					}
					ImGui::PopID();
					break;
				}
				if (!bIndexOpen)
				{
					ImGui::PopID();
					continue;
				}

				float Time = PrimaryCurve->Keys[KeyIndex].Time;
				ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
				ImGui::TableSetColumnIndex(0);
				ImGui::Indent(80.0f);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("In Val");
				ImGui::Unindent(80.0f);
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(140.0f);
				if (ImGui::DragFloat("##Time", &Time, 0.01f, 0.0f, 1.0f))
				{
					if (!bPropertyEditUndoCaptured)
					{
						CaptureUndoSnapshot("Edit Particle Curve Key");
						bPropertyEditUndoCaptured = true;
					}
					for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
					{
						const char* ChannelName = bVector ? Channels[ChannelIndex] : "Value";
						const FString StoredChannelName = MakeStoredMaxChannelName(ChannelName);
						FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, StoredChannelName.c_str(), GetStoredInitialValue(ChannelName));
						if (KeyIndex < static_cast<int32>(Curve.Keys.size()))
						{
							Curve.Keys[KeyIndex].Time = std::clamp(Time, 0.0f, 1.0f);
							Curve.SortKeys();
						}
					}
					bChanged = true;
				}

				ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
				ImGui::TableSetColumnIndex(0);
				ImGui::Indent(80.0f);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Out Val");
				ImGui::Unindent(80.0f);
				ImGui::TableSetColumnIndex(1);
				if (bVector)
				{
					float Values[3] = {};
					for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
					{
						const FString StoredChannelName = MakeStoredMaxChannelName(Channels[ChannelIndex]);
						FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, StoredChannelName.c_str(), GetStoredInitialValue(Channels[ChannelIndex]));
						Values[ChannelIndex] = KeyIndex < static_cast<int32>(Curve.Keys.size()) ? Curve.Keys[KeyIndex].Value : 0.0f;
					}
					ImGui::SetNextItemWidth(IsColorOverLifeProperty(ValueProperty) ? 180.0f : 320.0f);
					bool bEditedVector = false;
					if (IsColorOverLifeProperty(ValueProperty))
					{
						FVector ColorValue(Values[0], Values[1], Values[2]);
						bEditedVector = DrawColorOverLifeEditor("##Value", ColorValue);
						Values[0] = ColorValue.X;
						Values[1] = ColorValue.Y;
						Values[2] = ColorValue.Z;
					}
					else
					{
						bEditedVector = ImGui::DragFloat3("##Value", Values, 0.1f);
					}
					if (bEditedVector)
					{
						if (!bPropertyEditUndoCaptured)
						{
							CaptureUndoSnapshot("Edit Particle Curve Key");
							bPropertyEditUndoCaptured = true;
						}
						for (int32 ChannelIndex = 0; ChannelIndex < 3; ++ChannelIndex)
						{
							const FString StoredChannelName = MakeStoredMaxChannelName(Channels[ChannelIndex]);
							FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, StoredChannelName.c_str(), GetStoredInitialValue(Channels[ChannelIndex]));
							if (KeyIndex < static_cast<int32>(Curve.Keys.size()))
							{
								Curve.Keys[KeyIndex].Value = Values[ChannelIndex];
							}
						}
						bChanged = true;
					}
				}
				else
				{
					float Value = PrimaryCurve->Keys[KeyIndex].Value;
					ImGui::SetNextItemWidth(140.0f);
					if (ImGui::DragFloat("##Value", &Value, 0.1f))
					{
						if (!bPropertyEditUndoCaptured)
						{
							CaptureUndoSnapshot("Edit Particle Curve Key");
							bPropertyEditUndoCaptured = true;
						}
						PrimaryCurve->Keys[KeyIndex].Value = Value;
						bChanged = true;
					}
				}

				ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
				ImGui::TableSetColumnIndex(0);
				ImGui::Indent(56.0f);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Interp Mode");
				ImGui::Unindent(56.0f);
				ImGui::TableSetColumnIndex(1);
				const char* InterpItems[] = { "Constant", "Linear", "Cubic" };
				int32 InterpIndex = 2;
				if (PrimaryCurve->Keys[KeyIndex].InterpMode == ECurveInterpMode::Constant)
				{
					InterpIndex = 0;
				}
				else if (PrimaryCurve->Keys[KeyIndex].InterpMode == ECurveInterpMode::Linear)
				{
					InterpIndex = 1;
				}
				ImGui::SetNextItemWidth(std::min(140.0f, ImGui::GetContentRegionAvail().x));
				if (ParticleCombo("##InterpMode", &InterpIndex, InterpItems, IM_ARRAYSIZE(InterpItems)))
				{
					CaptureUndoSnapshot("Edit Particle Curve Interp");
					const ECurveInterpMode NewInterpMode =
						InterpIndex == 0 ? ECurveInterpMode::Constant :
						InterpIndex == 1 ? ECurveInterpMode::Linear :
						ECurveInterpMode::Cubic;
					for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
					{
						const char* ChannelName = bVector ? Channels[ChannelIndex] : "Value";
						const FString StoredChannelName = MakeStoredMaxChannelName(ChannelName);
						FFloatCurve& Curve = GetOrCreateParticleDistributionCurve(Module, ValueProperty, StoredChannelName.c_str(), GetStoredInitialValue(ChannelName));
						if (KeyIndex < static_cast<int32>(Curve.Keys.size()))
						{
							Curve.Keys[KeyIndex].InterpMode = NewInterpMode;
						}
					}
					bChanged = true;
				}
				if (ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsAnyItemActive())
				{
					bPropertyEditUndoCaptured = false;
				}
				ImGui::TreePop();
				ImGui::PopID();
			}
			ImGui::TreePop();
			ImGui::PopID();
			ImGui::PopID();
			return bChanged;
		};

		auto DrawDistributionRows = [&](const FProperty& PrimaryProperty, const FProperty* SecondaryProperty) -> bool
		{
			static const char* DistributionItems[] =
			{
				"Distribution Float Constant",
				"Distribution Float Constant Curve",
				"Distribution Float Uniform",
				"Distribution Float Uniform Curve"
			};

			bool bChanged = false;
			ImGui::PushID(PrimaryProperty.Name);
			const FString Key = MakeParticleDistributionKey(Module, PrimaryProperty);
			auto DistributionIt = ParticleDistributionKinds.find(Key);
			if (DistributionIt == ParticleDistributionKinds.end())
			{
				if (const FParticleDistributionRuntimeData* RuntimeData = Module->FindDistributionRuntimeData(PrimaryProperty.Name))
				{
					DistributionIt = ParticleDistributionKinds.emplace(Key, std::clamp(RuntimeData->Kind, 0, 3)).first;
					if (PrimaryProperty.Type == EPropertyType::Float)
					{
						ParticleDistributionFloatMaxValues[Key] = RuntimeData->StoredMaxFloat;
						if (auto CurveIt = RuntimeData->Curves.find("Value"); CurveIt != RuntimeData->Curves.end())
						{
							ParticleDistributionCurves[MakeParticleDistributionCurveKey(Module, PrimaryProperty, "Value")] = CurveIt->second;
						}
						if (SecondaryProperty)
						{
							if (auto CurveIt = RuntimeData->Curves.find("MaxValue"); CurveIt != RuntimeData->Curves.end())
							{
								ParticleDistributionCurves[MakeParticleDistributionCurveKey(Module, *SecondaryProperty, "Value")] = CurveIt->second;
							}
						}
						else if (RuntimeData->Kind == 3)
						{
							if (auto CurveIt = RuntimeData->Curves.find("MaxValue"); CurveIt != RuntimeData->Curves.end())
							{
								ParticleDistributionCurves[MakeParticleDistributionCurveKey(Module, PrimaryProperty, "MaxValue")] = CurveIt->second;
							}
						}
					}
					else if (IsVectorProperty(PrimaryProperty))
					{
						ParticleDistributionVectorMaxValues[Key] = RuntimeData->StoredMaxVector;
						const char* Channels[] = { "X", "Y", "Z" };
						for (const char* ChannelName : Channels)
						{
							if (auto CurveIt = RuntimeData->Curves.find(ChannelName); CurveIt != RuntimeData->Curves.end())
							{
								ParticleDistributionCurves[MakeParticleDistributionCurveKey(Module, PrimaryProperty, ChannelName)] = CurveIt->second;
							}
							if (SecondaryProperty)
							{
								const FString MaxChannelName = FString("Max") + ChannelName;
								if (auto CurveIt = RuntimeData->Curves.find(MaxChannelName); CurveIt != RuntimeData->Curves.end())
								{
									ParticleDistributionCurves[MakeParticleDistributionCurveKey(Module, *SecondaryProperty, ChannelName)] = CurveIt->second;
								}
							}
							else if (RuntimeData->Kind == 3)
							{
								const FString MaxChannelName = FString("Max") + ChannelName;
								if (auto CurveIt = RuntimeData->Curves.find(MaxChannelName); CurveIt != RuntimeData->Curves.end())
								{
									ParticleDistributionCurves[MakeParticleDistributionCurveKey(Module, PrimaryProperty, MaxChannelName.c_str())] = CurveIt->second;
								}
							}
						}
					}
				}
				else
				{
					DistributionIt = ParticleDistributionKinds.emplace(Key, 3).first;
				}
			}
			int32& DistributionKind = DistributionIt->second;
			DistributionKind = std::clamp(DistributionKind, 0, static_cast<int32>(IM_ARRAYSIZE(DistributionItems)) - 1);
			auto MakeDistributionGroupLabel = [&]() -> FString
			{
				FString Label = GetPropertyDisplayName(PrimaryProperty);
				if (SecondaryProperty && EndsWith(Label, " Min"))
				{
					Label = Label.substr(0, Label.size() - 4);
				}
				else if (SecondaryProperty && EndsWith(Label, "Min"))
				{
					Label = Label.substr(0, Label.size() - 3);
				}
				return TrimCopy(Label);
			};
			auto CopyPropertyValue = [&](const FProperty& SourceProperty, const FProperty& TargetProperty)
			{
				void* SourcePtr = SourceProperty.GetValuePtr(Module);
				void* TargetPtr = TargetProperty.GetValuePtr(Module);
				if (!SourcePtr || !TargetPtr || SourceProperty.Type != TargetProperty.Type)
				{
					return false;
				}

				switch (SourceProperty.Type)
				{
				case EPropertyType::Float:
					*static_cast<float*>(TargetPtr) = *static_cast<float*>(SourcePtr);
					return true;
				case EPropertyType::Struct:
					if (SourceProperty.ScriptStruct == TargetProperty.ScriptStruct)
					{
						const char* Hint = SourceProperty.EditorHint ? SourceProperty.EditorHint : "";
						if ((Hint[0] == '\0' && SourceProperty.ScriptStruct && std::strcmp(SourceProperty.ScriptStruct->GetName(), "FVector") == 0) ||
							std::strcmp(Hint, "FVector") == 0)
						{
							*static_cast<FVector*>(TargetPtr) = *static_cast<FVector*>(SourcePtr);
							return true;
						}
					}
					break;
				default:
					break;
				}
				return false;
			};
			auto SyncConstantMaxToMin = [&]()
			{
				if (SecondaryProperty && CopyPropertyValue(PrimaryProperty, *SecondaryProperty))
				{
					NotifyParticleModulePropertyChanged(Module, GetSelectedEmitter(), *SecondaryProperty);
					RefreshPreviewComponent(false);
					bDirty = true;
				}
			};

			ImGui::TableNextRow(ImGuiTableRowFlags_None, 28.0f);
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			const FString GroupLabel = MakeDistributionGroupLabel();
			const bool bPropertyOpen = ImGui::TreeNodeEx(GroupLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted("");

			if (bPropertyOpen)
			{
				ImGui::TableNextRow(ImGuiTableRowFlags_None, 28.0f);
				ImGui::TableSetColumnIndex(0);
				ImGui::Indent(20.0f);
				ImGui::AlignTextToFramePadding();
				ImGui::TreeNodeEx("Distribution", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
				ImGui::Unindent(20.0f);
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(std::min(250.0f, ImGui::GetContentRegionAvail().x));
				const int32 PreviousDistributionKind = DistributionKind;
				if (ParticleCombo("##DistributionKind", &DistributionKind, DistributionItems, IM_ARRAYSIZE(DistributionItems)))
				{
					const int32 NewDistributionKind = DistributionKind;
					DistributionKind = PreviousDistributionKind;
					CaptureUndoSnapshot("Edit Particle Distribution");
					DistributionKind = NewDistributionKind;
					if (DistributionKind == 0 || DistributionKind == 1)
					{
						SyncConstantMaxToMin();
					}
					bChanged = true;
				}

				const bool bUniform = DistributionKind == 2 || DistributionKind == 3;
				const bool bCurve = DistributionKind == 1 || DistributionKind == 3;
				if (bCurve)
				{
					if (bUniform && SecondaryProperty)
					{
						bChanged |= DrawCurvePointRows("Min Curve", PrimaryProperty);
						bChanged |= DrawCurvePointRows("Max Curve", *SecondaryProperty);
					}
					else if (bUniform)
					{
						bChanged |= DrawCurvePointRows("Min Curve", PrimaryProperty);
						bChanged |= DrawStoredMaxCurvePointRows("Max Curve", PrimaryProperty, Key);
					}
					else
					{
						bChanged |= DrawCurvePointRows("Constant Curve", PrimaryProperty);
					}
				}
				else if (bUniform)
				{
					bChanged |= DrawDistributionValueRow("Min", PrimaryProperty);
					if (SecondaryProperty)
					{
						bChanged |= DrawDistributionValueRow("Max", *SecondaryProperty);
					}
					else
					{
						bChanged |= DrawDistributionStoredMaxRow(PrimaryProperty, Key);
					}
				}
				else
				{
					bChanged |= DrawDistributionValueRow("Constant", PrimaryProperty);
					if (bChanged)
					{
						SyncConstantMaxToMin();
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
			return bChanged;
		};

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 3.0f));
		if (ImGui::BeginTable(TableId, 2, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, LabelWidth);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			for (const FProperty* Property : Properties)
			{
				if (!IsVisibleProperty(Property))
				{
					continue;
				}

				if (IsParticleDistributionProperty(Module, *Property))
				{
					const FString PropertyName = Property->Name;
					if (EndsWith(PropertyName, "Max"))
					{
						const FString MinName = PropertyName.substr(0, PropertyName.size() - 3) + "Min";
						if (const FProperty* MinProperty = FindPropertyByName(MinName))
						{
							if (IsParticleDistributionProperty(Module, *MinProperty) && MinProperty->Type == Property->Type)
							{
								continue;
							}
						}
					}

					const FProperty* SecondaryProperty = nullptr;
					if (EndsWith(PropertyName, "Min"))
					{
						const FString MaxName = PropertyName.substr(0, PropertyName.size() - 3) + "Max";
						SecondaryProperty = FindPropertyByName(MaxName);
						if (SecondaryProperty && (!IsParticleDistributionProperty(Module, *SecondaryProperty) || SecondaryProperty->Type != Property->Type))
						{
							SecondaryProperty = nullptr;
						}
					}

					if (DrawDistributionRows(*Property, SecondaryProperty))
					{
						NotifyParticleModulePropertyChanged(Module, OwnerEmitter, *Property);
						SyncParticleDistributionRuntimeDataToAsset();
						RefreshPreviewComponent(true);
					}
					RenderedPropertyCount += SecondaryProperty ? 2 : 1;
					continue;
				}

				ImGui::PushID(Property->Name);
				ImGui::TableNextRow(ImGuiTableRowFlags_None, 30.0f);
				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(GetPropertyDisplayName(*Property));
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);
				DrawParticleModuleProperty(Module, *Property);
				ImGui::PopID();
				++RenderedPropertyCount;
			}
			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
		return RenderedPropertyCount;
	};

	int32 RenderedPropertyCount = 0;
	if (Cast<UParticleModuleRequired>(Module))
	{
		if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen))
		{
			RenderedPropertyCount += DrawPropertyTable("##ParticleRequiredEmitterTable", "Emitter", false);
		}
		if (ImGui::CollapsingHeader("SubUV", ImGuiTreeNodeFlags_DefaultOpen))
		{
			RenderedPropertyCount += DrawPropertyTable("##ParticleRequiredSubUVTable", "SubUV", false);
		}
		if (ImGui::CollapsingHeader("Required", ImGuiTreeNodeFlags_DefaultOpen))
		{
			RenderedPropertyCount += DrawPropertyTable("##ParticleRequiredGeneralTable", nullptr, true);
		}
	}
	else
	{
		RenderedPropertyCount = DrawPropertyTable("##ParticleModuleDetailsTable", nullptr, true, true);
	}

	if (RenderedPropertyCount == 0)
	{
		ImGui::TextDisabled("No editable properties.");
	}

	(void)OwnerEmitter;
	if (bInheritedModule)
	{
		ImGui::EndDisabled();
	}
	ImGui::PopID();
}

bool FEditorParticleSystemWidget::DrawParticleObjectProperty(UObject* Object, const FProperty& Property)
{
	if (!Object || !Property.Name)
	{
		return false;
	}

	void* ValuePtr = Property.GetValuePtr(Object);
	const FString Label = FString("##") + Property.Name;
	const bool bChanged = DrawParticlePropertyValue(Property, ValuePtr, Object, Label.c_str());
	if (ImGui::IsItemActivated() && !bPropertyEditUndoCaptured)
	{
		CaptureUndoSnapshot("Edit Particle System");
		bPropertyEditUndoCaptured = true;
	}
	if (bChanged)
	{
		if (!bPropertyEditUndoCaptured)
		{
			CaptureUndoSnapshot("Edit Particle System");
			bPropertyEditUndoCaptured = true;
		}
		Object->PostEditProperty(Property.Name);
		if (ParticleSystemAsset)
		{
			ParticleSystemAsset->CacheEmitterModuleInfo();
		}
		bDirty = true;
		RefreshPreviewComponent(false);
	}
	if (ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsAnyItemActive())
	{
		bPropertyEditUndoCaptured = false;
	}
	return bChanged;
}

bool FEditorParticleSystemWidget::DrawParticleModuleProperty(UParticleModule* Module, const FProperty& Property)
{
	if (!Module || !Property.Name)
	{
		return false;
	}

	void* ValuePtr = Property.GetValuePtr(Module);
	const FString Label = FString("##") + Property.Name;
	const bool bChanged = DrawParticlePropertyValue(Property, ValuePtr, Module, Label.c_str());
	if (ImGui::IsItemActivated() && !bPropertyEditUndoCaptured)
	{
		CaptureUndoSnapshot("Edit Particle Module");
		bPropertyEditUndoCaptured = true;
	}
	if (bChanged)
	{
		if (!bPropertyEditUndoCaptured)
		{
			CaptureUndoSnapshot("Edit Particle Module");
			bPropertyEditUndoCaptured = true;
		}
		NotifyParticleModulePropertyChanged(Module, GetSelectedEmitter(), Property);
		RefreshPreviewComponent(false);
	}
	if (ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsAnyItemActive())
	{
		bPropertyEditUndoCaptured = false;
	}
	return bChanged;
}

bool FEditorParticleSystemWidget::IsParticleDistributionProperty(UParticleModule* Module, const FProperty& Property) const
{
	if (!Module || !Property.IsEditable() || IsInternalParticleModuleProperty(Property))
	{
		return false;
	}

	if (Cast<UParticleModuleRequired>(Module))
	{
		return false;
	}

	if (Property.Type == EPropertyType::Float)
	{
		return true;
	}

	if (Property.Type != EPropertyType::Struct)
	{
		return false;
	}

	const char* Hint = Property.EditorHint;
	if ((!Hint || Hint[0] == '\0') && Property.ScriptStruct)
	{
		Hint = Property.ScriptStruct->GetName();
	}
	return Hint && std::strcmp(Hint, "FVector") == 0;
}

bool FEditorParticleSystemWidget::IsEmitterTimeDistributionProperty(UParticleModule* Module, const FProperty& Property) const
{
	if (!Module || !Property.Name)
	{
		return false;
	}

	const char* Name = Property.Name;
	return std::strcmp(Name, "Rate") == 0 ||
		std::strcmp(Name, "LifetimeMin") == 0 ||
		std::strcmp(Name, "StartLocationMin") == 0 ||
		std::strcmp(Name, "SphereRadius") == 0 ||
		std::strcmp(Name, "BoxExtents") == 0 ||
		std::strcmp(Name, "ConeHeight") == 0 ||
		std::strcmp(Name, "ConeHalfAngle") == 0 ||
		std::strcmp(Name, "StartVelocityMin") == 0 ||
		std::strcmp(Name, "StartRotationRateMin") == 0 ||
		std::strcmp(Name, "RotRateMin") == 0;
}

FString FEditorParticleSystemWidget::MakeParticleDistributionKey(UParticleModule* Module, const FProperty& Property) const
{
	return MakeParticleModuleCurveKey(Module) + "::" + (Property.Name ? Property.Name : "");
}

FString FEditorParticleSystemWidget::MakeParticleDistributionCurveKey(UParticleModule* Module, const FProperty& Property, const char* ChannelName) const
{
	return MakeParticleDistributionKey(Module, Property) + "::Curve::" + (ChannelName ? ChannelName : "");
}

FString FEditorParticleSystemWidget::MakeParticleModuleCurveKey(UParticleModule* Module) const
{
	const std::uintptr_t ModuleKey = reinterpret_cast<std::uintptr_t>(Module);
	return "ModulePtr:" + std::to_string(ModuleKey);
}

FFloatCurve& FEditorParticleSystemWidget::GetOrCreateParticleDistributionCurve(UParticleModule* Module, const FProperty& Property, const char* ChannelName, float InitialValue)
{
	const FString Key = MakeParticleDistributionCurveKey(Module, Property, ChannelName);
	FFloatCurve& Curve = ParticleDistributionCurves[Key];
	if (Curve.Keys.empty())
	{
		FCurveKey StartKey;
		StartKey.Time = 0.0f;
		StartKey.Value = InitialValue;
		StartKey.InterpMode = ECurveInterpMode::Cubic;
		StartKey.TangentMode = ECurveTangentMode::Auto;
		Curve.Keys.push_back(StartKey);

		FCurveKey EndKey = StartKey;
		EndKey.Time = 1.0f;
		Curve.Keys.push_back(EndKey);
	}
	return Curve;
}

void FEditorParticleSystemWidget::SyncParticleDistributionRuntimeDataToAsset()
{
	if (!ParticleSystemAsset)
	{
		return;
	}

	auto IsVectorProperty = [](const FProperty& Property) -> bool
	{
		const char* Hint = Property.EditorHint;
		if ((!Hint || Hint[0] == '\0') && Property.ScriptStruct)
		{
			Hint = Property.ScriptStruct->GetName();
		}
		return Property.Type == EPropertyType::Struct && Hint && std::strcmp(Hint, "FVector") == 0;
	};
	auto EndsWithLocal = [](const FString& Text, const char* Suffix) -> bool
	{
		const size_t SuffixLength = std::strlen(Suffix);
		return Text.size() >= SuffixLength && Text.compare(Text.size() - SuffixLength, SuffixLength, Suffix) == 0;
	};
	auto FindPropertyByName = [](const TArray<const FProperty*>& Properties, const FString& PropertyName) -> const FProperty*
	{
		for (const FProperty* Property : Properties)
		{
			if (Property && Property->Name && PropertyName == Property->Name)
			{
				return Property;
			}
		}
		return nullptr;
	};

	for (UParticleEmitter* Emitter : ParticleSystemAsset->Emitters)
	{
		if (!Emitter)
		{
			continue;
		}
		for (UParticleLODLevel* LODLevel : Emitter->GetLODLevels())
		{
			if (!LODLevel)
			{
				continue;
			}

			TArray<UParticleModule*> Modules = LODLevel->GetModules();
			if (LODLevel->GetRequiredModule())
			{
				Modules.push_back(LODLevel->GetRequiredModule());
			}

			for (UParticleModule* Module : Modules)
			{
				if (!Module || !Module->GetClass())
				{
					continue;
				}

				TArray<const FProperty*> Properties;
				Module->GetClass()->GetAllProperties(Properties);
				for (const FProperty* Property : Properties)
				{
					if (!Property || !Property->Name || !IsParticleDistributionProperty(Module, *Property))
					{
						continue;
					}

					const FString PropertyName = Property->Name;
					if (EndsWithLocal(PropertyName, "Max"))
					{
						const FString MinName = PropertyName.substr(0, PropertyName.size() - 3) + "Min";
						if (const FProperty* MinProperty = FindPropertyByName(Properties, MinName))
						{
							if (IsParticleDistributionProperty(Module, *MinProperty) && MinProperty->Type == Property->Type)
							{
								continue;
							}
						}
					}

					const FProperty* SecondaryProperty = nullptr;
					if (EndsWithLocal(PropertyName, "Min"))
					{
						const FString MaxName = PropertyName.substr(0, PropertyName.size() - 3) + "Max";
						SecondaryProperty = FindPropertyByName(Properties, MaxName);
						if (SecondaryProperty && (!IsParticleDistributionProperty(Module, *SecondaryProperty) || SecondaryProperty->Type != Property->Type))
						{
							SecondaryProperty = nullptr;
						}
					}

					const FString DistributionKey = MakeParticleDistributionKey(Module, *Property);
					auto KindIt = ParticleDistributionKinds.find(DistributionKey);
					if (KindIt == ParticleDistributionKinds.end())
					{
						continue;
					}

					FParticleDistributionRuntimeData Data;
					Data.Kind = std::clamp(KindIt->second, 0, 3);
					Data.bVector = IsVectorProperty(*Property);
					if (Property->Type == EPropertyType::Float)
					{
						if (SecondaryProperty)
						{
							if (void* ValuePtr = SecondaryProperty->GetValuePtr(Module))
							{
								Data.StoredMaxFloat = *static_cast<float*>(ValuePtr);
							}
						}
						else if (auto StoredIt = ParticleDistributionFloatMaxValues.find(DistributionKey); StoredIt != ParticleDistributionFloatMaxValues.end())
						{
							Data.StoredMaxFloat = StoredIt->second;
						}
					}
					else if (Data.bVector)
					{
						if (SecondaryProperty)
						{
							if (void* ValuePtr = SecondaryProperty->GetValuePtr(Module))
							{
								Data.StoredMaxVector = *static_cast<FVector*>(ValuePtr);
							}
						}
						else if (auto StoredIt = ParticleDistributionVectorMaxValues.find(DistributionKey); StoredIt != ParticleDistributionVectorMaxValues.end())
						{
							Data.StoredMaxVector = StoredIt->second;
						}
					}

					if (Data.Kind == 1 || Data.Kind == 3)
					{
						const char* Channels[] = { "X", "Y", "Z" };
						const int32 ChannelCount = Data.bVector ? 3 : 1;
						for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
						{
							const char* ChannelName = Data.bVector ? Channels[ChannelIndex] : "Value";
							if (auto CurveIt = ParticleDistributionCurves.find(MakeParticleDistributionCurveKey(Module, *Property, ChannelName)); CurveIt != ParticleDistributionCurves.end())
							{
								Data.Curves[ChannelName] = CurveIt->second;
							}
							if (Data.Kind == 3 && SecondaryProperty)
							{
								const FString MaxChannelName = Data.bVector ? FString("Max") + ChannelName : FString("MaxValue");
								if (auto CurveIt = ParticleDistributionCurves.find(MakeParticleDistributionCurveKey(Module, *SecondaryProperty, ChannelName)); CurveIt != ParticleDistributionCurves.end())
								{
									Data.Curves[MaxChannelName] = CurveIt->second;
								}
							}
							else if (Data.Kind == 3)
							{
								const FString MaxChannelName = Data.bVector ? FString("Max") + ChannelName : FString("MaxValue");
								if (auto CurveIt = ParticleDistributionCurves.find(MakeParticleDistributionCurveKey(Module, *Property, MaxChannelName.c_str())); CurveIt != ParticleDistributionCurves.end())
								{
									Data.Curves[MaxChannelName] = CurveIt->second;
								}
							}
						}
					}

					Module->SetDistributionRuntimeData(PropertyName, Data);
				}
			}
		}
	}
}

void FEditorParticleSystemWidget::OpenParticleModuleCurves(int32 EmitterIndex, int32 ModuleIndex)
{
	ActiveParticleCurveEmitterIndex = EmitterIndex;
	ActiveParticleCurveModuleIndex = ModuleIndex;
	ActiveParticleCurveModuleKey = MakeParticleModuleCurveKey(GetSelectedModule());
	ActiveParticleCurveChannelKey.clear();
	ActiveParticleCurveKeyIndex = -1;
	DragParticleCurveChannelKey.clear();
	DragParticleCurveKeyIndex = -1;
	ParticleCurveViewModuleKey = ActiveParticleCurveModuleKey;
	ParticleCurveViewMinTime = 0.0f;
	ParticleCurveViewMaxTime = 1.0f;
	bParticleCurveViewInitialized = false;
	bParticleCurveViewUserAdjusted = false;
}

bool FEditorParticleSystemWidget::DrawParticlePropertyValue(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget, const char* Label)
{
	if (!ValuePtr)
	{
		return false;
	}

	switch (Property.Type)
	{
	case EPropertyType::Bool:
		return ImGui::Checkbox(Label, static_cast<bool*>(ValuePtr));
	case EPropertyType::Int:
	{
		int32* Value = static_cast<int32*>(ValuePtr);
		const bool bChanged = ImGui::DragInt(Label, Value, Property.Speed);
		if (bChanged)
		{
			if (Property.Min != 0.0f)
			{
				*Value = std::max(*Value, static_cast<int32>(Property.Min));
			}
			if (Property.Max != 0.0f)
			{
				*Value = std::min(*Value, static_cast<int32>(Property.Max));
			}
		}
		return bChanged;
	}
	case EPropertyType::Float:
	{
		float* Value = static_cast<float*>(ValuePtr);
		const bool bChanged = ImGui::DragFloat(Label, Value, Property.Speed);
		if (bChanged)
		{
			if (Property.Min != 0.0f)
			{
				*Value = std::max(*Value, Property.Min);
			}
			if (Property.Max != 0.0f)
			{
				*Value = std::min(*Value, Property.Max);
			}
		}
		return bChanged;
	}
	case EPropertyType::Struct:
		return DrawParticleStructPropertyValue(Property, ValuePtr, NotifyTarget, Label);
	case EPropertyType::String:
	{
		FString* Value = static_cast<FString*>(ValuePtr);
		char Buffer[512];
		strncpy_s(Buffer, sizeof(Buffer), Value->c_str(), _TRUNCATE);
		if (ImGui::InputText(Label, Buffer, sizeof(Buffer)))
		{
			*Value = Buffer;
			return true;
		}
		return false;
	}
	case EPropertyType::Name:
	{
		FName* Value = static_cast<FName*>(ValuePtr);
		FString Current = Value->ToString();
		const char* DisplayName = GetPropertyDisplayName(Property);
		const bool bSubUVProperty =
			(Property.Name && std::strcmp(Property.Name, "SubUVName") == 0) ||
			(DisplayName && std::strcmp(DisplayName, "SubUV") == 0);
		const TArray<FString>* Names = (bSubUVProperty && EditorEngine)
			? &EditorEngine->GetAssetService().GetSubUVNames()
			: nullptr;

		if (Names && !Names->empty())
		{
			bool bChanged = false;
			const char* Preview = Current.empty() || Current == FName::None.ToString() ? "<None>" : Current.c_str();
			if (BeginParticleCombo(Label, Preview))
			{
				const bool bNoneSelected = Current.empty() || Current == FName::None.ToString();
				if (ImGui::Selectable("<None>", bNoneSelected))
				{
					*Value = FName::None;
					bChanged = true;
				}
				for (const FString& Name : *Names)
				{
					const bool bSelected = Current == Name;
					if (ImGui::Selectable(Name.c_str(), bSelected))
					{
						*Value = FName(Name);
						bChanged = true;
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				EndParticleCombo();
			}
			return bChanged;
		}

		char Buffer[256];
		strncpy_s(Buffer, sizeof(Buffer), Current.c_str(), _TRUNCATE);
		if (ImGui::InputText(Label, Buffer, sizeof(Buffer)))
		{
			*Value = FName(Buffer);
			return true;
		}
		return false;
	}
	case EPropertyType::Enum:
	{
		if (!Property.EnumMeta || !Property.EnumMeta->Values || Property.EnumMeta->Count == 0)
		{
			return false;
		}

		int64 CurrentValue = 0;
		switch (Property.EnumMeta->Size)
		{
		case 1: CurrentValue = static_cast<int64>(*static_cast<uint8*>(ValuePtr)); break;
		case 2: CurrentValue = static_cast<int64>(*static_cast<uint16*>(ValuePtr)); break;
		case 4: CurrentValue = static_cast<int64>(*static_cast<int32*>(ValuePtr)); break;
		case 8: CurrentValue = static_cast<int64>(*static_cast<int64*>(ValuePtr)); break;
		default: break;
		}

		int32 CurrentIndex = 0;
		for (uint32 Index = 0; Index < Property.EnumMeta->Count; ++Index)
		{
			if (Property.EnumMeta->Values[Index].Value == CurrentValue)
			{
				CurrentIndex = static_cast<int32>(Index);
				break;
			}
		}

		const auto ComboGetter = [](void* Data, int Index) -> const char*
		{
			const UEnum* EnumMeta = static_cast<const UEnum*>(Data);
			if (!EnumMeta || Index < 0 || static_cast<uint32>(Index) >= EnumMeta->Count)
			{
				return "";
			}
			const FEnumValue& ValueMeta = EnumMeta->Values[Index];
			return (ValueMeta.DisplayName && ValueMeta.DisplayName[0] != '\0') ? ValueMeta.DisplayName : ValueMeta.Name;
		};

		if (ParticleCombo(Label, &CurrentIndex, ComboGetter, const_cast<UEnum*>(Property.EnumMeta), static_cast<int>(Property.EnumMeta->Count)))
		{
			const int64 NewValue = Property.EnumMeta->Values[CurrentIndex].Value;
			switch (Property.EnumMeta->Size)
			{
			case 1: *static_cast<uint8*>(ValuePtr) = static_cast<uint8>(NewValue); break;
			case 2: *static_cast<uint16*>(ValuePtr) = static_cast<uint16>(NewValue); break;
			case 4: *static_cast<int32*>(ValuePtr) = static_cast<int32>(NewValue); break;
			case 8: *static_cast<int64*>(ValuePtr) = static_cast<int64>(NewValue); break;
			default: break;
			}
			return true;
		}
		return false;
	}
	case EPropertyType::ObjectPtr:
	{
		if (!Property.ObjectPtrOps)
		{
			return false;
		}

		UObject* CurrentObject = Property.ObjectPtrOps->GetObject(ValuePtr);
		const bool bMaterialAsset =
			Property.ReferenceKind == EObjectReferenceKind::Asset &&
			Property.ObjectClass &&
			Property.ObjectClass->IsChildOf(UMaterialInterface::StaticClass());
		const bool bStaticMeshAsset =
			Property.ReferenceKind == EObjectReferenceKind::Asset &&
			Property.ObjectClass &&
			Property.ObjectClass->IsChildOf(UStaticMesh::StaticClass());
		if (bMaterialAsset && EditorEngine)
		{
			FEditorAssetService& AssetService = EditorEngine->GetAssetService();
			const TArray<FString>& MaterialNames = AssetService.GetMaterialInterfaceNames();
			UMaterialInterface* CurrentMaterial = Cast<UMaterialInterface>(CurrentObject);
			const FString CurrentIdentifier = CurrentMaterial
				? (CurrentMaterial->GetFilePath().empty() ? CurrentMaterial->GetName() : FPaths::Normalize(CurrentMaterial->GetFilePath()))
				: FString();
			const FString CurrentLabel = CurrentIdentifier.empty() ? FString("None") : CurrentIdentifier;
			bool bChanged = false;

			if (BeginParticleCombo(Label, CurrentLabel.c_str()))
			{
				if (ImGui::Selectable("None", CurrentMaterial == nullptr))
				{
					Property.ObjectPtrOps->SetObject(ValuePtr, nullptr);
					bChanged = true;
				}

				for (int32 MaterialIndex = 0; MaterialIndex < static_cast<int32>(MaterialNames.size()); ++MaterialIndex)
				{
					ImGui::PushID(MaterialIndex);
					const FString& MaterialLabel = MaterialNames[MaterialIndex].empty()
						? FString("<Unnamed Material>")
						: MaterialNames[MaterialIndex];
					const bool bSelected = CurrentIdentifier == MaterialLabel;
					if (ImGui::Selectable(MaterialLabel.c_str(), bSelected))
					{
						if (UMaterialInterface* Candidate = AssetService.ResolveMaterialInterfaceByIndex(MaterialIndex))
						{
							Property.ObjectPtrOps->SetObject(ValuePtr, Candidate);
							bChanged = true;
						}
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}
				EndParticleCombo();
			}
			return bChanged;
		}

		if (bStaticMeshAsset && EditorEngine)
		{
			FEditorAssetService& AssetService = EditorEngine->GetAssetService();
			const TArray<FString>& StaticMeshPaths = AssetService.GetStaticMeshAssetPaths();
			UStaticMesh* CurrentMesh = Cast<UStaticMesh>(CurrentObject);
			UParticleMeshRendererProperties* MeshRenderer = Cast<UParticleMeshRendererProperties>(NotifyTarget);
			const FString CurrentIdentifier = CurrentMesh
				? FPaths::Normalize(CurrentMesh->GetAssetPathFileName())
				: FString();
			const FString CurrentLabel = CurrentIdentifier.empty() ? FString("None") : CurrentIdentifier;
			bool bChanged = false;
			auto ResolveDefaultMeshMaterial = [](UStaticMesh* Mesh) -> UMaterialInterface*
			{
				if (!Mesh)
				{
					return nullptr;
				}

				const TArray<FStaticMeshSection>& Sections = Mesh->GetSections();
				const TArray<FStaticMeshMaterialSlot>& Slots = Mesh->GetMaterialSlots();
				if (!Sections.empty() && !Slots.empty())
				{
					const int32 SlotIndex = Sections[0].MaterialSlotIndex;
					if (SlotIndex >= 0 && SlotIndex < static_cast<int32>(Slots.size()) && Slots[SlotIndex].Material)
					{
						return Slots[SlotIndex].Material;
					}
				}

				for (const FStaticMeshMaterialSlot& Slot : Slots)
				{
					if (Slot.Material)
					{
						return Slot.Material;
					}
				}
				return nullptr;
			};

			if (BeginParticleCombo(Label, CurrentLabel.c_str()))
			{
				if (ImGui::Selectable("None", CurrentMesh == nullptr))
				{
					Property.ObjectPtrOps->SetObject(ValuePtr, nullptr);
					if (Property.Name && std::strcmp(Property.Name, "Mesh") == 0)
					{
						if (MeshRenderer)
						{
							MeshRenderer->SetOverrideMaterial(false, nullptr);
						}
					}
					bChanged = true;
				}

				for (int32 MeshIndex = 0; MeshIndex < static_cast<int32>(StaticMeshPaths.size()); ++MeshIndex)
				{
					ImGui::PushID(MeshIndex);
					const FString& MeshPath = StaticMeshPaths[MeshIndex];
					const FString NormalizedPath = FPaths::Normalize(MeshPath);
					const bool bSelected = CurrentIdentifier == NormalizedPath;
					if (ImGui::Selectable(MeshPath.c_str(), bSelected))
					{
						if (UStaticMesh* Candidate = AssetService.LoadStaticMesh(MeshPath))
						{
							Property.ObjectPtrOps->SetObject(ValuePtr, Candidate);
							if (Property.Name && std::strcmp(Property.Name, "Mesh") == 0)
							{
								UMaterialInterface* DefaultMaterial = ResolveDefaultMeshMaterial(Candidate);
								if (MeshRenderer)
								{
									MeshRenderer->SetOverrideMaterial(false, DefaultMaterial);
								}
							}
							bChanged = true;
						}
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}
				EndParticleCombo();
			}
			return bChanged;
		}

		ImGui::TextDisabled("%s <unsupported object>", Label);
		return false;
	}
	case EPropertyType::Array:
	{
		const int32 ElementCount = Property.ArrayOps ? Property.ArrayOps->Num(ValuePtr) : 0;
		ImGui::Text("%d Array elements", ElementCount);
		return false;
	}
	default:
		ImGui::TextDisabled("%s <unsupported>", Label);
		break;
	}

	return false;
}

bool FEditorParticleSystemWidget::DrawParticleStructPropertyValue(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget, const char* Label)
{
	if (!ValuePtr)
	{
		return false;
	}

	const char* Hint = Property.EditorHint;
	if ((!Hint || Hint[0] == '\0') && Property.ScriptStruct)
	{
		Hint = Property.ScriptStruct->GetName();
	}

	if (Hint && std::strcmp(Hint, "FVector") == 0)
	{
		return ImGui::DragFloat3(Label, static_cast<float*>(ValuePtr), Property.Speed);
	}
	if (Hint && std::strcmp(Hint, "FVector4") == 0)
	{
		return ImGui::DragFloat4(Label, static_cast<float*>(ValuePtr), Property.Speed);
	}
	if (Hint && std::strcmp(Hint, "FColor") == 0)
	{
		return ImGui::ColorEdit4(Label, &static_cast<FColor*>(ValuePtr)->R);
	}

	if (!Property.ScriptStruct)
	{
		ImGui::TextDisabled("%s <unregistered struct>", Label);
		return false;
	}

	bool bChanged = false;
	if (ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen))
	{
		TArray<const FProperty*> ChildProperties;
		Property.ScriptStruct->GetAllProperties(ChildProperties);
		for (const FProperty* Child : ChildProperties)
		{
			if (!Child || !Child->Name)
			{
				continue;
			}

			void* ChildPtr = reinterpret_cast<uint8*>(ValuePtr) + Child->Offset;
			const FString ChildLabel = MakeParticlePropertyLabel(*Child);
			if (DrawParticlePropertyValue(*Child, ChildPtr, NotifyTarget, ChildLabel.c_str()))
			{
				bChanged = true;
			}
		}
		ImGui::TreePop();
	}
	return bChanged;
}

void FEditorParticleSystemWidget::NotifyParticleModulePropertyChanged(UParticleModule* Module, UParticleEmitter* OwnerEmitter, const FProperty& Property)
{
	if (Module && Property.Name)
	{
		Module->PostEditProperty(Property.Name);
	}
	if (OwnerEmitter)
	{
		SyncInheritedModuleFromHigherLOD(OwnerEmitter, Module);
		OwnerEmitter->CacheEmitterModuleInfo();
	}
	if (ParticleSystemAsset)
	{
		ParticleSystemAsset->CacheEmitterModuleInfo();
	}
	bDirty = true;
}

void FEditorParticleSystemWidget::DrawCurveEditorPanel(const ImVec2& Size)
{
	DrawPanelHeader("Curve Editor");

	const ImVec2 BodySize(Size.x, std::max(1.0f, Size.y - PanelHeaderHeight));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));
	ImGui::BeginChild(
		"##ParticleCurveEditorBody",
		BodySize,
		false,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::BeginGroup();

	UParticleEmitter* ActiveEmitter = nullptr;
	UParticleModule* ActiveModule = nullptr;
	if (ParticleSystemAsset &&
		ActiveParticleCurveEmitterIndex >= 0 &&
		ActiveParticleCurveEmitterIndex < static_cast<int32>(ParticleSystemAsset->Emitters.size()))
	{
		ActiveEmitter = ParticleSystemAsset->Emitters[ActiveParticleCurveEmitterIndex];
		if (UParticleLODLevel* LODLevel = GetEmitterLODLevel(ActiveEmitter))
		{
			const TArray<UParticleModule*>& Modules = LODLevel->GetModules();
			if (ActiveParticleCurveModuleIndex >= 0 && ActiveParticleCurveModuleIndex < static_cast<int32>(Modules.size()))
			{
				ActiveModule = Modules[ActiveParticleCurveModuleIndex];
			}
		}
	}

	struct FParticleCurveChannelView
	{
		FString Label;
		FString Key;
		FFloatCurve* Curve = nullptr;
		UParticleModule* Module = nullptr;
		const FProperty* Property = nullptr;
		FString DistributionKey;
		FString ChannelName;
		int32 ComponentIndex = 0;
		bool bCurveDistribution = false;
		bool bStoredMaxValue = false;
		bool bMaxRangeChannel = false;
		float ConstantValue = 0.0f;
		ImU32 Color = 0;
	};

	TArray<FParticleCurveChannelView> Channels;
	if (ActiveModule && ActiveParticleCurveModuleKey == MakeParticleModuleCurveKey(ActiveModule))
	{
		TArray<const FProperty*> Properties;
		if (ActiveModule->GetClass())
		{
			ActiveModule->GetClass()->GetAllProperties(Properties);
		}

		const ImU32 CurveChannelColors[] =
		{
			ImGui::GetColorU32(ImVec4(0.96f, 0.18f, 0.22f, 1.0f)),
			ImGui::GetColorU32(ImVec4(0.18f, 0.78f, 0.28f, 1.0f)),
			ImGui::GetColorU32(ImVec4(0.20f, 0.55f, 1.00f, 1.0f)),
			ImGui::GetColorU32(ImVec4(1.00f, 0.76f, 0.14f, 1.0f)),
			ImGui::GetColorU32(ImVec4(0.78f, 0.32f, 1.00f, 1.0f)),
			ImGui::GetColorU32(ImVec4(0.10f, 0.86f, 0.86f, 1.0f)),
			ImGui::GetColorU32(ImVec4(1.00f, 0.42f, 0.12f, 1.0f)),
			ImGui::GetColorU32(ImVec4(0.58f, 0.90f, 0.22f, 1.0f)),
			ImGui::GetColorU32(ImVec4(0.38f, 0.36f, 1.00f, 1.0f)),
			ImGui::GetColorU32(ImVec4(1.00f, 0.30f, 0.70f, 1.0f)),
			ImGui::GetColorU32(ImVec4(0.34f, 0.92f, 0.62f, 1.0f)),
			ImGui::GetColorU32(ImVec4(0.82f, 0.82f, 0.86f, 1.0f))
		};

		auto EndsWithLocal = [](const FString& Text, const char* Suffix) -> bool
		{
			const size_t SuffixLength = std::strlen(Suffix);
			return Text.size() >= SuffixLength && Text.compare(Text.size() - SuffixLength, SuffixLength, Suffix) == 0;
		};
		auto FindPropertyByName = [&](const FString& PropertyName) -> const FProperty*
		{
			for (const FProperty* Candidate : Properties)
			{
				if (Candidate && Candidate->Name && PropertyName == Candidate->Name)
				{
					return Candidate;
				}
			}
			return nullptr;
		};
		auto IsVectorProperty = [](const FProperty& Property) -> bool
		{
			const char* Hint = Property.EditorHint;
			if ((!Hint || Hint[0] == '\0') && Property.ScriptStruct)
			{
				Hint = Property.ScriptStruct->GetName();
			}
			return Property.Type == EPropertyType::Struct && Hint && std::strcmp(Hint, "FVector") == 0;
		};
		auto GetPropertyChannelValue = [&](const FProperty& Property, const char* ChannelName) -> float
		{
			void* ValuePtr = Property.GetValuePtr(ActiveModule);
			if (!ValuePtr)
			{
				return 0.0f;
			}
			if (Property.Type == EPropertyType::Float)
			{
				return *static_cast<float*>(ValuePtr);
			}
			if (IsVectorProperty(Property))
			{
				const FVector* Value = static_cast<FVector*>(ValuePtr);
				if (std::strcmp(ChannelName, "Y") == 0) { return Value->Y; }
				if (std::strcmp(ChannelName, "Z") == 0) { return Value->Z; }
				return Value->X;
			}
			return 0.0f;
		};
		auto GetStoredMaxChannelValue = [&](const FProperty& Property, const FString& DistributionKey, const char* ChannelName) -> float
		{
			if (Property.Type == EPropertyType::Float)
			{
				auto It = ParticleDistributionFloatMaxValues.find(DistributionKey);
				if (It == ParticleDistributionFloatMaxValues.end())
				{
					It = ParticleDistributionFloatMaxValues.emplace(DistributionKey, GetPropertyChannelValue(Property, ChannelName)).first;
				}
				return It->second;
			}
			if (IsVectorProperty(Property))
			{
				auto It = ParticleDistributionVectorMaxValues.find(DistributionKey);
				if (It == ParticleDistributionVectorMaxValues.end())
				{
					FVector InitialValue = FVector::ZeroVector;
					if (void* ValuePtr = Property.GetValuePtr(ActiveModule))
					{
						InitialValue = *static_cast<FVector*>(ValuePtr);
					}
					It = ParticleDistributionVectorMaxValues.emplace(DistributionKey, InitialValue).first;
				}
				if (std::strcmp(ChannelName, "Y") == 0) { return It->second.Y; }
				if (std::strcmp(ChannelName, "Z") == 0) { return It->second.Z; }
				return It->second.X;
			}
			return 0.0f;
		};
		auto AddChannels = [&](const FProperty& Property, const FString& LabelSuffix, bool bCurveDistribution, bool bStoredMaxValue, const FString& StorageKey)
		{
			const bool bVector = IsVectorProperty(Property);
			const char* ChannelNames[] = { "X", "Y", "Z" };
			const int32 ChannelCount = bVector ? 3 : 1;
			auto GetDisplayChannelName = [&](const char* ChannelName) -> const char*
			{
				if (Cast<UParticleModuleColor>(ActiveModule) &&
					Property.Name &&
					std::strcmp(Property.Name, "ColorOverLife") == 0)
				{
					if (std::strcmp(ChannelName, "X") == 0) { return "R"; }
					if (std::strcmp(ChannelName, "Y") == 0) { return "G"; }
					if (std::strcmp(ChannelName, "Z") == 0) { return "B"; }
				}
				return ChannelName;
			};
			for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
			{
				const char* ChannelName = bVector ? ChannelNames[ChannelIndex] : "Value";
				const float InitialValue = bStoredMaxValue
					? GetStoredMaxChannelValue(Property, StorageKey, ChannelName)
					: GetPropertyChannelValue(Property, ChannelName);
				auto StripRangeSuffix = [](FString Label) -> FString
				{
					const char* Suffixes[] = { " Min", " Max", "Min", "Max" };
					for (const char* Suffix : Suffixes)
					{
						const size_t SuffixLength = std::strlen(Suffix);
						if (Label.size() >= SuffixLength && Label.compare(Label.size() - SuffixLength, SuffixLength, Suffix) == 0)
						{
							Label.erase(Label.size() - SuffixLength);
							break;
						}
					}
					return Label;
				};
				FParticleCurveChannelView View;
				View.Label = StripRangeSuffix(FString(GetPropertyDisplayName(Property)));
				if (!LabelSuffix.empty())
				{
					View.Label += " " + LabelSuffix;
				}
				if (bVector)
				{
					View.Label += " ";
					View.Label += GetDisplayChannelName(ChannelName);
				}
				View.Module = ActiveModule;
				View.Property = &Property;
				View.DistributionKey = StorageKey;
				View.ChannelName = ChannelName;
				View.ComponentIndex = ChannelIndex;
				View.bCurveDistribution = bCurveDistribution;
				View.bStoredMaxValue = bStoredMaxValue;
				View.ConstantValue = InitialValue;
				const bool bMaxChannel = bStoredMaxValue || LabelSuffix == "Max";
				View.bMaxRangeChannel = bMaxChannel;
				View.Color = CurveChannelColors[static_cast<int32>(Channels.size()) % static_cast<int32>(IM_ARRAYSIZE(CurveChannelColors))];
				if (bCurveDistribution)
				{
					const FString CurveChannelName = bStoredMaxValue
						? (bVector ? FString("Max") + ChannelName : FString("MaxValue"))
						: FString(ChannelName);
					View.Key = MakeParticleDistributionCurveKey(ActiveModule, Property, CurveChannelName.c_str()) + "::Range::" + (bMaxChannel ? "Max" : "Min");
					View.Curve = &GetOrCreateParticleDistributionCurve(ActiveModule, Property, CurveChannelName.c_str(), InitialValue);
				}
				else
				{
					View.Key = StorageKey + "::Constant::" + (bMaxChannel ? "Max::" : "Min::") + ChannelName;
				}
				Channels.push_back(View);
			}
		};

		for (const FProperty* Property : Properties)
		{
			if (!Property || !Property->Name || !IsParticleDistributionProperty(ActiveModule, *Property))
			{
				continue;
			}

			const FString PropertyName = Property->Name;
			if (EndsWithLocal(PropertyName, "Max"))
			{
				const FString MinName = PropertyName.substr(0, PropertyName.size() - 3) + "Min";
				if (const FProperty* MinProperty = FindPropertyByName(MinName))
				{
					if (IsParticleDistributionProperty(ActiveModule, *MinProperty) && MinProperty->Type == Property->Type)
					{
						continue;
					}
				}
			}

			const FProperty* SecondaryProperty = nullptr;
			if (EndsWithLocal(PropertyName, "Min"))
			{
				const FString MaxName = PropertyName.substr(0, PropertyName.size() - 3) + "Max";
				SecondaryProperty = FindPropertyByName(MaxName);
				if (SecondaryProperty && (!IsParticleDistributionProperty(ActiveModule, *SecondaryProperty) || SecondaryProperty->Type != Property->Type))
				{
					SecondaryProperty = nullptr;
				}
			}

			const FString DistributionKey = MakeParticleDistributionKey(ActiveModule, *Property);
			auto DistributionIt = ParticleDistributionKinds.find(DistributionKey);
			if (DistributionIt == ParticleDistributionKinds.end())
			{
				if (const FParticleDistributionRuntimeData* RuntimeData = ActiveModule->FindDistributionRuntimeData(Property->Name))
				{
					DistributionIt = ParticleDistributionKinds.emplace(DistributionKey, std::clamp(RuntimeData->Kind, 0, 3)).first;
					if (Property->Type == EPropertyType::Float)
					{
						ParticleDistributionFloatMaxValues[DistributionKey] = RuntimeData->StoredMaxFloat;
						if (auto CurveIt = RuntimeData->Curves.find("Value"); CurveIt != RuntimeData->Curves.end())
						{
							ParticleDistributionCurves[MakeParticleDistributionCurveKey(ActiveModule, *Property, "Value")] = CurveIt->second;
						}
						if (SecondaryProperty)
						{
							if (auto CurveIt = RuntimeData->Curves.find("MaxValue"); CurveIt != RuntimeData->Curves.end())
							{
								ParticleDistributionCurves[MakeParticleDistributionCurveKey(ActiveModule, *SecondaryProperty, "Value")] = CurveIt->second;
							}
						}
						else if (RuntimeData->Kind == 3)
						{
							if (auto CurveIt = RuntimeData->Curves.find("MaxValue"); CurveIt != RuntimeData->Curves.end())
							{
								ParticleDistributionCurves[MakeParticleDistributionCurveKey(ActiveModule, *Property, "MaxValue")] = CurveIt->second;
							}
						}
					}
					else if (IsVectorProperty(*Property))
					{
						ParticleDistributionVectorMaxValues[DistributionKey] = RuntimeData->StoredMaxVector;
						const char* Channels[] = { "X", "Y", "Z" };
						for (const char* ChannelName : Channels)
						{
							if (auto CurveIt = RuntimeData->Curves.find(ChannelName); CurveIt != RuntimeData->Curves.end())
							{
								ParticleDistributionCurves[MakeParticleDistributionCurveKey(ActiveModule, *Property, ChannelName)] = CurveIt->second;
							}
							if (SecondaryProperty)
							{
								const FString MaxChannelName = FString("Max") + ChannelName;
								if (auto CurveIt = RuntimeData->Curves.find(MaxChannelName); CurveIt != RuntimeData->Curves.end())
								{
									ParticleDistributionCurves[MakeParticleDistributionCurveKey(ActiveModule, *SecondaryProperty, ChannelName)] = CurveIt->second;
								}
							}
							else if (RuntimeData->Kind == 3)
							{
								const FString MaxChannelName = FString("Max") + ChannelName;
								if (auto CurveIt = RuntimeData->Curves.find(MaxChannelName); CurveIt != RuntimeData->Curves.end())
								{
									ParticleDistributionCurves[MakeParticleDistributionCurveKey(ActiveModule, *Property, MaxChannelName.c_str())] = CurveIt->second;
								}
							}
						}
					}
				}
				else
				{
					DistributionIt = ParticleDistributionKinds.emplace(DistributionKey, 3).first;
				}
			}
			DistributionIt->second = std::clamp(DistributionIt->second, 0, 3);
			const int32 DistributionKind = DistributionIt->second;
			if (DistributionKind == 0)
			{
				AddChannels(*Property, "", false, false, DistributionKey);
			}
			else if (DistributionKind == 1)
			{
				AddChannels(*Property, "", true, false, DistributionKey);
			}
			else if (DistributionKind == 2)
			{
				AddChannels(*Property, SecondaryProperty ? "Min" : "Min", false, false, DistributionKey);
				if (SecondaryProperty)
				{
					AddChannels(*SecondaryProperty, "Max", false, false, DistributionKey);
				}
				else
				{
					AddChannels(*Property, "Max", false, true, DistributionKey);
				}
			}
			else
			{
				AddChannels(*Property, "Min", true, false, DistributionKey);
				if (SecondaryProperty)
				{
					AddChannels(*SecondaryProperty, "Max", true, false, DistributionKey);
				}
				else
				{
					AddChannels(*Property, "Max", true, true, DistributionKey);
				}
			}
		}
	}

	const FString ActiveModuleLabel = ActiveModule ? GetModuleDisplayName(ActiveModule, false) : FString("No Module Selected");
	ImGui::TextUnformatted(ActiveModuleLabel.c_str());
	ImGui::Separator();

	const ImGuiStyle& Style = ImGui::GetStyle();
	const ImVec2 ContentAvail = ImGui::GetContentRegionAvail();
	const float ListWidth = std::clamp(ContentAvail.x * 0.24f, 150.0f, std::min(220.0f, ContentAvail.x * 0.45f));
	const float CanvasWidth = std::max(1.0f, ContentAvail.x - ListWidth - Style.ItemSpacing.x);
	const float CanvasHeight = std::max(1.0f, ContentAvail.y);
	const ImVec2 CanvasChildSize(CanvasWidth, CanvasHeight);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	ImGui::BeginChild("##ParticleCurveChannelList", ImVec2(ListWidth, CanvasChildSize.y), true);
	if (!ActiveModule)
	{
		ImGui::TextDisabled("Click a module curve icon.");
	}
	else if (Channels.empty())
	{
		ImGui::TextDisabled("No distribution values.");
	}
	if (!ActiveParticleCurveChannelKey.empty())
	{
		bool bActiveChannelStillExists = false;
		for (const FParticleCurveChannelView& Channel : Channels)
		{
			if (Channel.Key == ActiveParticleCurveChannelKey)
			{
				bActiveChannelStillExists = true;
				break;
			}
		}
		if (!bActiveChannelStillExists)
		{
			ActiveParticleCurveChannelKey.clear();
			ActiveParticleCurveKeyIndex = -1;
		}
	}
	for (FParticleCurveChannelView& Channel : Channels)
	{
		ImGui::PushID(Channel.Key.c_str());
		const bool bSelected = ActiveParticleCurveChannelKey == Channel.Key;
		const float RowHeight = 22.0f;
		const float ColorBoxWidth = 10.0f;
		const float ColorBoxRightPadding = 2.0f;
		const float ColorBoxLeftPadding = 14.0f;
		const float RowContentWidth = ImGui::GetContentRegionAvail().x;
		const ImVec2 RowStart = ImGui::GetCursorPos();
		const float ColorBoxX = RowStart.x + std::max(0.0f, RowContentWidth - ColorBoxRightPadding - ColorBoxWidth);
		const float LabelWidth = std::max(1.0f, ColorBoxX - RowStart.x - ColorBoxLeftPadding);
		if (ImGui::Selectable(Channel.Label.c_str(), bSelected, 0, ImVec2(LabelWidth, RowHeight)))
		{
			ActiveParticleCurveChannelKey = Channel.Key;
			ActiveParticleCurveKeyIndex = -1;
		}
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::SetCursorPosX(ColorBoxX);
		const float ColorBoxY = RowStart.y + std::max(0.0f, (RowHeight - ColorBoxWidth) * 0.5f);
		ImGui::SetCursorPosY(ColorBoxY);
		ImGui::ColorButton("##Color", ImGui::ColorConvertU32ToFloat4(Channel.Color), ImGuiColorEditFlags_NoTooltip, ImVec2(ColorBoxWidth, ColorBoxWidth));
		ImGui::PopID();
	}
	if (ActiveParticleCurveChannelKey.empty() && !Channels.empty())
	{
		ActiveParticleCurveChannelKey = Channels.front().Key;
		ActiveParticleCurveKeyIndex = -1;
	}
	bool bEmitterTimeCurveView = false;
	for (const FParticleCurveChannelView& Channel : Channels)
	{
		if (Channel.Property && IsEmitterTimeDistributionProperty(Channel.Module, *Channel.Property))
		{
			bEmitterTimeCurveView = true;
			break;
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("##ParticleCurveCanvas", CanvasChildSize, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const ImVec2 CanvasContentMin = ImGui::GetCursorScreenPos();
	const ImVec2 CanvasContentSize = ImGui::GetContentRegionAvail();
	constexpr float GraphInset = 2.0f;
	const ImVec2 CanvasMin(CanvasContentMin.x + GraphInset, CanvasContentMin.y + GraphInset);
	const ImVec2 CanvasSize(
		std::max(1.0f, CanvasContentSize.x - GraphInset * 2.0f),
		std::max(1.0f, CanvasContentSize.y - GraphInset * 2.0f));
	const ImVec2 CanvasMax(CanvasMin.x + CanvasSize.x, CanvasMin.y + CanvasSize.y);
	ImGui::InvisibleButton("##ParticleCurveCanvasHit", CanvasContentSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool bCanvasHovered = ImGui::IsItemHovered();
	const bool bDraggingParticleCurveValue = ImGui::IsMouseDown(ImGuiMouseButton_Left) && !DragParticleCurveChannelKey.empty();

	float DesiredMinValue = 0.0f;
	float DesiredMaxValue = 1.0f;
	float DesiredMinTime = 0.0f;
	float DesiredMaxTime = 1.0f;
	bool bHasValue = false;
	bool bHasTime = false;
	for (const FParticleCurveChannelView& Channel : Channels)
	{
		if (!Channel.bCurveDistribution)
		{
			DesiredMinValue = bHasValue ? std::min(DesiredMinValue, Channel.ConstantValue) : Channel.ConstantValue;
			DesiredMaxValue = bHasValue ? std::max(DesiredMaxValue, Channel.ConstantValue) : Channel.ConstantValue;
			bHasValue = true;
			continue;
		}
		if (!Channel.Curve)
		{
			continue;
		}
		for (const FCurveKey& Key : Channel.Curve->Keys)
		{
			DesiredMinTime = bHasTime ? std::min(DesiredMinTime, Key.Time) : Key.Time;
			DesiredMaxTime = bHasTime ? std::max(DesiredMaxTime, Key.Time) : Key.Time;
			bHasTime = true;
			DesiredMinValue = bHasValue ? std::min(DesiredMinValue, Key.Value) : Key.Value;
			DesiredMaxValue = bHasValue ? std::max(DesiredMaxValue, Key.Value) : Key.Value;
			bHasValue = true;
		}
	}
	if (std::fabs(DesiredMaxValue - DesiredMinValue) < 0.001f)
	{
		DesiredMinValue -= 1.0f;
		DesiredMaxValue += 1.0f;
	}
	const float ValuePadding = (DesiredMaxValue - DesiredMinValue) * 0.12f;
	DesiredMinValue -= ValuePadding;
	DesiredMaxValue += ValuePadding;

	if (!bParticleCurveViewInitialized || ParticleCurveViewModuleKey != ActiveParticleCurveModuleKey)
	{
		ParticleCurveViewModuleKey = ActiveParticleCurveModuleKey;
		ParticleCurveViewMinValue = DesiredMinValue;
		ParticleCurveViewMaxValue = DesiredMaxValue;
		ParticleCurveViewMinTime = 0.0f;
		ParticleCurveViewMaxTime = bEmitterTimeCurveView ? std::max(1.0f, DesiredMaxTime) : 1.0f;
		bParticleCurveViewInitialized = true;
	}
	else if (!bParticleCurveViewUserAdjusted)
	{
		if (bDraggingParticleCurveValue)
		{
			constexpr float DragAutoExpandAlpha = 0.08f;
			if (DesiredMinValue < ParticleCurveViewMinValue)
			{
				ParticleCurveViewMinValue += (DesiredMinValue - ParticleCurveViewMinValue) * DragAutoExpandAlpha;
			}
			if (DesiredMaxValue > ParticleCurveViewMaxValue)
			{
				ParticleCurveViewMaxValue += (DesiredMaxValue - ParticleCurveViewMaxValue) * DragAutoExpandAlpha;
			}
		}
		else
		{
			ParticleCurveViewMinValue = std::min(ParticleCurveViewMinValue, DesiredMinValue);
			ParticleCurveViewMaxValue = std::max(ParticleCurveViewMaxValue, DesiredMaxValue);
		}
	}
	if (std::fabs(ParticleCurveViewMaxValue - ParticleCurveViewMinValue) < 0.001f)
	{
		ParticleCurveViewMinValue -= 1.0f;
		ParticleCurveViewMaxValue += 1.0f;
	}
	if (std::fabs(ParticleCurveViewMaxTime - ParticleCurveViewMinTime) < 0.001f)
	{
		ParticleCurveViewMinTime = 0.0f;
		ParticleCurveViewMaxTime = 1.0f;
	}
	if (!bEmitterTimeCurveView)
	{
		ParticleCurveViewMinTime = std::clamp(ParticleCurveViewMinTime, 0.0f, 1.0f);
		ParticleCurveViewMaxTime = std::clamp(ParticleCurveViewMaxTime, 0.0f, 1.0f);
	}
	else
	{
		ParticleCurveViewMinTime = std::max(ParticleCurveViewMinTime, 0.0f);
		ParticleCurveViewMaxTime = std::max(ParticleCurveViewMaxTime, ParticleCurveViewMinTime + 0.001f);
	}
	if (ParticleCurveViewMaxTime <= ParticleCurveViewMinTime)
	{
		ParticleCurveViewMinTime = 0.0f;
		ParticleCurveViewMaxTime = 1.0f;
	}

	const float MinTime = ParticleCurveViewMinTime;
	const float MaxTime = ParticleCurveViewMaxTime;
	const float MinValue = ParticleCurveViewMinValue;
	const float MaxValue = ParticleCurveViewMaxValue;

	auto ToScreen = [&](float Time, float Value) -> ImVec2
	{
		const float X = CanvasMin.x + ((Time - MinTime) / (MaxTime - MinTime)) * CanvasSize.x;
		const float Alpha = (Value - MinValue) / (MaxValue - MinValue);
		const float Y = CanvasMax.y - Alpha * CanvasSize.y;
		return ImVec2(X, Y);
	};
	auto FromScreen = [&](const ImVec2& Screen) -> FCurveKey
	{
		FCurveKey Key;
		const float TimeAlpha = (Screen.x - CanvasMin.x) / CanvasSize.x;
		const float RawTime = MinTime + TimeAlpha * (MaxTime - MinTime);
		Key.Time = bEmitterTimeCurveView ? std::max(RawTime, 0.0f) : std::clamp(RawTime, 0.0f, 1.0f);
		const float Alpha = std::clamp((CanvasMax.y - Screen.y) / CanvasSize.y, 0.0f, 1.0f);
		Key.Value = MinValue + Alpha * (MaxValue - MinValue);
		Key.InterpMode = ECurveInterpMode::Cubic;
		Key.TangentMode = ECurveTangentMode::Auto;
		return Key;
	};
	auto EvaluateParticleCurve = [](const FFloatCurve& Curve, float Time) -> float
	{
		if (Curve.Keys.empty())
		{
			return 0.0f;
		}

		TArray<FCurveKey> SortedKeys = Curve.Keys;
		std::sort(
			SortedKeys.begin(),
			SortedKeys.end(),
			[](const FCurveKey& A, const FCurveKey& B)
			{
				return A.Time < B.Time;
			});

		if (Time <= SortedKeys.front().Time)
		{
			return SortedKeys.front().Value;
		}
		if (Time >= SortedKeys.back().Time)
		{
			return SortedKeys.back().Value;
		}

		for (int32 KeyIndex = 0; KeyIndex + 1 < static_cast<int32>(SortedKeys.size()); ++KeyIndex)
		{
			const FCurveKey& StartKey = SortedKeys[KeyIndex];
			const FCurveKey& EndKey = SortedKeys[KeyIndex + 1];
			if (Time < StartKey.Time || Time >= EndKey.Time)
			{
				continue;
			}

			const float SegmentLength = EndKey.Time - StartKey.Time;
			if (std::fabs(SegmentLength) < 0.0001f)
			{
				return StartKey.Value;
			}

			const float Alpha = std::clamp((Time - StartKey.Time) / SegmentLength, 0.0f, 1.0f);
			if (StartKey.InterpMode == ECurveInterpMode::Constant)
			{
				return StartKey.Value;
			}
			if (StartKey.InterpMode == ECurveInterpMode::Linear)
			{
				return StartKey.Value + (EndKey.Value - StartKey.Value) * Alpha;
			}

			const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);
			return StartKey.Value + (EndKey.Value - StartKey.Value) * SmoothAlpha;
		}

		return SortedKeys.back().Value;
	};
	auto FindCurveChannelByKey = [&](const FString& Key) -> FParticleCurveChannelView*
	{
		for (FParticleCurveChannelView& Channel : Channels)
		{
			if (Channel.Key == Key)
			{
				return &Channel;
			}
		}
		return nullptr;
	};
	auto ForEachSiblingCurveChannel = [&](const FParticleCurveChannelView& SourceChannel, auto&& Callback)
	{
		for (FParticleCurveChannelView& Channel : Channels)
		{
			if (!Channel.bCurveDistribution ||
				!Channel.Curve ||
				Channel.Module != SourceChannel.Module ||
				Channel.Property != SourceChannel.Property ||
				Channel.DistributionKey != SourceChannel.DistributionKey ||
				Channel.bMaxRangeChannel != SourceChannel.bMaxRangeChannel)
			{
				continue;
			}
			Callback(Channel);
		}
	};
	auto ClampParticleCurveTimeView = [&]()
	{
		constexpr float TimeViewMin = 0.0f;
		constexpr float TimeViewMax = 1.0f;
		constexpr float EmitterTimeViewMax = 100000.0f;
		float TimeRange = ParticleCurveViewMaxTime - ParticleCurveViewMinTime;
		const float EffectiveTimeViewMax = bEmitterTimeCurveView ? EmitterTimeViewMax : TimeViewMax;
		TimeRange = std::clamp(TimeRange, 0.02f, EffectiveTimeViewMax - TimeViewMin);

		if (ParticleCurveViewMinTime < TimeViewMin)
		{
			ParticleCurveViewMinTime = TimeViewMin;
			ParticleCurveViewMaxTime = ParticleCurveViewMinTime + TimeRange;
		}
		if (ParticleCurveViewMaxTime > EffectiveTimeViewMax)
		{
			ParticleCurveViewMaxTime = EffectiveTimeViewMax;
			ParticleCurveViewMinTime = ParticleCurveViewMaxTime - TimeRange;
		}
		ParticleCurveViewMinTime = std::clamp(ParticleCurveViewMinTime, TimeViewMin, EffectiveTimeViewMax - TimeRange);
		ParticleCurveViewMaxTime = ParticleCurveViewMinTime + TimeRange;
	};

	const ImVec2 MousePos = ImGui::GetIO().MousePos;
	const bool bMouseInCanvas =
		MousePos.x >= CanvasMin.x && MousePos.x <= CanvasMax.x &&
		MousePos.y >= CanvasMin.y && MousePos.y <= CanvasMax.y;
	if (bCanvasHovered)
	{
		const float Wheel = ImGui::GetIO().MouseWheel;
		if (std::fabs(Wheel) > 0.001f)
		{
			const float TimeAlpha = std::clamp((MousePos.x - CanvasMin.x) / CanvasSize.x, 0.0f, 1.0f);
			const float ValueAlpha = std::clamp((CanvasMax.y - MousePos.y) / CanvasSize.y, 0.0f, 1.0f);
			const float TimeCenter = MinTime + TimeAlpha * (MaxTime - MinTime);
			const float ValueCenter = MinValue + ValueAlpha * (MaxValue - MinValue);
			const float ZoomScale = std::pow(0.88f, Wheel);
			const float NewTimeRange = std::clamp((MaxTime - MinTime) * ZoomScale, 0.02f, bEmitterTimeCurveView ? 100000.0f : 1.0f);
			const float NewValueRange = std::max(0.02f, (MaxValue - MinValue) * ZoomScale);
			ParticleCurveViewMinTime = TimeCenter - TimeAlpha * NewTimeRange;
			ParticleCurveViewMaxTime = ParticleCurveViewMinTime + NewTimeRange;
			ClampParticleCurveTimeView();
			ParticleCurveViewMinValue = ValueCenter - ValueAlpha * NewValueRange;
			ParticleCurveViewMaxValue = ParticleCurveViewMinValue + NewValueRange;
			bParticleCurveViewUserAdjusted = true;
		}
	}
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
	{
		const ImVec2 Delta = ImGui::GetIO().MouseDelta;
		const float TimeDelta = -Delta.x / CanvasSize.x * (MaxTime - MinTime);
		const float ValueDelta = Delta.y / CanvasSize.y * (MaxValue - MinValue);
		ParticleCurveViewMinTime += TimeDelta;
		ParticleCurveViewMaxTime += TimeDelta;
		ClampParticleCurveTimeView();
		ParticleCurveViewMinValue += ValueDelta;
		ParticleCurveViewMaxValue += ValueDelta;
		bParticleCurveViewUserAdjusted = true;
	}

	DrawList->PushClipRect(CanvasMin, CanvasMax, true);
	DrawList->AddRectFilled(CanvasMin, CanvasMax, ImGui::GetColorU32(ImVec4(0.20f, 0.20f, 0.20f, 1.0f)));
	DrawList->AddRect(CanvasMin, CanvasMax, ImGui::GetColorU32(ImVec4(0.48f, 0.48f, 0.48f, 1.0f)));
	for (int32 GridIndex = 0; GridIndex <= 10; ++GridIndex)
	{
		const float T = static_cast<float>(GridIndex) / 10.0f;
		const float TimeValue = MinTime + T * (MaxTime - MinTime);
		const float X = CanvasMin.x + T * CanvasSize.x;
		DrawList->AddLine(ImVec2(X, CanvasMin.y), ImVec2(X, CanvasMax.y), ImGui::GetColorU32(ImVec4(0.55f, 0.55f, 0.55f, 0.75f)));
		char Label[32];
		std::snprintf(Label, sizeof(Label), "%.2f", TimeValue);
		DrawList->AddText(ImVec2(X + 3.0f, CanvasMax.y - 16.0f), ImGui::GetColorU32(ImVec4(0.85f, 0.85f, 0.85f, 1.0f)), Label);
	}
	for (int32 GridIndex = 0; GridIndex <= 4; ++GridIndex)
	{
		const float T = static_cast<float>(GridIndex) / 4.0f;
		const float Y = CanvasMin.y + T * CanvasSize.y;
		DrawList->AddLine(ImVec2(CanvasMin.x, Y), ImVec2(CanvasMax.x, Y), ImGui::GetColorU32(ImVec4(0.55f, 0.55f, 0.55f, 0.75f)));
		const float Value = MaxValue - T * (MaxValue - MinValue);
		char Label[32];
		std::snprintf(Label, sizeof(Label), "%.2f", Value);
		DrawList->AddText(ImVec2(CanvasMin.x + 4.0f, Y + 2.0f), ImGui::GetColorU32(ImVec4(0.85f, 0.85f, 0.85f, 1.0f)), Label);
	}

	FString HitChannelKey;
	int32 HitKeyIndex = -1;
	FString HitConstantChannelKey;
	float BestHitDistanceSq = 64.0f;
	float BestConstantDistance = 8.0f;
	auto TestHitChannel = [&](const FParticleCurveChannelView& Channel, float& InOutBestDistanceSq, FString& OutChannelKey, int32& OutKeyIndex)
	{
		if (!Channel.bCurveDistribution || !Channel.Curve)
		{
			return;
		}
		for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Channel.Curve->Keys.size()); ++KeyIndex)
		{
			const ImVec2 P = ToScreen(Channel.Curve->Keys[KeyIndex].Time, Channel.Curve->Keys[KeyIndex].Value);
			const float Dx = MousePos.x - P.x;
			const float Dy = MousePos.y - P.y;
			const float DistSq = Dx * Dx + Dy * Dy;
			if (DistSq < InOutBestDistanceSq)
			{
				InOutBestDistanceSq = DistSq;
				OutChannelKey = Channel.Key;
				OutKeyIndex = KeyIndex;
			}
		}
	};
	if (!ActiveParticleCurveChannelKey.empty())
	{
		if (FParticleCurveChannelView* ActiveHitChannel = FindCurveChannelByKey(ActiveParticleCurveChannelKey))
		{
			TestHitChannel(*ActiveHitChannel, BestHitDistanceSq, HitChannelKey, HitKeyIndex);
		}
	}
	for (const FParticleCurveChannelView& Channel : Channels)
	{
		if (!Channel.bCurveDistribution)
		{
			const float Y = ToScreen(0.0f, Channel.ConstantValue).y;
			const float Distance = std::fabs(MousePos.y - Y);
			if (Distance < BestConstantDistance && MousePos.x >= CanvasMin.x && MousePos.x <= CanvasMax.x)
			{
				BestConstantDistance = Distance;
				HitConstantChannelKey = Channel.Key;
			}
			continue;
		}
		if (!Channel.Curve)
		{
			continue;
		}
		if (!HitChannelKey.empty() && Channel.Key != HitChannelKey)
		{
			continue;
		}
		TestHitChannel(Channel, BestHitDistanceSq, HitChannelKey, HitKeyIndex);
	}

	if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && HitKeyIndex >= 0)
	{
		ActiveParticleCurveChannelKey = HitChannelKey;
		ActiveParticleCurveKeyIndex = HitKeyIndex;
		DragParticleCurveChannelKey = HitChannelKey;
		DragParticleCurveKeyIndex = HitKeyIndex;
	}
	else if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !HitConstantChannelKey.empty())
	{
		ActiveParticleCurveChannelKey = HitConstantChannelKey;
		ActiveParticleCurveKeyIndex = -1;
		DragParticleCurveChannelKey = HitConstantChannelKey;
		DragParticleCurveKeyIndex = -2;
	}

	if (bCanvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && HitKeyIndex < 0)
	{
		if (ActiveParticleCurveChannelKey.empty() && !Channels.empty())
		{
			ActiveParticleCurveChannelKey = Channels.front().Key;
		}
		FParticleCurveChannelView* ActiveChannel = FindCurveChannelByKey(ActiveParticleCurveChannelKey);
		if (ActiveChannel && ActiveChannel->bCurveDistribution && ActiveChannel->Curve)
		{
			CaptureUndoSnapshot("Add Particle Curve Key");
			FCurveKey Key = FromScreen(ImGui::GetIO().MousePos);
			ForEachSiblingCurveChannel(
				*ActiveChannel,
				[&](FParticleCurveChannelView& SiblingChannel)
				{
					FCurveKey SiblingKey = Key;
					if (SiblingChannel.Key != ActiveChannel->Key)
					{
						SiblingKey.Value = SiblingChannel.Curve ? EvaluateParticleCurve(*SiblingChannel.Curve, Key.Time) : 0.0f;
					}
					SiblingChannel.Curve->Keys.push_back(SiblingKey);
					SiblingChannel.Curve->SortKeys();
				});
			ActiveParticleCurveKeyIndex = -1;
			for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(ActiveChannel->Curve->Keys.size()); ++KeyIndex)
			{
				const FCurveKey& SortedKey = ActiveChannel->Curve->Keys[KeyIndex];
				if (std::fabs(SortedKey.Time - Key.Time) < 0.0001f && std::fabs(SortedKey.Value - Key.Value) < 0.0001f)
				{
					ActiveParticleCurveKeyIndex = KeyIndex;
					break;
				}
			}
			DragParticleCurveChannelKey = ActiveParticleCurveChannelKey;
			DragParticleCurveKeyIndex = ActiveParticleCurveKeyIndex;
			bParticleCurveEditUndoCaptured = true;
			bDirty = true;
		}
	}

	if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && bMouseInCanvas && !DragParticleCurveChannelKey.empty() && DragParticleCurveKeyIndex >= 0)
	{
		FParticleCurveChannelView* DragChannel = FindCurveChannelByKey(DragParticleCurveChannelKey);
		if (DragChannel && DragChannel->bCurveDistribution && DragChannel->Curve && DragParticleCurveKeyIndex < static_cast<int32>(DragChannel->Curve->Keys.size()))
		{
			if (!bParticleCurveEditUndoCaptured)
			{
				CaptureUndoSnapshot("Edit Particle Curve Key");
				bParticleCurveEditUndoCaptured = true;
			}
			FCurveKey DragKey = FromScreen(MousePos);
			ForEachSiblingCurveChannel(
				*DragChannel,
				[&](FParticleCurveChannelView& SiblingChannel)
				{
					if (DragParticleCurveKeyIndex >= static_cast<int32>(SiblingChannel.Curve->Keys.size()))
					{
						return;
					}
					SiblingChannel.Curve->Keys[DragParticleCurveKeyIndex].Time = DragKey.Time;
					if (SiblingChannel.Key == DragChannel->Key)
					{
						SiblingChannel.Curve->Keys[DragParticleCurveKeyIndex].Value = DragKey.Value;
					}
				});
			ActiveParticleCurveChannelKey = DragParticleCurveChannelKey;
			ActiveParticleCurveKeyIndex = DragParticleCurveKeyIndex;
			bDirty = true;
		}
	}
	else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && bMouseInCanvas && !DragParticleCurveChannelKey.empty() && DragParticleCurveKeyIndex == -2)
	{
		for (FParticleCurveChannelView& Channel : Channels)
		{
			if (Channel.Key != DragParticleCurveChannelKey || Channel.bCurveDistribution || !Channel.Property)
			{
				continue;
			}

			if (!bParticleCurveEditUndoCaptured)
			{
				CaptureUndoSnapshot("Edit Particle Constant Curve");
				bParticleCurveEditUndoCaptured = true;
			}

			const FCurveKey DragValue = FromScreen(MousePos);
			Channel.ConstantValue = DragValue.Value;
			if (Channel.bStoredMaxValue)
			{
				if (Channel.Property->Type == EPropertyType::Float)
				{
					ParticleDistributionFloatMaxValues[Channel.DistributionKey] = Channel.ConstantValue;
				}
				else
				{
					FVector& Value = ParticleDistributionVectorMaxValues[Channel.DistributionKey];
					if (Channel.ComponentIndex == 1) { Value.Y = Channel.ConstantValue; }
					else if (Channel.ComponentIndex == 2) { Value.Z = Channel.ConstantValue; }
					else { Value.X = Channel.ConstantValue; }
				}
			}
			else if (void* ValuePtr = Channel.Property->GetValuePtr(Channel.Module))
			{
				if (Channel.Property->Type == EPropertyType::Float)
				{
					*static_cast<float*>(ValuePtr) = Channel.ConstantValue;
				}
				else
				{
					FVector* Value = static_cast<FVector*>(ValuePtr);
					if (Channel.ComponentIndex == 1) { Value->Y = Channel.ConstantValue; }
					else if (Channel.ComponentIndex == 2) { Value->Z = Channel.ConstantValue; }
					else { Value->X = Channel.ConstantValue; }
				}
				NotifyParticleModulePropertyChanged(Channel.Module, ActiveEmitter, *Channel.Property);
			}

			ActiveParticleCurveChannelKey = DragParticleCurveChannelKey;
			ActiveParticleCurveKeyIndex = -1;
			bDirty = true;
			RefreshPreviewComponent(false);
			break;
		}
	}
	else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		const bool bEditedParticleCurve = bParticleCurveEditUndoCaptured;
		for (FParticleCurveChannelView& Channel : Channels)
		{
			if (Channel.Curve)
			{
				Channel.Curve->SortKeys();
			}
		}
		DragParticleCurveChannelKey.clear();
		DragParticleCurveKeyIndex = -1;
		bParticleCurveEditUndoCaptured = false;
		if (bEditedParticleCurve)
		{
			SyncParticleDistributionRuntimeDataToAsset();
			RefreshPreviewComponent(true);
		}
	}

	for (FParticleCurveChannelView& Channel : Channels)
	{
		if (!Channel.bCurveDistribution)
		{
			const ImVec2 A = ToScreen(MinTime, Channel.ConstantValue);
			const ImVec2 B = ToScreen(MaxTime, Channel.ConstantValue);
			DrawList->AddLine(A, B, Channel.Color, 2.0f);
			if (ActiveParticleCurveChannelKey == Channel.Key)
			{
				const float Y = A.y;
				DrawList->AddRectFilled(ImVec2(CanvasMin.x + 4.0f, Y - 3.0f), ImVec2(CanvasMin.x + 10.0f, Y + 3.0f), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
			}
			continue;
		}
		if (!Channel.Curve)
		{
			continue;
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			Channel.Curve->SortKeys();
		}
		TArray<FCurveKey> SortedKeys = Channel.Curve->Keys;
		std::sort(
			SortedKeys.begin(),
			SortedKeys.end(),
			[](const FCurveKey& A, const FCurveKey& B)
			{
				return A.Time < B.Time;
			});
		if (!SortedKeys.empty())
		{
			if (MinTime < SortedKeys.front().Time)
			{
				DrawList->AddLine(
					ToScreen(MinTime, SortedKeys.front().Value),
					ToScreen(std::min(MaxTime, SortedKeys.front().Time), SortedKeys.front().Value),
					Channel.Color,
					2.0f);
			}

			for (int32 KeyIndex = 0; KeyIndex + 1 < static_cast<int32>(SortedKeys.size()); ++KeyIndex)
			{
				const FCurveKey& StartKey = SortedKeys[KeyIndex];
				const FCurveKey& EndKey = SortedKeys[KeyIndex + 1];
				const float SegmentStart = std::max(MinTime, StartKey.Time);
				const float SegmentEnd = std::min(MaxTime, EndKey.Time);
				if (SegmentEnd < SegmentStart)
				{
					continue;
				}

				if (StartKey.InterpMode == ECurveInterpMode::Constant)
				{
					const ImVec2 StepStart = ToScreen(SegmentStart, StartKey.Value);
					const ImVec2 StepEnd = ToScreen(SegmentEnd, StartKey.Value);
					DrawList->AddLine(StepStart, StepEnd, Channel.Color, 2.0f);
					if (SegmentEnd >= EndKey.Time)
					{
						DrawList->AddLine(StepEnd, ToScreen(EndKey.Time, EndKey.Value), Channel.Color, 2.0f);
					}
					continue;
				}

				const int32 SegmentSamples = std::max(2, static_cast<int32>((SegmentEnd - SegmentStart) / std::max(MaxTime - MinTime, 0.0001f) * CanvasSize.x / 8.0f));
				ImVec2 PrevPoint = ToScreen(SegmentStart, EvaluateParticleCurve(*Channel.Curve, SegmentStart));
				for (int32 SampleIndex = 1; SampleIndex <= SegmentSamples; ++SampleIndex)
				{
					const float Time = SegmentStart + (SegmentEnd - SegmentStart) * static_cast<float>(SampleIndex) / static_cast<float>(SegmentSamples);
					const ImVec2 NextPoint = ToScreen(Time, EvaluateParticleCurve(*Channel.Curve, Time));
					DrawList->AddLine(PrevPoint, NextPoint, Channel.Color, 2.0f);
					PrevPoint = NextPoint;
				}
			}

			if (MaxTime > SortedKeys.back().Time)
			{
				DrawList->AddLine(
					ToScreen(std::max(MinTime, SortedKeys.back().Time), SortedKeys.back().Value),
					ToScreen(MaxTime, SortedKeys.back().Value),
					Channel.Color,
					2.0f);
			}
		}
		for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Channel.Curve->Keys.size()); ++KeyIndex)
		{
			const ImVec2 P = ToScreen(Channel.Curve->Keys[KeyIndex].Time, Channel.Curve->Keys[KeyIndex].Value);
			const bool bSelected = ActiveParticleCurveChannelKey == Channel.Key && ActiveParticleCurveKeyIndex == KeyIndex;
			DrawList->AddRectFilled(ImVec2(P.x - 3.0f, P.y - 3.0f), ImVec2(P.x + 3.0f, P.y + 3.0f), bSelected ? ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) : Channel.Color);
		}
	}
	DrawList->PopClipRect();

	ImGui::EndChild();

	ImGui::EndGroup();
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}
