#include "Editor/UI/EditorMaterialWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/EditorRenderPipeline.h"
#include "Editor/UI/EditorMainPanel.h"
#include "Editor/Undo/EditorUndoSystem.h"

#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Asset/StaticMesh.h"
#include "Core/ResourceManager.h"
#include "Object/ObjectIterator.h"
#include "Math/Utils.h"
#include <algorithm>
#include <filesystem>

#include "ImGui/imgui.h"

namespace
{
ImU32 ToColorU32(const FVector& Color, float Alpha = 1.0f)
{
	return ImGui::GetColorU32(ImVec4(
		MathUtil::Clamp(Color.X, 0.0f, 1.0f),
		MathUtil::Clamp(Color.Y, 0.0f, 1.0f),
		MathUtil::Clamp(Color.Z, 0.0f, 1.0f),
		MathUtil::Clamp(Alpha, 0.0f, 1.0f)));
}

const UMaterial* ResolveBaseMaterial(UMaterialInterface* Material)
{
	if (const UMaterial* BaseMaterial = Cast<UMaterial>(Material))
	{
		return BaseMaterial;
	}
	if (const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
	{
		return Instance->Parent;
	}
	return nullptr;
}

void SyncEditableMaterialData(UMaterialInterface* Material, const FString& ParamName, const FMaterialParamValue& ParamValue)
{
	UMaterial* BaseMaterial = Cast<UMaterial>(Material);
	if (!BaseMaterial)
	{
		return;
	}

	if (ParamValue.Type == EMaterialParamType::Vector3 && std::holds_alternative<FVector>(ParamValue.Value))
	{
		const FVector Color = std::get<FVector>(ParamValue.Value);
		if (ParamName == "DiffuseColor")
		{
			BaseMaterial->MaterialData.DiffuseColor = Color;
		}
		else if (ParamName == "AmbientColor")
		{
			BaseMaterial->MaterialData.AmbientColor = Color;
		}
		else if (ParamName == "SpecularColor")
		{
			BaseMaterial->MaterialData.SpecularColor = Color;
		}
		else if (ParamName == "EmissiveColor")
		{
			BaseMaterial->MaterialData.EmissiveColor = Color;
		}
	}
	else if (ParamValue.Type == EMaterialParamType::Float && std::holds_alternative<float>(ParamValue.Value))
	{
		const float Value = std::get<float>(ParamValue.Value);
		if (ParamName == "Opacity")
		{
			BaseMaterial->MaterialData.Opacity = Value;
		}
		else if (ParamName == "Shininess")
		{
			BaseMaterial->MaterialData.Shininess = Value;
		}
	}
	else if (ParamValue.Type == EMaterialParamType::Bool && std::holds_alternative<bool>(ParamValue.Value))
	{
		const bool bValue = std::get<bool>(ParamValue.Value);
		if (ParamName == "bHasDiffuseMap")
		{
			BaseMaterial->MaterialData.bHasDiffuseTexture = bValue;
		}
		else if (ParamName == "bHasAmbientMap")
		{
			BaseMaterial->MaterialData.bHasAmbientTexture = bValue;
		}
		else if (ParamName == "bHasSpecularMap")
		{
			BaseMaterial->MaterialData.bHasSpecularTexture = bValue;
		}
		else if (ParamName == "bHasEmissiveMap")
		{
			BaseMaterial->MaterialData.bHasEmissiveTexture = bValue;
		}
		else if (ParamName == "bHasBumpMap")
		{
			BaseMaterial->MaterialData.bHasBumpTexture = bValue;
		}
	}
	else if (ParamValue.Type == EMaterialParamType::Texture && std::holds_alternative<UTexture*>(ParamValue.Value))
	{
		const UTexture* Texture = std::get<UTexture*>(ParamValue.Value);
		const FString TexturePath = Texture ? FPaths::Normalize(Texture->GetFilePath()) : FString();
		if (ParamName == "DiffuseMap")
		{
			BaseMaterial->MaterialData.DiffuseTexPath = TexturePath;
			BaseMaterial->MaterialData.bHasDiffuseTexture = !TexturePath.empty();
		}
		else if (ParamName == "AmbientMap")
		{
			BaseMaterial->MaterialData.AmbientTexPath = TexturePath;
			BaseMaterial->MaterialData.bHasAmbientTexture = !TexturePath.empty();
		}
		else if (ParamName == "SpecularMap")
		{
			BaseMaterial->MaterialData.SpecularTexPath = TexturePath;
			BaseMaterial->MaterialData.bHasSpecularTexture = !TexturePath.empty();
		}
		else if (ParamName == "EmissiveMap")
		{
			BaseMaterial->MaterialData.EmissiveTexPath = TexturePath;
			BaseMaterial->MaterialData.bHasEmissiveTexture = !TexturePath.empty();
		}
		else if (ParamName == "BumpMap")
		{
			BaseMaterial->MaterialData.BumpTexPath = TexturePath;
			BaseMaterial->MaterialData.bHasBumpTexture = !TexturePath.empty();
		}
	}
}

const char* ToSamplerTypeLabel(ESamplerType Type)
{
	switch (Type)
	{
	case ESamplerType::EST_Point: return "Point";
	case ESamplerType::EST_Linear: return "Linear";
	case ESamplerType::EST_Anisotropic: return "Anisotropic";
	case ESamplerType::EST_Shadow: return "Shadow";
	default: return "Unknown";
	}
}

const char* ToDepthStencilTypeLabel(EDepthStencilType Type)
{
	switch (Type)
	{
	case EDepthStencilType::Default: return "Default";
	case EDepthStencilType::DepthReadOnly: return "Depth Read Only";
	case EDepthStencilType::StencilWrite: return "Stencil Write";
	case EDepthStencilType::StencilWriteOnlyEqual: return "Stencil Write Only Equal";
	case EDepthStencilType::GizmoInside: return "Gizmo Inside";
	case EDepthStencilType::GizmoOutside: return "Gizmo Outside";
	case EDepthStencilType::AlwaysOnTop: return "Always On Top";
	default: return "Unknown";
	}
}

const char* ToBlendTypeLabel(EBlendType Type)
{
	switch (Type)
	{
	case EBlendType::Opaque: return "Opaque";
	case EBlendType::AlphaBlend: return "Alpha Blend";
	case EBlendType::NoColor: return "No Color";
	default: return "Unknown";
	}
}

const char* ToRasterizerTypeLabel(ERasterizerType Type)
{
	switch (Type)
	{
	case ERasterizerType::SolidBackCull: return "Solid Back Cull";
	case ERasterizerType::SolidFrontCull: return "Solid Front Cull";
	case ERasterizerType::SolidNoCull: return "Solid No Cull";
	case ERasterizerType::WireFrame: return "Wire Frame";
	case ERasterizerType::DepthView: return "Depth View";
	default: return "Unknown";
	}
}

bool DrawSamplerTypeCombo(const char* Label, ESamplerType& Type)
{
	const ESamplerType Values[] = {
		ESamplerType::EST_Point,
		ESamplerType::EST_Linear,
		ESamplerType::EST_Anisotropic,
		ESamplerType::EST_Shadow
	};

	bool bChanged = false;
	if (ImGui::BeginCombo(Label, ToSamplerTypeLabel(Type)))
	{
		for (ESamplerType Value : Values)
		{
			const bool bSelected = Type == Value;
			if (ImGui::Selectable(ToSamplerTypeLabel(Value), bSelected))
			{
				Type = Value;
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	return bChanged;
}

bool DrawDepthStencilTypeCombo(const char* Label, EDepthStencilType& Type)
{
	const EDepthStencilType Values[] = {
		EDepthStencilType::Default,
		EDepthStencilType::DepthReadOnly,
		EDepthStencilType::StencilWrite,
		EDepthStencilType::StencilWriteOnlyEqual,
		EDepthStencilType::GizmoInside,
		EDepthStencilType::GizmoOutside,
		EDepthStencilType::AlwaysOnTop
	};

	bool bChanged = false;
	if (ImGui::BeginCombo(Label, ToDepthStencilTypeLabel(Type)))
	{
		for (EDepthStencilType Value : Values)
		{
			const bool bSelected = Type == Value;
			if (ImGui::Selectable(ToDepthStencilTypeLabel(Value), bSelected))
			{
				Type = Value;
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	return bChanged;
}

bool DrawBlendTypeCombo(const char* Label, EBlendType& Type)
{
	const EBlendType Values[] = {
		EBlendType::Opaque,
		EBlendType::AlphaBlend,
		EBlendType::NoColor
	};

	bool bChanged = false;
	if (ImGui::BeginCombo(Label, ToBlendTypeLabel(Type)))
	{
		for (EBlendType Value : Values)
		{
			const bool bSelected = Type == Value;
			if (ImGui::Selectable(ToBlendTypeLabel(Value), bSelected))
			{
				Type = Value;
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	return bChanged;
}

bool DrawRasterizerTypeCombo(const char* Label, ERasterizerType& Type)
{
	const ERasterizerType Values[] = {
		ERasterizerType::SolidBackCull,
		ERasterizerType::SolidFrontCull,
		ERasterizerType::SolidNoCull,
		ERasterizerType::WireFrame,
		ERasterizerType::DepthView
	};

	bool bChanged = false;
	if (ImGui::BeginCombo(Label, ToRasterizerTypeLabel(Type)))
	{
		for (ERasterizerType Value : Values)
		{
			const bool bSelected = Type == Value;
			if (ImGui::Selectable(ToRasterizerTypeLabel(Value), bSelected))
			{
				Type = Value;
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	return bChanged;
}

const char* GetMaterialParamTypeName(EMaterialParamType Type)
{
	switch (Type)
	{
	case EMaterialParamType::Bool: return "Bool";
	case EMaterialParamType::Int: return "Int";
	case EMaterialParamType::UInt: return "UInt";
	case EMaterialParamType::Float: return "Float";
	case EMaterialParamType::Vector2: return "Vector2";
	case EMaterialParamType::Vector3: return "Vector3";
	case EMaterialParamType::Vector4: return "Vector4";
	case EMaterialParamType::Matrix4: return "Matrix4";
	case EMaterialParamType::Texture: return "Texture";
	default: return "Unknown";
	}
}

FString SanitizeAssetStem(FString Name)
{
	for (char& Ch : Name)
	{
		if (Ch == '\\' || Ch == '/' || Ch == ':' || Ch == '*' || Ch == '?' || Ch == '"' || Ch == '<' || Ch == '>' || Ch == '|')
		{
			Ch = '_';
		}
	}
	return Name.empty() ? "Material" : Name;
}

std::filesystem::path ResolveMaterialInstanceDirectory(const UMaterial* BaseMaterial)
{
	std::filesystem::path MatPath = std::filesystem::path(BaseMaterial ? FPaths::ToWide(BaseMaterial->GetFilePath()) : L"");
	if (!MatPath.empty() && MatPath.has_parent_path() && MatPath.extension() != L".mtl")
	{
		return MatPath.parent_path();
	}
	return std::filesystem::path("Asset") / "Material" / "Instances";
}

FString ResolveMaterialInstanceStem(const UMaterial* BaseMaterial)
{
	if (!BaseMaterial)
	{
		return "Material";
	}

	std::filesystem::path MatPath = std::filesystem::path(FPaths::ToWide(BaseMaterial->GetFilePath()));
	if (!MatPath.empty() && MatPath.has_stem() && MatPath.extension() != L".mtl")
	{
		return SanitizeAssetStem(FPaths::ToString(MatPath.stem().wstring()));
	}

	return SanitizeAssetStem(BaseMaterial->GetName());
}

void AddUniqueTexturePath(TArray<FString>& Paths, const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!NormalizedPath.empty() && std::find(Paths.begin(), Paths.end(), NormalizedPath) == Paths.end())
	{
		Paths.push_back(NormalizedPath);
	}
}

TArray<FString> BuildMaterialTexturePickerPaths(UEditorEngine* EditorEngine)
{
	TArray<FString> Paths;
	if (EditorEngine)
	{
		for (const FString& Path : EditorEngine->GetAssetService().GetTextureAssetPaths())
		{
			AddUniqueTexturePath(Paths, Path);
		}
	}

	for (const FString& Path : FResourceManager::Get().GetTextureFilePath())
	{
		AddUniqueTexturePath(Paths, Path);
	}

	for (TObjectIterator<UTexture> It; It; ++It)
	{
		if (UTexture* Texture = *It)
		{
			AddUniqueTexturePath(Paths, Texture->GetFilePath());
		}
	}

	std::sort(Paths.begin(), Paths.end());
	return Paths;
}

}

#define MAT_SEPARATOR() ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

void FEditorMaterialWidget::ResetSelection()
{
	SelectedMaterialPtr = nullptr;
	EditingSlotOwner = nullptr;
	EditingSlotIndex = -1;
	AssetEditingMaterialPtr = nullptr;
}

void FEditorMaterialWidget::OpenMaterialAsset(UMaterialInterface* Material)
{
	if (!Material)
	{
		return;
	}

	AssetEditingMaterialPtr = Material;
	EditingSlotOwner = nullptr;
	EditingSlotIndex = -1;
	SelectedMaterialPtr = Material;
	bFocusWindowNextFrame = true;
}

void FEditorMaterialWidget::OpenMaterialSlot(UPrimitiveComponent* PrimitiveComp, int32 SlotIndex)
{
	if (!PrimitiveComp || SlotIndex < 0 || SlotIndex >= PrimitiveComp->GetNumMaterials())
	{
		return;
	}

	AssetEditingMaterialPtr = nullptr;
	EditingSlotOwner = PrimitiveComp;
	EditingSlotIndex = SlotIndex;
	SelectedMaterialPtr = PrimitiveComp->GetMaterial(SlotIndex);
	bFocusWindowNextFrame = true;
}

void FEditorMaterialWidget::OnActorDestroyed(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (EditingSlotOwner && EditingSlotOwner->GetOwner() == Actor)
	{
		ResetSelection();
	}
}

void FEditorMaterialWidget::Render(float DeltaTime)
{
	ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f), ImGuiCond_Once);
	if (bFocusWindowNextFrame)
	{
		ImGui::SetNextWindowFocus();
		bFocusWindowNextFrame = false;
	}
    ImGui::Begin("Material Editor");

	if (EditingSlotOwner && EditingSlotIndex >= 0)
	{
		RefreshEditingMaterialFromSlot();
		RenderSingleMaterialEditor(EditingSlotOwner);
		ImGui::End();
		return;
	}

	if (AssetEditingMaterialPtr)
	{
		RenderAssetMaterialEditor();
		ImGui::End();
		return;
	}

	ImGui::TextDisabled("Open a material asset, or press Edit on a StaticMesh material slot.");
	
	ImGui::End();
}

void FEditorMaterialWidget::RenderAssetMaterialEditor()
{
	SelectedMaterialPtr = AssetEditingMaterialPtr;
	if (!SelectedMaterialPtr)
	{
		ImGui::TextDisabled("No material asset selected.");
		return;
	}

	ImGui::TextDisabled("Asset Material");
	ImGui::TextWrapped("%s", SelectedMaterialPtr->GetName().c_str());

	MAT_SEPARATOR();
	RenderMaterialPreviewSummary();
	RenderMaterialPreview(nullptr);
	MAT_SEPARATOR();
	RenderMaterialDetails(nullptr);
}

void FEditorMaterialWidget::RenderSingleMaterialEditor(UPrimitiveComponent* SlotOwnerComp)
{
	if (!SlotOwnerComp || EditingSlotIndex < 0 || EditingSlotIndex >= SlotOwnerComp->GetNumMaterials())
	{
		ImGui::TextDisabled("The edited material slot is no longer valid.");
		return;
	}

	ImGui::TextDisabled("StaticMesh Material Slot");
	ImGui::Text("Slot [%d]", EditingSlotIndex);
	ImGui::TextWrapped("%s", SelectedMaterialPtr ? SelectedMaterialPtr->GetName().c_str() : "(None)");

	MAT_SEPARATOR();
	RenderMaterialPreviewSummary();
	RenderMaterialPreview(SlotOwnerComp);
	MAT_SEPARATOR();
	RenderMaterialDetails(SlotOwnerComp);
}

void FEditorMaterialWidget::RenderMaterialDetails(UPrimitiveComponent* SlotOwnerComp)
{
	if (!SelectedMaterialPtr)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No material assigned. Assign one from the component Material Slot.");
		return;
	}

	if (ImGui::Button("Create Instance"))
	{
		CreateInstanceForCurrentMaterial();
	}

	RenderMaterialProperties();
}

void FEditorMaterialWidget::RefreshEditingMaterialFromSlot()
{
	if (!EditingSlotOwner || EditingSlotIndex < 0 || EditingSlotIndex >= EditingSlotOwner->GetNumMaterials())
	{
		SelectedMaterialPtr = nullptr;
		return;
	}

	SelectedMaterialPtr = EditingSlotOwner->GetMaterial(EditingSlotIndex);
}

bool FEditorMaterialWidget::CreateInstanceForCurrentMaterial()
{
	UMaterial* BaseMat = Cast<UMaterial>(SelectedMaterialPtr);
	if (!BaseMat)
	{
		if (SelectedMaterialPtr && SelectedMaterialPtr->IsA<UMaterialInstance>())
		{
			EditorEngine->GetNotificationService().Info("Material is already an instance");
		}
		return false;
	}

	const std::filesystem::path InstanceDir = ResolveMaterialInstanceDirectory(BaseMat);
	std::error_code Ec;
	std::filesystem::create_directories(InstanceDir, Ec);

	const FString PureName = ResolveMaterialInstanceStem(BaseMat);
	int32 Index = 0;
	std::filesystem::path FinalPath;
	do
	{
		const FString NewName = PureName + "_Inst_" + std::to_string(Index) + ".uasset";
		FinalPath = InstanceDir / NewName;
		Index++;
	} while (std::filesystem::exists(FinalPath));

	const FString InstancePath = FPaths::Normalize(FPaths::ToString(FinalPath.generic_wstring()));
	UMaterialInstance* NewInstance = EditorEngine->GetAssetService().CreateMaterialInstance(InstancePath, BaseMat);
	if (!NewInstance)
	{
		return false;
	}

	if (!EditorEngine->GetAssetService().SaveMaterialInstance(InstancePath, NewInstance))
	{
		return false;
	}
	FEditorFileSystemState CreatedInstanceState =
		EditorEngine->GetUndoSystem().CaptureFileSystemState(InstancePath, "Create Material Instance");
	EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(CreatedInstanceState, "Create Material Instance");

	SelectedMaterialPtr = NewInstance;
	if (EditingSlotOwner && EditingSlotIndex >= 0 && EditingSlotIndex < EditingSlotOwner->GetNumMaterials())
	{
		TArray<FEditorSerializedActorState> BeforeActorStates;
		if (AActor* OwnerActor = EditingSlotOwner->GetOwner())
		{
			TArray<AActor*> Actors;
			Actors.push_back(OwnerActor);
			BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(Actors);
		}

		EditingSlotOwner->SetMaterial(EditingSlotIndex, NewInstance);
		if (AActor* OwnerActor = EditingSlotOwner->GetOwner())
		{
			TArray<AActor*> Actors;
			Actors.push_back(OwnerActor);
			EditorEngine->GetUndoSystem().RecordActorStateChange(
				BeforeActorStates,
				EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
				"Assign Material");
		}
		EditorEngine->GetSceneService().MarkDirty();
	}
	else
	{
		AssetEditingMaterialPtr = NewInstance;
	}

	EditorEngine->GetNotificationService().Info("Material instance created");
	EditorEngine->GetAssetService().RefreshAssetDatabase();
	EditorEngine->GetMainPanel().RefreshContentBrowser();
	return true;
}

void FEditorMaterialWidget::CollectMaterialParamsFromStorage(UMaterialInterface* Material, TMap<FString, FMaterialParamValue>& OutParams)
{
	if (!Material)
	{
		return;
	}

	if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
	{
		if (Instance->Parent)
		{
			CollectMaterialParamsFromStorage(Instance->Parent, OutParams);
		}

		for (const auto& [ParamName, ParamValue] : Instance->OverridedParams)
		{
			OutParams[ParamName] = ParamValue;
		}
		return;
	}

	if (UMaterial* BaseMaterial = Cast<UMaterial>(Material))
	{
		for (const auto& [ParamName, ParamValue] : BaseMaterial->MaterialParams)
		{
			OutParams[ParamName] = ParamValue;
		}
	}
}

UStaticMesh* FEditorMaterialWidget::ResolvePreviewMesh(UPrimitiveComponent* PrimitiveComp)
{
	if (PreviewMesh == nullptr)
	{
		PreviewMesh = FResourceManager::Get().LoadStaticMesh("Asset\\Mesh\\PreviewSphere.uasset");
	}

	if (PreviewMesh && PreviewMesh->HasValidMeshData())
	{
		return PreviewMesh;
	}

	UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(PrimitiveComp);
	UStaticMesh* SelectedMesh = MeshComp ? MeshComp->GetStaticMesh() : nullptr;
	return (SelectedMesh && SelectedMesh->HasValidMeshData()) ? SelectedMesh : nullptr;
}

void FEditorMaterialWidget::RenderMaterialPreviewSummary()
{
	if (!SelectedMaterialPtr)
	{
		ImGui::TextDisabled("No material preview.");
		return;
	}

	const bool bInstance = SelectedMaterialPtr->IsA<UMaterialInstance>();
	ImGui::TextDisabled(bInstance ? "Editable Material Instance" : "Editable Material");
	if (!SelectedMaterialPtr->GetFilePath().empty())
	{
		ImGui::TextWrapped("%s", FPaths::Normalize(SelectedMaterialPtr->GetFilePath()).c_str());
	}

	const UMaterial* BaseMaterial = ResolveBaseMaterial(SelectedMaterialPtr);
	if (!BaseMaterial)
	{
		return;
	}

	const FMaterial& MaterialData = BaseMaterial->MaterialData;
	ImGui::Spacing();
	ImGui::TextDisabled("Material Colors");
	ImGui::ColorButton("Diffuse##MaterialSummary", ImVec4(MaterialData.DiffuseColor.X, MaterialData.DiffuseColor.Y, MaterialData.DiffuseColor.Z, MaterialData.Opacity), ImGuiColorEditFlags_NoTooltip, ImVec2(42.0f, 22.0f));
	ImGui::SameLine();
	ImGui::TextUnformatted("Diffuse");
	ImGui::ColorButton("Specular##MaterialSummary", ImVec4(MaterialData.SpecularColor.X, MaterialData.SpecularColor.Y, MaterialData.SpecularColor.Z, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(42.0f, 22.0f));
	ImGui::SameLine();
	ImGui::TextUnformatted("Specular");
	ImGui::ColorButton("Emissive##MaterialSummary", ImVec4(MaterialData.EmissiveColor.X, MaterialData.EmissiveColor.Y, MaterialData.EmissiveColor.Z, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(42.0f, 22.0f));
	ImGui::SameLine();
	ImGui::TextUnformatted("Emissive");
}

void FEditorMaterialWidget::RenderMaterialPreview(UPrimitiveComponent* PrimitiveComp)
{
	if (!SelectedMaterialPtr)
	{
		return;
	}

	const bool bInstance = SelectedMaterialPtr->IsA<UMaterialInstance>();
	ImGui::TextDisabled(bInstance ? "Editable Material Instance" : "Editable Material");
	const UMaterial* BaseMaterial = ResolveBaseMaterial(SelectedMaterialPtr);

	const float PreviewWidth = std::max(180.0f, ImGui::GetContentRegionAvail().x);
	const ImVec2 PreviewSize(PreviewWidth, 180.0f);
	UStaticMesh* Mesh = ResolvePreviewMesh(PrimitiveComp);
	ID3D11ShaderResourceView* PreviewSRV = nullptr;

	if (FEditorRenderPipeline* RenderPipeline = EditorEngine->GetEditorRenderPipeline())
	{
		PreviewSRV = RenderPipeline->RenderMaterialPreview(
			EditorEngine->GetRenderer(),
			Mesh,
			SelectedMaterialPtr,
			static_cast<uint32>(PreviewSize.x),
			static_cast<uint32>(PreviewSize.y),
			PreviewYawRad,
			PreviewPitchRad,
			PreviewDistance);
	}

	const ImVec2 PreviewMin = ImGui::GetCursorScreenPos();
	if (PreviewSRV)
	{
		ImGui::Image(reinterpret_cast<ImTextureID>(PreviewSRV), PreviewSize);
	}
	else
	{
		const ImVec2 PreviewMax(PreviewMin.x + PreviewSize.x, PreviewMin.y + PreviewSize.y);
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImU32 PreviewBg = ImGui::GetColorU32(ImVec4(0.09f, 0.10f, 0.12f, 1.0f));
		DrawList->AddRectFilled(PreviewMin, PreviewMax, PreviewBg, 6.0f);
		DrawList->AddRect(PreviewMin, PreviewMax, ImGui::GetColorU32(ImVec4(0.25f, 0.28f, 0.34f, 1.0f)), 6.0f);
		DrawList->AddText(ImVec2(PreviewMin.x + 12.0f, PreviewMin.y + 12.0f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), "3D preview unavailable.");
		ImGui::Dummy(PreviewSize);
	}

	if (ImGui::IsItemHovered())
	{
		const ImGuiIO& IO = ImGui::GetIO();
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			PreviewYawRad += IO.MouseDelta.x * 0.01f;
			PreviewPitchRad = MathUtil::Clamp(PreviewPitchRad + IO.MouseDelta.y * 0.01f, -1.35f, 1.35f);
		}
		if (IO.MouseWheel != 0.0f)
		{
			PreviewDistance = MathUtil::Clamp(PreviewDistance - IO.MouseWheel * 0.35f, 1.5f, 10.0f);
		}
	}

	const ImVec2 PreviewMax(PreviewMin.x + PreviewSize.x, PreviewMin.y + PreviewSize.y);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRect(PreviewMin, PreviewMax, ImGui::GetColorU32(ImVec4(0.25f, 0.28f, 0.34f, 1.0f)), 6.0f);

	if (BaseMaterial)
	{
		const FMaterial& MaterialData = BaseMaterial->MaterialData;
		const ImVec2 SwatchSize(34.0f, 18.0f);
		const ImVec2 SwatchMin(PreviewMin.x + 8.0f, PreviewMax.y - 28.0f);
		DrawList->AddRectFilled(SwatchMin, ImVec2(SwatchMin.x + SwatchSize.x, SwatchMin.y + SwatchSize.y),
			ToColorU32(MaterialData.DiffuseColor, MaterialData.Opacity), 3.0f);
		const ImVec2 SpecMin(SwatchMin.x + 42.0f, SwatchMin.y);
		DrawList->AddRectFilled(SpecMin, ImVec2(SpecMin.x + SwatchSize.x, SpecMin.y + SwatchSize.y),
			ToColorU32(MaterialData.SpecularColor), 3.0f);
		const ImVec2 EmissiveMin(SpecMin.x + 42.0f, SpecMin.y);
		DrawList->AddRectFilled(EmissiveMin, ImVec2(EmissiveMin.x + SwatchSize.x, EmissiveMin.y + SwatchSize.y),
			ToColorU32(MaterialData.EmissiveColor), 3.0f);
	}

	ImGui::TextDisabled("Drag preview to rotate the material sphere. Wheel zooms.");
}

void FEditorMaterialWidget::RenderMaterialProperties()
{
	TMap<FString, FMaterialParamValue> DisplayParams;
	CollectMaterialParamsFromStorage(SelectedMaterialPtr, DisplayParams);

	auto SaveSelectedMaterial = [this]()
	{
		if (!EditorEngine || !SelectedMaterialPtr)
		{
			return false;
		}
		if (UMaterialInstance* Instance = Cast<UMaterialInstance>(SelectedMaterialPtr))
		{
			return EditorEngine->GetAssetService().SaveMaterialInstance(SelectedMaterialPtr->GetFilePath(), Instance);
		}
		if (UMaterial* Material = Cast<UMaterial>(SelectedMaterialPtr))
		{
			return EditorEngine->GetAssetService().SaveMaterial(SelectedMaterialPtr->GetFilePath(), Material);
		}
		return false;
	};

	ImGui::TextDisabled("Render Policy");
	if (UMaterial* Material = Cast<UMaterial>(SelectedMaterialPtr))
	{
		bool bRenderPolicyChanged = false;
		bRenderPolicyChanged = DrawBlendTypeCombo("Blend Mode", Material->BlendType) || bRenderPolicyChanged;
		bRenderPolicyChanged = DrawDepthStencilTypeCombo("Depth State", Material->DepthStencilType) || bRenderPolicyChanged;
		bRenderPolicyChanged = DrawRasterizerTypeCombo("Rasterizer", Material->RasterizerType) || bRenderPolicyChanged;
		bRenderPolicyChanged = DrawSamplerTypeCombo("Sampler", Material->SamplerType) || bRenderPolicyChanged;

		if (bRenderPolicyChanged)
		{
			SaveSelectedMaterial();
			if (EditorEngine)
			{
				EditorEngine->GetMainPanel().RefreshContentBrowser();
				EditorEngine->GetNotificationService().Info("Material render policy saved");
			}
		}
	}
	else if (const UMaterial* BaseMaterial = ResolveBaseMaterial(SelectedMaterialPtr))
	{
		ImGui::Text("Blend Mode: %s", ToBlendTypeLabel(BaseMaterial->GetBlendType()));
		ImGui::Text("Depth State: %s", ToDepthStencilTypeLabel(BaseMaterial->GetDepthStencilType()));
		ImGui::Text("Rasterizer: %s", ToRasterizerTypeLabel(BaseMaterial->GetRasterizerType()));
		ImGui::Text("Sampler: %s", ToSamplerTypeLabel(BaseMaterial->GetSamplerType()));
		ImGui::TextDisabled("Material instances inherit render policy from their parent material.");
	}
	MAT_SEPARATOR();

	for (auto& [ParamName, ParamValue] : DisplayParams)
	{
		FEditorMaterialState BeforeState = EditorEngine
			? EditorEngine->GetUndoSystem().CaptureMaterialState(SelectedMaterialPtr, SelectedMaterialPtr->GetFilePath(), "Edit Material")
			: FEditorMaterialState();
		if (!RenderMaterialParamValue(ParamName, ParamValue))
		{
			continue;
		}

		SelectedMaterialPtr->SetParam(ParamName, ParamValue);
		SyncEditableMaterialData(SelectedMaterialPtr, ParamName, ParamValue);
		SaveSelectedMaterial();

		if (EditorEngine)
		{
			EditorEngine->GetUndoSystem().RecordMaterialState(
				BeforeState,
				EditorEngine->GetUndoSystem().CaptureMaterialState(SelectedMaterialPtr, SelectedMaterialPtr->GetFilePath(), "Edit Material"),
				"Edit Material");
			EditorEngine->GetMainPanel().RefreshContentBrowser();
		}
	}
}

bool FEditorMaterialWidget::RenderMaterialParamValue(const FString& ParamName, FMaterialParamValue& ParamValue)
{
	switch (ParamValue.Type)
	{
	case EMaterialParamType::Bool:
		return ImGui::Checkbox(ParamName.c_str(), &std::get<bool>(ParamValue.Value));
	case EMaterialParamType::Int:
		return ImGui::DragInt(ParamName.c_str(), &std::get<int32>(ParamValue.Value));
	case EMaterialParamType::UInt:
		return ImGui::DragInt(ParamName.c_str(), reinterpret_cast<int32*>(&std::get<uint32>(ParamValue.Value)));
	case EMaterialParamType::Float:
		return ImGui::DragFloat(ParamName.c_str(), &std::get<float>(ParamValue.Value), 0.01f);
	case EMaterialParamType::Vector2:
		return ImGui::DragFloat2(ParamName.c_str(), &std::get<FVector2>(ParamValue.Value).X, 0.01f);
	case EMaterialParamType::Vector3:
		return ImGui::DragFloat3(ParamName.c_str(), &std::get<FVector>(ParamValue.Value).X, 0.01f);
	case EMaterialParamType::Vector4:
		return ImGui::DragFloat4(ParamName.c_str(), &std::get<FVector4>(ParamValue.Value).X, 0.01f);
	case EMaterialParamType::Texture:
	{
		UTexture* CurrentTex = std::get<UTexture*>(ParamValue.Value);
		ID3D11ShaderResourceView* SRV = CurrentTex ? CurrentTex->GetSRV() : nullptr;
		if (SRV)
		{
			ImGui::ImageButton(ParamName.c_str(), reinterpret_cast<void*>(SRV), ImVec2(64, 64));
		}
		else
		{
			ImGui::Button(("None##TextureButton_" + ParamName).c_str(), ImVec2(64, 64));
		}
		ImGui::SameLine();

		ImGui::BeginGroup();
		ImGui::Text("%s", ParamName.c_str());
		const FString CurrentPath = CurrentTex ? FPaths::Normalize(CurrentTex->GetFilePath()) : FString();
		const FString CurrentLabel = CurrentPath.empty() ? FString("None") : CurrentPath;

		bool bChanged = false;
		ImGui::SetNextItemWidth(200.0f);
		FString ComboId = "##Combo_" + ParamName;
		if (ImGui::BeginCombo(ComboId.c_str(), CurrentLabel.c_str()))
		{
			const TArray<FString> TexturePaths = BuildMaterialTexturePickerPaths(EditorEngine);
			if (TexturePaths.empty())
			{
				ImGui::TextDisabled("No texture assets");
			}
			for (const FString& TexPath : TexturePaths)
			{
				const bool bSelected = (TexPath == CurrentPath);
				if (ImGui::Selectable(TexPath.c_str(), bSelected))
				{
					if (UTexture* Texture = FResourceManager::Get().LoadTexture(TexPath))
					{
						ParamValue.Value = Texture;
						bChanged = true;
					}
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::EndGroup();
		return bChanged;
	}
	default:
		return false;
	}
}
