#include "Editor/UI/EditorPropertyWidget.h"

#include "Editor/UI/ComponentMenuRegistry.h"
#include "Editor/EditorEngine.h"
#include "Editor/EditorRenderPipeline.h"
#include "Editor/Undo/EditorUndoSystem.h"
#include "ImGui/imgui.h"
#include "GameFramework/World.h"
#include "Core/AssetPathPolicy.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Particle/ParticleMeshEmitterInstance.h"
#include "Particle/ParticleMeshTypes.h"
#include "Particle/ParticleModuleBeamNoise.h"
#include "Particle/ParticleModuleBeamSource.h"
#include "Particle/ParticleModuleBeamTarget.h"
#include "Particle/ParticleModuleMeshRotationRate.h"
#include "Particle/ParticleModules.h"
#include "Particle/ParticleRendererProperties.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/Movement/RotatingMovementComponent.h"
#include "Component/FireballComponent.h"
#include "Component/Movement/ProjectileMovementComponent.h"
#include "Component/Movement/InterpToMovementComponent.h"
#include "Component/Movement/PursuitMovementComponent.h"
#include "Component/ActorSequenceComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Component/PostProcess/Light/PointLightComponent.h"
#include "Core/EditorResourcePaths.h"
#include "Core/DebugDetails.h"
#include "Core/PropertyTypes.h"
#include "Core/Reflection/ReflectionRegistry.h"
#include "Core/Paths.h"
#include "Core/Containers/Set.h"
#include "Core/Logging/Log.h"
#include "Math/Color.h"
#include "Math/Quat.h"
#include "Core/Guid.h"
#include "Core/ResourceManager.h"
#include "Render/Resource/Material.h"
#include "Asset/StaticMesh.h"
#include "Asset/SkeletalMesh.h"
#include "Animation/AnimGraphAsset.h"
#include "Animation/AnimLuaProgramAsset.h"
#include "Animation/AnimSequence.h"
#include "Object/FName.h"
#include "Object/Class.h"
#include "Object/Property.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <cstring>
#include "Component/HeightFogComponent.h"
#include "Selection/SelectionManager.h"
#include "Component/BoxComponent.h"
#include "Component/SphereComponent.h"
#include "Component/CapsuleComponent.h"
#include "Component/CameraComponent.h"
#include "Component/SpringArmComponent.h"
#include "Component/SoundComponent.h"
#include "Runtime/Script/ScriptManager.h"
#include <Runtime/Script/ScriptComponent.h>
#include <commdlg.h>
#include "Animation/AnimInstance.h"
#include "Animation/AnimationStateMachine.h"

#define SEPARATOR(); ImGui::Spacing(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); ImGui::Spacing();

namespace UIConstants
{
	constexpr float XButtonSize    = 20.0f;
	constexpr float MinScrollHeight = 50.0f;
}

namespace
{
	using FDetailsPerfClock = std::chrono::steady_clock;

	double DetailsPerfMs(FDetailsPerfClock::time_point Start, FDetailsPerfClock::time_point End)
	{
		return std::chrono::duration<double, std::milli>(End - Start).count();
	}

	const TArray<FString>& EmptyAssetNames()
	{
		static const TArray<FString> Empty;
		return Empty;
	}

	static bool DrawXButton(const char* id, float size = UIConstants::XButtonSize)
	{
		ImGui::PushID(id);

		ImVec2 pos = ImGui::GetCursorScreenPos();
		bool bClicked = ImGui::InvisibleButton("##xbtn", ImVec2(size, size));

		ImVec4 col = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
		if      (ImGui::IsItemActive())  col = ImVec4(0.9f, 0.1f, 0.1f, 1.0f);
		else if (ImGui::IsItemHovered()) col = ImVec4(0.8f, 0.2f, 0.2f, 0.8f);

		ImDrawList* dl = ImGui::GetWindowDrawList();

		// 호버/클릭 시 배경 원
		ImVec2 center(pos.x + size * 0.5f + 0.5f, pos.y + size * 0.5f + 0.5f);
		dl->AddCircleFilled(center, size * 0.5f, ImGui::ColorConvertFloat4ToU32(
			ImGui::IsItemActive()
				? ImVec4(0.9f, 0.1f, 0.1f, 1.0f)
				: ImVec4(0.8f, 0.2f, 0.2f, 0.8f)));

		// X 직접 그리기 (폰트 무관)
		float pad = size * 0.3f;
		ImU32 color = ImGui::ColorConvertFloat4ToU32(col);
		dl->AddLine(
			ImVec2(pos.x + pad,        pos.y + pad),
			ImVec2(pos.x + size - pad, pos.y + size - pad),
			color, 2.0f);
		dl->AddLine(
			ImVec2(pos.x + size - pad, pos.y + pad),
			ImVec2(pos.x + pad,        pos.y + size - pad),
			color, 2.0f);

		ImGui::PopID();
		return bClicked;
	}

	static void DrawDetailsSeparator()
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}

	static void DrawDetailsSectionLabel(const char* Label)
	{
		ImVec2 Pos = ImGui::GetCursorScreenPos();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImU32 Color = ImGui::GetColorU32(ImGuiCol_Text);
		DrawList->AddText(ImVec2(Pos.x + 0.75f, Pos.y), Color, Label);
		ImGui::TextUnformatted(Label);
	}

	static bool DrawDetailsCategoryHeader(const char* Label, bool bDefaultOpen = true)
	{
		if (!Label || Label[0] == '\0')
		{
			Label = "Default";
		}

		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		if (bDefaultOpen)
		{
			Flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 3.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.19f, 0.23f, 0.29f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.23f, 0.27f, 0.34f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.31f, 0.39f, 1.0f));
		const bool bOpen = ImGui::CollapsingHeader(Label, Flags);
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);
		return bOpen;
	}

	static const char* GetPropertyDisplayName(const FProperty& Prop)
	{
		return (Prop.DisplayName && Prop.DisplayName[0] != '\0') ? Prop.DisplayName : Prop.Name;
	}

	static FString GetPropertyCategoryLabel(const FProperty* Property)
	{
		return (Property && Property->Category && Property->Category[0] != '\0')
			? FString(Property->Category)
			: FString("Default");
	}

	static bool IsSkeletalMeshSectionProperty(const FProperty* Property)
	{
		return Property
			&& Property->Name
			&& (std::strcmp(Property->Name, "SkeletalMeshPath") == 0
				|| std::strcmp(Property->Name, "SkinningMode") == 0
				|| std::strcmp(Property->Name, "AnimationAssetPath") == 0
				|| std::strcmp(Property->Name, "AnimGraphAssetPath") == 0
				|| std::strcmp(Property->Name, "AnimationMode") == 0);
	}

	static bool ShouldRenderReflectedProperty(UObject* Object, const FProperty* Property)
	{
		if (!Object || !Property || !Property->Name)
		{
			return false;
		}

		if (const UParticleModuleCollision* CollisionModule = Cast<UParticleModuleCollision>(Object))
		{
			if (std::strcmp(Property->Name, "bUseParticleSizeAsRadius") == 0)
			{
				return CollisionModule->GetTraceMode() == EParticleCollisionTraceMode::Sphere;
			}
			if (std::strcmp(Property->Name, "CollisionRadius") == 0)
			{
				return CollisionModule->GetTraceMode() == EParticleCollisionTraceMode::Sphere
					&& !CollisionModule->IsUsingParticleSizeAsRadius();
			}
		}

		return true;
	}

	static bool TryNormalizeDroppedAssetPath(const ImGuiPayload* Payload, FString& OutPath)
	{
		if (!Payload || !Payload->Data || Payload->DataSize <= 0)
		{
			return false;
		}

		const FString PayloadPath = static_cast<const char*>(Payload->Data);
		const std::filesystem::path DroppedPath = FPaths::ToWide(PayloadPath);
		OutPath = DroppedPath.is_absolute()
			? FPaths::Normalize(FPaths::ToRelativeString(DroppedPath.wstring()))
			: FPaths::Normalize(PayloadPath);
		return !OutPath.empty();
	}

	static bool RenderAnimGraphAssetPathWidget(FString& Value, const char* Label, const TArray<FString>& Options)
	{
		bool bChanged = false;
		const char* Preview = Value.empty() ? "<None>" : Value.c_str();

		if (ImGui::BeginCombo(Label, Preview))
		{
			if (ImGui::Selectable("<None>", Value.empty()))
			{
				Value.clear();
				bChanged = true;
			}
			for (const FString& Path : Options)
			{
				const bool bSelected = Value == Path;
				if (ImGui::Selectable(Path.c_str(), bSelected))
				{
					Value = Path;
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("AnimGraphContentItem"))
			{
				if (Payload->Data && Payload->DataSize > 0)
				{
					const FString PayloadPath = static_cast<const char*>(Payload->Data);
					const std::filesystem::path DroppedPath = FPaths::ToWide(PayloadPath);
					Value = DroppedPath.is_absolute()
						? FPaths::Normalize(FPaths::ToRelativeString(DroppedPath.wstring()))
						: FPaths::Normalize(PayloadPath);
					bChanged = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		return bChanged;
	}

	static FString MakePropertyWidgetLabel(const FProperty& Prop)
	{
		const char* DisplayName = GetPropertyDisplayName(Prop);
		if (!DisplayName)
		{
			return "";
		}
		if (!Prop.Name || strcmp(DisplayName, Prop.Name) == 0)
		{
			return DisplayName;
		}
		return FString(DisplayName) + "##" + Prop.Name;
	}

	static void CollectEditableReflectedProperties(UObject* Object, TArray<const FProperty*>& OutProperties)
	{
		if (!Object || !Object->GetClass())
		{
			return;
		}

		TArray<const FProperty*> AllProperties;
		Object->GetClass()->GetAllProperties(AllProperties);
		for (const FProperty* Property : AllProperties)
		{
			if (!Property || !Property->Name || !Property->IsEditable())
			{
				continue;
			}
			OutProperties.push_back(Property);
		}
	}

	static int64 ReadEnumPropertyValue(const FProperty& Prop, const UObject* Object)
	{
		const void* ValuePtr = Prop.GetValuePtr(Object);
		if (!ValuePtr || !Prop.EnumMeta)
		{
			return 0;
		}

		switch (Prop.EnumMeta->Size)
		{
		case 1: return static_cast<int64>(*static_cast<const uint8*>(ValuePtr));
		case 2: return static_cast<int64>(*static_cast<const uint16*>(ValuePtr));
		case 4: return static_cast<int64>(*static_cast<const int32*>(ValuePtr));
		case 8: return static_cast<int64>(*static_cast<const int64*>(ValuePtr));
		default: return 0;
		}
	}

	static void WriteEnumPropertyValue(const FProperty& Prop, UObject* Object, int64 Value)
	{
		void* ValuePtr = Prop.GetValuePtr(Object);
		if (!ValuePtr || !Prop.EnumMeta)
		{
			return;
		}

		switch (Prop.EnumMeta->Size)
		{
		case 1: *static_cast<uint8*>(ValuePtr) = static_cast<uint8>(Value); break;
		case 2: *static_cast<uint16*>(ValuePtr) = static_cast<uint16>(Value); break;
		case 4: *static_cast<int32*>(ValuePtr) = static_cast<int32>(Value); break;
		case 8: *static_cast<int64*>(ValuePtr) = static_cast<int64>(Value); break;
		default: break;
		}
	}

	static bool IsLiveActor(AActor* Actor)
	{
		return Actor
			&& UObjectManager::Get().ContainsObject(Actor)
			&& !Actor->IsPendingKill();
	}

	static bool IsLiveComponent(UActorComponent* Component)
	{
		return Component && UObjectManager::Get().ContainsObject(Component);
	}

	// 컴포넌트 포인터를 ImGui PushID 용 문자열로 변환
	static void MakeXButtonId(char* OutBuf, size_t BufSize, const void* Ptr)
	{
		snprintf(OutBuf, BufSize, "xbtn_%p", Ptr);
	}

	static FString GetMovementComponentDisplayName(UMovementComponent* MoveComp)
	{
		if (!MoveComp) return "None";

		USceneComponent* UpdatedComp = MoveComp->GetUpdatedComponent();
		if (UpdatedComp)
		{
			FString TargetName = UpdatedComp->GetFName().ToString();
			if (TargetName.empty())
			{
				TargetName = UpdatedComp->GetClassName();
			}
			return FString("MC_") + TargetName;
		}

		// 대상이 없는 경우
		FString DefaultName = MoveComp->GetFName().ToString();
		if (DefaultName.empty())
		{
			DefaultName = MoveComp->GetClassName();
		}
		return DefaultName;
	}

	static FString MakeDefaultScriptName(const FString& SceneName, AActor* Actor)
	{
		FString ActorName = "Actor";
		FString ValidSceneName = SceneName.empty() ? "Default" : SceneName;

		if (Actor)
		{
			ActorName = Actor->GetClassName();
		}

		return ValidSceneName + "_" + ActorName;
	}

	static bool IsBlankString(const FString& Value)
	{
		return std::all_of(
			Value.begin(),
			Value.end(),
			[](unsigned char Ch)
			{
				return std::isspace(Ch) != 0;
			});
	}

	static FString MakeScriptReferenceFromPath(const FString& PathText)
	{
		if (PathText.empty())
		{
			return {};
		}

		std::filesystem::path ScriptPath(FPaths::ToWide(PathText));
		if (ScriptPath.is_absolute())
		{
			return FPaths::ToRelativeString(ScriptPath.lexically_normal().wstring());
		}
		return FPaths::Normalize(PathText);
	}

	static FString GetLuaScriptDisplayName(const FString& ScriptReference)
	{
		if (ScriptReference.empty())
		{
			return "<None>";
		}

		std::filesystem::path ScriptPath(FPaths::ToWide(ScriptReference));
		if (ScriptPath.has_filename())
		{
			return FPaths::ToUtf8(ScriptPath.filename().generic_wstring());
		}

		return ScriptReference;
	}

	static bool RenderLuaScriptComboWidget(FString& Value, const char* Label)
	{
		bool bChanged = false;
		const FString Preview = GetLuaScriptDisplayName(Value);
		if (ImGui::BeginCombo(Label, Preview.c_str()))
		{
			FScriptManager& ScriptManager = FScriptManager::Get();
			ScriptManager.RefreshLuaScriptFiles();

			TArray<FString> ScriptReferences;
			for (const auto& Pair : ScriptManager.GetScriptArray())
			{
				const FLuaScriptInfo& Info = Pair.second;
				if (!Info.ScriptPath.empty())
				{
					ScriptReferences.push_back(MakeScriptReferenceFromPath(FPaths::ToUtf8(Info.ScriptPath)));
				}
				else
				{
					ScriptReferences.push_back(Pair.first.ToString());
				}
			}

			std::sort(ScriptReferences.begin(), ScriptReferences.end());
			ScriptReferences.erase(
				std::unique(ScriptReferences.begin(), ScriptReferences.end()),
				ScriptReferences.end());

			const bool bNoneSelected = Value.empty();
			if (ImGui::Selectable("<None>", bNoneSelected))
			{
				Value.clear();
				bChanged = true;
			}
			if (bNoneSelected)
			{
				ImGui::SetItemDefaultFocus();
			}

			for (const FString& ScriptReference : ScriptReferences)
			{
				const FString DisplayName = GetLuaScriptDisplayName(ScriptReference);
				const bool bSelected = Value == ScriptReference
					|| GetLuaScriptDisplayName(Value) == DisplayName;

				if (ImGui::Selectable(DisplayName.c_str(), bSelected))
				{
					Value = ScriptReference;
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

	static bool PromptCreateScriptAs(UEditorEngine* EditorEngine, const FString& ScriptPathHint, FString& OutFilePath)
	{
		OutFilePath.clear();

		std::filesystem::path ScriptDir = (std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"Script").lexically_normal();
		std::error_code CreateDirEc;
		std::filesystem::create_directories(ScriptDir, CreateDirEc);

		std::filesystem::path HintPath(FPaths::ToWide(ScriptPathHint));
		if (HintPath.has_filename() && HintPath.extension() != L".lua")
		{
			HintPath.replace_extension(L".lua");
		}

		std::wstring FileName = HintPath.has_filename() ? HintPath.filename().wstring() : L"NewScript.lua";
		if (FileName.empty() || FileName == L".lua")
		{
			FileName = L"NewScript.lua";
		}

		std::filesystem::path InitialDir = ScriptDir;
		if (HintPath.has_parent_path())
		{
			std::filesystem::path CandidateDir = HintPath.is_absolute()
				? HintPath.parent_path()
				: (std::filesystem::path(FPaths::RootDir()) / HintPath.parent_path()).lexically_normal();
			std::error_code ExistsEc;
			if (std::filesystem::is_directory(CandidateDir, ExistsEc))
			{
				InitialDir = CandidateDir;
			}
		}

		WCHAR FileBuffer[MAX_PATH] = {};
		const std::wstring DefaultFile = (InitialDir / FileName).wstring();
		const std::wstring InitialDirString = InitialDir.wstring();
		wcsncpy_s(FileBuffer, MAX_PATH, DefaultFile.c_str(), _TRUNCATE);

		OPENFILENAMEW DialogDesc = {};
		DialogDesc.lStructSize = sizeof(DialogDesc);
		DialogDesc.hwndOwner = EditorEngine && EditorEngine->GetWindow() ? EditorEngine->GetWindow()->GetHWND() : nullptr;
		DialogDesc.lpstrFilter = L"Lua Script Files (*.lua)\0*.lua\0All Files (*.*)\0*.*\0";
		DialogDesc.lpstrFile = FileBuffer;
		DialogDesc.nMaxFile = MAX_PATH;
		DialogDesc.lpstrInitialDir = InitialDirString.c_str();
		DialogDesc.lpstrDefExt = L"lua";
		DialogDesc.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

		if (!GetSaveFileNameW(&DialogDesc))
		{
			return false;
		}

		std::filesystem::path SelectedPath(FileBuffer);
		if (SelectedPath.extension() != L".lua")
		{
			SelectedPath.replace_extension(L".lua");
		}
		OutFilePath = FPaths::ToUtf8(SelectedPath.lexically_normal().wstring());
		return true;
	}

	static void InitializeSpawnedComponentDefaults(UActorComponent* Component)
	{
		if (USubUVComponent* SubUV = Cast<USubUVComponent>(Component))
		{
			SubUV->SetSubUV(FName("Explosion"));
			SubUV->SetSpriteSize(2.0f, 2.0f);
			SubUV->SetFrameRate(30.f);
		}
		else if (UTextRenderComponent* Text = Cast<UTextRenderComponent>(Component))
		{
			Text->SetFont(FName("Default"));
			Text->SetText("TextRender");
		}
		else if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component))
		{
			Billboard->SetTextureName(FEditorResourcePaths::Icon("Pawn_64x.png"));
		}
		else if (USpringArmComponent* SpringArm = Cast<USpringArmComponent>(Component))
		{
			SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 1.6f));
		}
		else if (UHeightFogComponent* HeightFog = Cast<UHeightFogComponent>(Component))
		{
			HeightFog->SetFogDensity(0);
			HeightFog->SetFogInscatteringColor(FVector4(0.72f, 0.8f, 0.9f, 1.0f));
			HeightFog->SetHeightFalloff(0);
			HeightFog->SetFogHeight(0);
		}
	}

    static const FProperty* FindPropertyByName(const TArray<const FProperty*>& Properties, const char* Name)
	{
	    if (!Name)
	    {
			return nullptr;
	    }

		for (const FProperty* Property : Properties)
		{
		    if (Property && Property->Name && std::strcmp(Property->Name, Name) == 0)
		    {
				return Property;
		    }
		}

		return nullptr;
	}
}

void FEditorPropertyWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	ActorSequenceDetails.Initialize(EditorEngine, &bPropertyEditUndoCaptured);
}

void FEditorPropertyWidget::ResetSelection()
{
	SelectedComponent = nullptr;
	LastSelectedActor = nullptr;
	LockedDetailsActor = nullptr;
	bTransformFieldEditUndoCaptured = false;
	TransformFieldBeforeActorStates.clear();
	bSkeletalBonePoseEditUndoCaptured = false;
	SkeletalBonePoseEditComponent = nullptr;
	SkeletalBonePoseEditBoneIndex = -1;
	SkeletalBonePoseBeforeState = FEditorSkeletalBonePoseState();
	bDetailsLocked = false;
	bActorSelected = true;
}

void FEditorPropertyWidget::Render(float DeltaTime)
{
	LastDeltaTime = DeltaTime;

	ImGui::SetNextWindowSize(ImVec2(350.0f, 500.0f), ImGuiCond_Once);
	ImGui::Begin("Details");

	const FWorldContext* Ctx = EditorEngine->GetFocusedWorldContext();

	AActor* CurrentSelection = Ctx->SelectionManager->GetPrimarySelection();
	if (!IsLiveActor(CurrentSelection))
	{
		Ctx->SelectionManager->ClearSelection();
		CurrentSelection = nullptr;
	}

	if (bDetailsLocked && LockedDetailsActor)
	{
		UWorld* LockedWorld = IsLiveActor(LockedDetailsActor) ? LockedDetailsActor->GetFocusedWorld() : nullptr;
		bool bLockedActorAlive = false;
		if (LockedWorld)
		{
			const TArray<AActor*>& WorldActors = LockedWorld->GetActors();
			bLockedActorAlive = std::find(WorldActors.begin(), WorldActors.end(), LockedDetailsActor) != WorldActors.end();
		}
		if (!bLockedActorAlive)
		{
			LockedDetailsActor = nullptr;
			bDetailsLocked = false;
			SelectedComponent = nullptr;
			LastSelectedActor = nullptr;
			bActorSelected = true;
		}
	}

	AActor* PrimaryActor = (bDetailsLocked && LockedDetailsActor) ? LockedDetailsActor : CurrentSelection;
	RenderDetailsLockBar(CurrentSelection, PrimaryActor);

	if (!IsLiveActor(PrimaryActor))
	{
		SelectedComponent = nullptr;
		LastSelectedActor = nullptr;
		bActorSelected = true;
		ImGui::Text("No object selected.");
		ImGui::End();
		return;
	}

	UpdateSelectionState(PrimaryActor);

	const TArray<AActor*>& SelectedActors = Ctx->SelectionManager->GetSelectedActors();
	TArray<AActor*> LockedActorList;
	const TArray<AActor*>* DisplayActors = &SelectedActors;
	if (bDetailsLocked)
	{
		LockedActorList.push_back(PrimaryActor);
		DisplayActors = &LockedActorList;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& !ImGui::GetIO().WantTextInput
		&& ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		if (bActorSelected)
		{
			TArray<AActor*> ActorsToDelete = *DisplayActors;
			if (EditorEngine && EditorEngine->DeleteActors(ActorsToDelete) > 0)
			{
				SelectedComponent = nullptr;
				LastSelectedActor = nullptr;
				bActorSelected = true;
				ImGui::End();
				return;
			}
		}
		else
		{
			DeleteSelectedComponent(PrimaryActor);
		}
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& !ImGui::GetIO().WantTextInput
		&& ImGui::IsKeyPressed(ImGuiKey_F2, false))
	{
		if (!bActorSelected && SelectedComponent)
		{
			bFocusComponentNameNextFrame = true;
		}
		else
		{
			SelectActorForDetails();
			bFocusActorNameNextFrame = true;
		}
	}

	// 상단 액터 정보 및 컨트롤 영역
	RenderActorHeaderRegion(PrimaryActor, *DisplayActors);

	if (!bDetailsLocked && Ctx->SelectionManager->GetPrimarySelection() == nullptr)
	{
		ImGui::End();
		return;
	}

	// 컴포넌트 트리 영역
	SEPARATOR();
	RenderComponentTree(PrimaryActor);
	RenderDetailsContextMenu(PrimaryActor, *DisplayActors);

	// 디테일 프로퍼티 영역
	SEPARATOR();
	DrawDetailsSectionLabel("Details");
	DrawDetailsSeparator();

	float ScrollHeight = std::max(UIConstants::MinScrollHeight, ImGui::GetContentRegionAvail().y);
	ImGui::BeginChild("##Details", ImVec2(0, ScrollHeight), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
	{
		RenderDetails(PrimaryActor, *DisplayActors);
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)
			&& ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			bOpenDetailsContextMenu = true;
		}
	}
	ImGui::EndChild();

	ImGui::End();
}

void FEditorPropertyWidget::OnActorDestroyed(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (LockedDetailsActor == Actor)
	{
		LockedDetailsActor = nullptr;
		bDetailsLocked = false;
	}

	if (LastSelectedActor == Actor)
	{
		ResetSelection();
		return;
	}

	if (SelectedComponent && SelectedComponent->GetOwner() == Actor)
	{
		ResetSelection();
	}
}

void FEditorPropertyWidget::RenderDetailsLockBar(AActor* CurrentSelection, AActor* DisplayActor)
{
	ImGui::TextUnformatted("Inspector");
	ImGui::SameLine();

	const bool bCanLock = CurrentSelection != nullptr;
	ImGui::BeginDisabled(!bDetailsLocked && !bCanLock);
	if (ImGui::SmallButton(bDetailsLocked ? "Unlock" : "Lock"))
	{
		if (bDetailsLocked)
		{
			LockedDetailsActor = nullptr;
			bDetailsLocked = false;
			LastSelectedActor = nullptr;
			SelectedComponent = nullptr;
			bActorSelected = true;
		}
		else if (CurrentSelection)
		{
			LockedDetailsActor = CurrentSelection;
			bDetailsLocked = true;
			LastSelectedActor = nullptr;
			SelectedComponent = nullptr;
			bActorSelected = true;
		}
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (bDetailsLocked && DisplayActor)
	{
		FString LockedName = DisplayActor->GetFName().ToString();
		if (LockedName.empty()) LockedName = DisplayActor->GetClassName();
		ImGui::TextDisabled("Locked: %s", LockedName.c_str());
	}
	else
	{
		ImGui::TextDisabled("Unlocked");
	}

	DrawDetailsSeparator();
}

void FEditorPropertyWidget::UpdateSelectionState(AActor* PrimaryActor)
{
	UWorld* World = PrimaryActor->GetFocusedWorld();
	const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(World);

	if (PrimaryActor != LastSelectedActor)
	{
		SelectedComponent = nullptr;
		LastSelectedActor = PrimaryActor;
		bActorSelected = true;
	}

	if (!bDetailsLocked && Ctx->SelectionManager)
	{
		Ctx->SelectionManager->ValidateSelection();
		UActorComponent* ManagerComponent = Ctx->SelectionManager->GetSelectedComponent();
		if (IsLiveComponent(ManagerComponent) && ManagerComponent->GetOwner() == PrimaryActor)
		{
			SelectedComponent = ManagerComponent;
			bActorSelected = false;
		}
		else
		{
			SelectedComponent = nullptr;
			bActorSelected = true;
		}
	}
}

void FEditorPropertyWidget::SelectActorForDetails()
{
	const FWorldContext* Ctx = EditorEngine->GetFocusedWorldContext();
	bActorSelected = true;
	SelectedComponent = nullptr;
	if (Ctx->SelectionManager)
	{
		Ctx->SelectionManager->ClearComponentSelection();
	}
}

void FEditorPropertyWidget::SelectComponentForDetails(UActorComponent* Component)
{
	const FWorldContext* Ctx = EditorEngine->GetFocusedWorldContext();
	SelectedComponent = Component;
	bActorSelected = false;
	if (Ctx->SelectionManager)
	{
		Ctx->SelectionManager->SelectComponent(Component);
	}
}

AActor* FEditorPropertyWidget::ResolveActorStateUndoOwner(UObject* Object) const
{
	if (AActor* Actor = Cast<AActor>(Object))
	{
		return Actor;
	}

	if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		return Component->GetOwner();
	}

	return nullptr;
}

void FEditorPropertyWidget::BeginActorStatePropertyEdit(UObject* Object, const char* Label)
{
	if (bPropertyEditUndoCaptured || !EditorEngine)
	{
		return;
	}

	if (AActor* OwnerActor = ResolveActorStateUndoOwner(Object))
	{
		TArray<AActor*> Actors;
		Actors.push_back(OwnerActor);
		PropertyEditBeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(Actors);
		if (!PropertyEditBeforeActorStates.empty())
		{
			bPropertyEditUndoCaptured = true;
			bPropertyEditUsesActorState = true;
			PropertyEditTargetObject = Object;
			return;
		}
	}

	EditorEngine->GetUndoSystem().CaptureSnapshot(Label ? Label : "Edit Property");
	bPropertyEditUndoCaptured = true;
	bPropertyEditUsesActorState = false;
	PropertyEditTargetObject = Object;
}

void FEditorPropertyWidget::EndActorStatePropertyEdit(UObject* Object, const char* Label)
{
	if (!bPropertyEditUndoCaptured || !EditorEngine)
	{
		ResetPropertyEditUndoState();
		return;
	}

	if (bPropertyEditUsesActorState)
	{
		UObject* TargetObject = PropertyEditTargetObject ? PropertyEditTargetObject : Object;
		if (AActor* OwnerActor = ResolveActorStateUndoOwner(TargetObject))
		{
			TArray<AActor*> Actors;
			Actors.push_back(OwnerActor);
			EditorEngine->GetUndoSystem().RecordActorStateChange(
				PropertyEditBeforeActorStates,
				EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
				Label ? Label : "Edit Property");
		}
	}

	ResetPropertyEditUndoState();
}

void FEditorPropertyWidget::BeginReflectedPropertyEdit(UObject* Object, const FProperty& Property, const char* Label)
{
	if (bPropertyEditUndoCaptured || !EditorEngine)
	{
		return;
	}

	PropertyEditBeforeReflectedState = EditorEngine->GetUndoSystem().CaptureReflectedProperty(Object, Property);
	if (PropertyEditBeforeReflectedState.IsValid())
	{
		bPropertyEditUndoCaptured = true;
		bPropertyEditUsesReflectedProperty = true;
		PropertyEditTargetObject = Object;
		PropertyEditTargetProperty = &Property;
		return;
	}

	EditorEngine->GetUndoSystem().CaptureSnapshot(Label ? Label : "Edit Property");
	bPropertyEditUndoCaptured = true;
	bPropertyEditUsesReflectedProperty = false;
	PropertyEditTargetObject = Object;
	PropertyEditTargetProperty = &Property;
}

void FEditorPropertyWidget::EndReflectedPropertyEdit(UObject* Object, const FProperty& Property, const char* Label)
{
	if (!bPropertyEditUndoCaptured || !EditorEngine)
	{
		ResetPropertyEditUndoState();
		return;
	}

	if (bPropertyEditUsesReflectedProperty)
	{
		UObject* TargetObject = PropertyEditTargetObject ? PropertyEditTargetObject : Object;
		const FProperty* TargetProperty = PropertyEditTargetProperty ? PropertyEditTargetProperty : &Property;
		EditorEngine->GetUndoSystem().RecordReflectedProperty(
			PropertyEditBeforeReflectedState,
			EditorEngine->GetUndoSystem().CaptureReflectedProperty(TargetObject, *TargetProperty),
			Label ? Label : "Edit Property");
	}

	ResetPropertyEditUndoState();
}

void FEditorPropertyWidget::ResetPropertyEditUndoState()
{
	bPropertyEditUndoCaptured = false;
	bPropertyEditUsesActorState = false;
	bPropertyEditUsesReflectedProperty = false;
	PropertyEditTargetObject = nullptr;
	PropertyEditTargetProperty = nullptr;
	PropertyEditBeforeActorStates.clear();
	PropertyEditBeforeReflectedState = FEditorReflectedPropertyState();
}

void FEditorPropertyWidget::RenderActorHeaderRegion(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	const int32 SelectionCount = static_cast<int32>(SelectedActors.size());

	if (SelectionCount > 1)
	{
		RenderMultiSelectionHeader(PrimaryActor, SelectedActors, SelectionCount);
	}
	else
	{
		RenderSingleSelectionHeader(PrimaryActor);
	}
}

void FEditorPropertyWidget::RenderMultiSelectionHeader(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors, int32 SelectionCount)
{
	ImGui::Text("Class: %s", PrimaryActor->GetClassName());

	FString PrimaryName = PrimaryActor->GetFName().ToString();
	if (PrimaryName.empty()) PrimaryName = PrimaryActor->GetClassName();

	const bool bWasActorSelected = bActorSelected;
	if (bWasActorSelected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
	ImGui::Text("Name: %s (+%d)", PrimaryName.c_str(), SelectionCount - 1);
	if (bWasActorSelected) ImGui::PopStyleColor();

	if (ImGui::IsItemClicked())
	{
		SelectActorForDetails();
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		SelectActorForDetails();
		bOpenDetailsContextMenu = true;
	}
}

void FEditorPropertyWidget::RenderSingleSelectionHeader(AActor* PrimaryActor)
{
	const bool bWasActorSelected = bActorSelected;
	if (bWasActorSelected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
	ImGui::Text("Actor: %s", PrimaryActor->GetFName().ToString().c_str());
	if (ImGui::IsItemClicked())
	{
		SelectActorForDetails();
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		SelectActorForDetails();
		bOpenDetailsContextMenu = true;
	}
	if (bWasActorSelected) ImGui::PopStyleColor();
}

void FEditorPropertyWidget::RenderDetailsContextMenu(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	if (bOpenDetailsContextMenu)
	{
		ImGui::OpenPopup("##DetailsContextMenu");
		bOpenDetailsContextMenu = false;
	}

	if (!ImGui::BeginPopup("##DetailsContextMenu"))
	{
		return;
	}

	USceneComponent* AddAttachTarget = nullptr;
	if (!bActorSelected && PrimaryActor && SelectedComponent && SelectedComponent->GetOwner() == PrimaryActor)
	{
		AddAttachTarget = Cast<USceneComponent>(SelectedComponent);
	}

	if (AddAttachTarget && ImGui::BeginMenu("Add Component"))
	{
		if (UClass* ComponentClass = FComponentMenuRegistry::DrawSpawnableComponentClassMenu())
		{
			TArray<FEditorSerializedActorState> BeforeActorStates;
			if (EditorEngine)
			{
				TArray<AActor*> Actors;
				Actors.push_back(PrimaryActor);
				BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(Actors);
			}
			if (UActorComponent* NewComp = PrimaryActor->AddComponentByClass(ComponentClass))
			{
				InitializeSpawnedComponentDefaults(NewComp);
				AttachAndSelectNewComponent(PrimaryActor, NewComp, AddAttachTarget);
				if (EditorEngine)
				{
					TArray<AActor*> Actors;
					Actors.push_back(PrimaryActor);
					EditorEngine->GetUndoSystem().RecordActorStateChange(
						BeforeActorStates,
						EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
						"Add Component");
					EditorEngine->GetSceneService().MarkDirty();
				}
			}
		}
		ImGui::EndMenu();
	}

	DrawDetailsSeparator();
	if (bActorSelected)
	{
		ImGui::BeginDisabled(SelectedActors.empty());
		if (ImGui::MenuItem(SelectedActors.size() > 1 ? "Delete Actors" : "Delete Actor", "Del"))
		{
			TArray<AActor*> ActorsToDelete = SelectedActors;
			if (EditorEngine && EditorEngine->DeleteActors(ActorsToDelete) > 0)
			{
				SelectedComponent = nullptr;
				LastSelectedActor = nullptr;
				bActorSelected = true;
			}
		}
		ImGui::EndDisabled();
	}
	else
	{
		const bool bCanDeleteComponent = CanDeleteComponent(PrimaryActor, SelectedComponent);
		ImGui::BeginDisabled(!bCanDeleteComponent);
		if (ImGui::MenuItem("Delete Component", "Del"))
		{
			DeleteSelectedComponent(PrimaryActor);
		}
		ImGui::EndDisabled();
		if (!bCanDeleteComponent && PrimaryActor && SelectedComponent == PrimaryActor->GetRootComponent())
		{
			ImGui::TextDisabled("Root component cannot be deleted.");
		}
	}

	ImGui::EndPopup();
}

void FEditorPropertyWidget::RenderComponentTree(AActor* Actor)
{
	if (!IsLiveActor(Actor))
	{
		ImGui::TextDisabled("Selected actor is no longer available.");
		return;
	}

	DrawDetailsSectionLabel("Components");
	DrawDetailsSeparator();

	float TreeHeight = std::max(64.0f, ImGui::GetContentRegionAvail().y * 0.2f);

	// BeginChild를 호출하여 내부 스크롤이 가능한 Child Window를 생성합니다.
	ImGui::BeginChild("##ComponentTreeChild", ImVec2(0, TreeHeight), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

	USceneComponent* Root = Actor->GetRootComponent();
	FString ActorName = Actor->GetFName().ToString();
	if (ActorName.empty()) ActorName = Actor->GetClassName();

	ImGuiTreeNodeFlags ActorFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
	if (bActorSelected) ActorFlags |= ImGuiTreeNodeFlags_Selected;

	const bool bActorNodeOpen = ImGui::TreeNodeEx(Actor, ActorFlags, "%s (Instance)", ActorName.c_str());
	if (ImGui::IsItemClicked())
	{
		SelectActorForDetails();
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		SelectActorForDetails();
		bOpenDetailsContextMenu = true;
	}

	if (bActorNodeOpen)
	{
		if (Root)
		{
			RenderSceneComponentNode(Actor, Root);
		}

		// Non-scene ActorComponents 및 MovementComponent들 하단 출력
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			// SceneComponent는 위의 트리 렌더링에서 처리되었으므로 패스
			if (!IsLiveComponent(Comp) || Comp->IsA<USceneComponent>())
				continue;

			ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			if (!bActorSelected && SelectedComponent == Comp)
				Flags |= ImGuiTreeNodeFlags_Selected;

			// MovementComponent 일 때와 일반 컴포넌트 일 때의 출력 형식 분리
			if (UMovementComponent* MoveComp = Cast<UMovementComponent>(Comp))
			{
				FString MoveName = GetMovementComponentDisplayName(MoveComp);
				ImGui::TreeNodeEx(Comp, Flags, "%s", MoveName.c_str());

				// --- DRAG SOURCE (MovementComponent) ---
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("DND_MOVE_COMP", &Comp, sizeof(UActorComponent*));
					ImGui::Text("Moving %s", MoveName.c_str());
					ImGui::EndDragDropSource();
				}
			}
			else
			{
				FString Name = Comp->GetFName().ToString();
				ImGui::TreeNodeEx(Comp, Flags, "%s", Name.c_str());
			}

			if (ImGui::IsItemClicked())
			{
				SelectComponentForDetails(Comp);
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				SelectComponentForDetails(Comp);
				bOpenDetailsContextMenu = true;
			}

		}

		ImGui::TreePop();
	}

	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)
		&& !ImGui::IsAnyItemHovered()
		&& ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		SelectActorForDetails();
		bOpenDetailsContextMenu = true;
	}

	ImGui::EndChild();
}

void FEditorPropertyWidget::RenderSceneComponentNode(AActor* Actor, USceneComponent* Comp)
{
	if (!IsLiveActor(Actor) || !IsLiveComponent(Comp)) return;

	FString Name = Comp->GetFName().ToString();
	if (Name.empty()) Name = Comp->GetClassName();

	const auto& Children = Comp->GetChildren();

	bool bHasChildren = !Children.empty(); // 자식 무브먼트 체크 제거

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
	if (!bHasChildren) Flags |= ImGuiTreeNodeFlags_Leaf;
	if (!bActorSelected && SelectedComponent == Comp) Flags |= ImGuiTreeNodeFlags_Selected;

	bool bIsRoot = (Comp->GetParent() == nullptr);

	bool bOpen = ImGui::TreeNodeEx(
		Comp, Flags, "%s%s",
		Name.c_str(),
		bIsRoot ? " (Root)" : ""
	);

	// --- DRAG SOURCE (SceneComponent) ---
	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("DND_SCENE_COMP", &Comp, sizeof(USceneComponent*));
		ImGui::Text("Dragging %s", Name.c_str());
		ImGui::EndDragDropSource();
	}

	// --- DROP TARGET ---
	if (ImGui::BeginDragDropTarget())
	{
		// 1. SceneComponent를 SceneComponent에 드롭 (부착)
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("DND_SCENE_COMP"))
		{
			USceneComponent* DraggedComp = *(USceneComponent**)Payload->Data;
			// 자기 자신이나 자신의 조상에게 부착하는 것을 방지
			bool bIsAncestor = false;
			for (USceneComponent* P = Comp; P; P = P->GetParent())
			{
				if (P == DraggedComp) { bIsAncestor = true; break; }
			}

			if (DraggedComp && DraggedComp != Comp && !bIsAncestor)
			{
				TArray<FEditorSerializedActorState> BeforeActorStates;
				if (EditorEngine)
				{
					TArray<AActor*> Actors;
					Actors.push_back(Actor);
					BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(Actors);
				}
				DraggedComp->AttachToComponent(Comp);
				if (EditorEngine)
				{
					TArray<AActor*> Actors;
					Actors.push_back(Actor);
					EditorEngine->GetUndoSystem().RecordActorStateChange(
						BeforeActorStates,
						EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
						"Attach Component");
					EditorEngine->GetSceneService().MarkDirty();
				}
			}
		}
		// 2. MovementComponent를 SceneComponent에 드롭 (UpdatedComponent 설정)
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("DND_MOVE_COMP"))
		{
			UMovementComponent* DraggedMoveComp = *(UMovementComponent**)Payload->Data;
			if (DraggedMoveComp)
			{
				TArray<FEditorSerializedActorState> BeforeActorStates;
				if (EditorEngine)
				{
					TArray<AActor*> Actors;
					Actors.push_back(Actor);
					BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(Actors);
				}
				DraggedMoveComp->SetUpdatedComponent(Comp);
				if (EditorEngine)
				{
					TArray<AActor*> Actors;
					Actors.push_back(Actor);
					EditorEngine->GetUndoSystem().RecordActorStateChange(
						BeforeActorStates,
						EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
						"Set Updated Component");
					EditorEngine->GetSceneService().MarkDirty();
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::IsItemClicked())
	{
		SelectComponentForDetails(Comp);
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		SelectComponentForDetails(Comp);
		bOpenDetailsContextMenu = true;
	}

	if (bOpen)
	{
		for (USceneComponent* Child : Children)
		{
			if (IsLiveComponent(Child))
			{
				RenderSceneComponentNode(Actor, Child);
			}
		}

		ImGui::TreePop();
	}
}

bool FEditorPropertyWidget::CanDeleteComponent(AActor* Owner, UActorComponent* Component) const
{
	if (!Owner || !Component)
	{
		return false;
	}

	if (Component == Owner->GetRootComponent())
	{
		return false;
	}

	if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
	{
		for (UActorComponent* ActorComp : Owner->GetComponents())
		{
			if (UMovementComponent* MoveComp = Cast<UMovementComponent>(ActorComp))
			{
				if (MoveComp->GetUpdatedComponent() == SceneComp)
				{
					return false;
				}
			}
		}
	}

	return true;
}

void FEditorPropertyWidget::DeleteSelectedComponent(AActor* Owner)
{
	if (!CanDeleteComponent(Owner, SelectedComponent))
	{
		return;
	}

	UWorld* World = Owner->GetFocusedWorld();
	const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(World);

	UActorComponent* ComponentToDelete = SelectedComponent;
	TArray<FEditorSerializedActorState> BeforeActorStates;
	if (EditorEngine)
	{
		TArray<AActor*> Actors;
		Actors.push_back(Owner);
		BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(Actors);
	}
	SelectedComponent = nullptr;
	bActorSelected = true;
	if (Ctx->SelectionManager)
	{
		Ctx->SelectionManager->OnComponentDestroyed(ComponentToDelete);
	}
	Owner->RemoveComponent(ComponentToDelete);
	if (EditorEngine)
	{
		TArray<AActor*> Actors;
		Actors.push_back(Owner);
		EditorEngine->GetUndoSystem().RecordActorStateChange(
			BeforeActorStates,
			EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
			"Delete Component");
		EditorEngine->GetSceneService().MarkDirty();
	}
}

void FEditorPropertyWidget::RenderDetails(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	if (bActorSelected)
	{
		RenderActorProperties(PrimaryActor, SelectedActors);
	}
	else if (SelectedComponent)
	{
		RenderComponentProperties();
	}
	else
	{
		ImGui::TextDisabled("Select an actor or component to view details.");
	}
}

void FEditorPropertyWidget::RenderActorProperties(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	ImGui::Text("Actor: %s", PrimaryActor->GetClassName());
	RenderEditableName("Name##Actor", PrimaryActor, &bFocusActorNameNextFrame); // 편집 가능한 UI

	if (PrimaryActor->GetRootComponent())
	{
		DrawDetailsSeparator();
		if (DrawDetailsCategoryHeader("Transform"))
		{
			ImGui::Spacing();

		// FVector(위치, 회전, 크기)를 읽어서 Properties를 그려 주는 단순한 친구입니다.
		auto DrawTransformField = [&](const char* Label, FVector CurrentValue, auto ApplyFunc)
		{
			float Arr[3] = { CurrentValue.X, CurrentValue.Y, CurrentValue.Z };
			const bool bEdited = ImGui::DragFloat3(Label, Arr, 0.1f);
			if (ImGui::IsItemActivated() && EditorEngine && !bTransformFieldEditUndoCaptured)
			{
				TransformFieldBeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorTransforms(SelectedActors);
				bTransformFieldEditUndoCaptured = !TransformFieldBeforeActorStates.empty();
			}
			if (bEdited)
			{
				FVector Delta = FVector(Arr[0], Arr[1], Arr[2]) - CurrentValue;
				for (AActor* Actor : SelectedActors)
				{
					if (Actor) ApplyFunc(Actor, Delta);
				}

				UWorld* World = PrimaryActor->GetFocusedWorld();
				const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(World);
				Ctx->SelectionManager->GetGizmo()->UpdateGizmoTransform();
			}
			if (bTransformFieldEditUndoCaptured && ImGui::IsItemDeactivatedAfterEdit() && EditorEngine)
			{
				EditorEngine->GetUndoSystem().RecordActorTransforms(
					TransformFieldBeforeActorStates,
					EditorEngine->GetUndoSystem().CaptureActorTransforms(SelectedActors));
				TransformFieldBeforeActorStates.clear();
				bTransformFieldEditUndoCaptured = false;
			}
			else if (bTransformFieldEditUndoCaptured && ImGui::IsItemDeactivated())
			{
				TransformFieldBeforeActorStates.clear();
				bTransformFieldEditUndoCaptured = false;
			}
		};

		// Location, Rotation, Scale을 한 번에 그려줍니다.
		DrawTransformField("Location", PrimaryActor->GetActorLocation(), [](AActor* A, FVector D) { A->AddActorWorldOffset(D); });
		DrawTransformField("Rotation", PrimaryActor->GetActorRotation(), [](AActor* A, FVector D) { A->SetActorRotation(A->GetActorRotation() + D); });
		DrawTransformField("Scale",    PrimaryActor->GetActorScale(),    [](AActor* A, FVector D) { A->SetActorScale(A->GetActorScale() + D); });
		}
	}

	TArray<const FProperty*> ReflectedProperties;
	CollectEditableReflectedProperties(PrimaryActor, ReflectedProperties);
	if (!ReflectedProperties.empty())
	{
		RenderReflectionPropertiesByCategory(PrimaryActor, ReflectedProperties);
	}

	RenderDebugDetails(PrimaryActor, PrimaryActor, SelectedActors);
}

void FEditorPropertyWidget::RenderActorTags(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	if (!PrimaryActor)
	{
		return;
	}

	DrawDetailsSeparator();
	if (!DrawDetailsCategoryHeader("Actor Tags"))
	{
		return;
	}
	if (SelectedActors.size() > 1)
	{
		ImGui::TextDisabled("Tag edits apply to selected actors.");
	}

	const TArray<FString> Tags = PrimaryActor->GetTags();
	if (Tags.empty())
	{
		ImGui::TextDisabled("No tags.");
	}
	else
	{
		for (int32 TagIndex = 0; TagIndex < static_cast<int32>(Tags.size()); ++TagIndex)
		{
			const FString& Tag = Tags[TagIndex];
			ImGui::PushID(TagIndex);
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.76f, 0.84f, 1.0f));
			ImGui::TextUnformatted(Tag.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove"))
			{
				TArray<FEditorSerializedActorState> BeforeActorStates;
				if (EditorEngine)
				{
					BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(SelectedActors);
				}
				bool bChanged = false;
				for (AActor* Actor : SelectedActors)
				{
					if (Actor && Actor->HasTag(Tag))
					{
						Actor->RemoveTag(Tag);
						bChanged = true;
					}
				}
				if (bChanged && EditorEngine)
				{
					EditorEngine->GetUndoSystem().RecordActorStateChange(
						BeforeActorStates,
						EditorEngine->GetUndoSystem().CaptureActorStates(SelectedActors),
						"Remove Actor Tag");
					EditorEngine->GetSceneService().MarkDirty();
				}
			}
			ImGui::PopID();
		}
	}

	ImGui::Spacing();
	ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 58.0f));
	const bool bAddByEnter = ImGui::InputTextWithHint(
		"##NewActorTag",
		"New tag",
		NewActorTagBuffer,
		IM_ARRAYSIZE(NewActorTagBuffer),
		ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	const bool bAddByButton = ImGui::Button("Add", ImVec2(52.0f, 0.0f));

	if ((bAddByEnter || bAddByButton) && NewActorTagBuffer[0] != '\0')
	{
		const FString NewTag = NewActorTagBuffer;
		TArray<FEditorSerializedActorState> BeforeActorStates;
		if (EditorEngine)
		{
			BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(SelectedActors);
		}
		bool bChanged = false;
		for (AActor* Actor : SelectedActors)
		{
			if (Actor && !Actor->HasTag(NewTag))
			{
				Actor->AddTag(NewTag);
				bChanged = true;
			}
		}
		if (bChanged)
		{
			NewActorTagBuffer[0] = '\0';
			if (EditorEngine)
			{
				EditorEngine->GetUndoSystem().RecordActorStateChange(
					BeforeActorStates,
					EditorEngine->GetUndoSystem().CaptureActorStates(SelectedActors),
					"Add Actor Tag");
				EditorEngine->GetSceneService().MarkDirty();
			}
		}
	}
}

void FEditorPropertyWidget::RenderComponentTags(UActorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	const TArray<FString> Tags = Component->GetTags();
	if (Tags.empty())
	{
		ImGui::TextDisabled("No tags.");
	}
	else
	{
		for (int32 TagIndex = 0; TagIndex < static_cast<int32>(Tags.size()); ++TagIndex)
		{
			const FString& Tag = Tags[TagIndex];
			ImGui::PushID(TagIndex);
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.76f, 0.84f, 1.0f));
			ImGui::TextUnformatted(Tag.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove"))
			{
				if (Component->HasTag(Tag))
				{
					AActor* Owner = Component->GetOwner();
					TArray<FEditorSerializedActorState> BeforeActorStates;
					if (EditorEngine)
					{
						TArray<AActor*> Actors;
						Actors.push_back(Owner);
						BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(Actors);
					}
					Component->RemoveTag(Tag);
					if (EditorEngine)
					{
						TArray<AActor*> Actors;
						Actors.push_back(Owner);
						EditorEngine->GetUndoSystem().RecordActorStateChange(
							BeforeActorStates,
							EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
							"Remove Component Tag");
						EditorEngine->GetSceneService().MarkDirty();
					}
				}
			}
			ImGui::PopID();
		}
	}

	ImGui::Spacing();
	ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 58.0f));
	const bool bAddByEnter = ImGui::InputTextWithHint(
		"##NewComponentTag",
		"New tag",
		NewComponentTagBuffer,
		IM_ARRAYSIZE(NewComponentTagBuffer),
		ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	const bool bAddByButton = ImGui::Button("Add", ImVec2(52.0f, 0.0f));

	if ((bAddByEnter || bAddByButton) && NewComponentTagBuffer[0] != '\0')
	{
		const FString NewTag = NewComponentTagBuffer;
		if (!Component->HasTag(NewTag))
		{
			AActor* Owner = Component->GetOwner();
			TArray<FEditorSerializedActorState> BeforeActorStates;
			if (EditorEngine)
			{
				TArray<AActor*> Actors;
				Actors.push_back(Owner);
				BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(Actors);
			}
			Component->AddTag(NewTag);
			NewComponentTagBuffer[0] = '\0';
			if (EditorEngine)
			{
				TArray<AActor*> Actors;
				Actors.push_back(Owner);
				EditorEngine->GetUndoSystem().RecordActorStateChange(
					BeforeActorStates,
					EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
					"Add Component Tag");
				EditorEngine->GetSceneService().MarkDirty();
			}
		}
	}
}

void FEditorPropertyWidget::RenderComponentProperties()
{
	bDetailsPerfTraceFrame = false;
	if (SelectedComponent != LastDetailsPerfComponent)
	{
		LastDetailsPerfComponent = SelectedComponent;
		bDetailsPerfTraceFrame =
			SelectedComponent &&
			(Cast<UStaticMeshComponent>(SelectedComponent) || Cast<USkeletalMeshComponent>(SelectedComponent));
	}

	const FDetailsPerfClock::time_point TotalStart = bDetailsPerfTraceFrame ? FDetailsPerfClock::now() : FDetailsPerfClock::time_point{};

	ImGui::Text("Component: %s", SelectedComponent->GetClassName());
	RenderEditableName("Name##Component", SelectedComponent, &bFocusComponentNameNextFrame); // 편집 가능한 UI

	DrawDetailsSeparator();

	TArray<const FProperty*> ReflectedProperties;
	const FDetailsPerfClock::time_point PropertiesStart = bDetailsPerfTraceFrame ? FDetailsPerfClock::now() : FDetailsPerfClock::time_point{};
	CollectEditableReflectedProperties(SelectedComponent, ReflectedProperties);
	const FDetailsPerfClock::time_point PropertiesEnd = bDetailsPerfTraceFrame ? FDetailsPerfClock::now() : FDetailsPerfClock::time_point{};

	AActor* Owner = SelectedComponent->GetOwner();
	double PropertyWidgetMs = 0.0;
	int32 RenderedPropertyCount = 0;

	const bool bUseSkeletalMeshSection = Cast<USkeletalMeshComponent>(SelectedComponent) != nullptr;
	bool bRenderedSkeletalMeshSection = false;

	auto RenderCountedProperty = [&](UObject* OwnerObject, const FProperty* Property)
		{
			if (!OwnerObject || !ShouldRenderReflectedProperty(OwnerObject, Property))
			{
				return;
			}

			const FDetailsPerfClock::time_point PropStart = bDetailsPerfTraceFrame ? FDetailsPerfClock::now() : FDetailsPerfClock::time_point{};

			RenderReflectionProperty(FPropertyHandle{ OwnerObject, Property });
			++RenderedPropertyCount;

			if (bDetailsPerfTraceFrame)
			{
				PropertyWidgetMs += DetailsPerfMs(PropStart, FDetailsPerfClock::now());
			}
		};

	if (USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(SelectedComponent))
	{
		bRenderedSkeletalMeshSection = true;
		if (DrawDetailsCategoryHeader("Skeletal Mesh"))
		{
			ImGui::Spacing();

			if (const FProperty* Prop = FindPropertyByName(ReflectedProperties, "SkeletalMeshPath"))
			{
				RenderCountedProperty(SelectedComponent, Prop);
			}

			if (const FProperty* Prop = FindPropertyByName(ReflectedProperties, "SkinningMode"))
			{
				RenderCountedProperty(SelectedComponent, Prop);
			}

			if (const FProperty* Prop = FindPropertyByName(ReflectedProperties, "AnimationMode"))
			{
				RenderCountedProperty(SelectedComponent, Prop);
			}

			if (SkeletalComp->GetAnimationMode() == EAnimationMode::AnimationSingleNode)
			{
				if (const FProperty* Prop = FindPropertyByName(ReflectedProperties, "AnimationAssetPath"))
				{
					RenderCountedProperty(SelectedComponent, Prop);
				}

				if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(SkeletalComp->GetAnimInstance()))
				{
					TArray<const FProperty*> AnimProps;
					CollectEditableReflectedProperties(SingleNode, AnimProps);

					if (const FProperty* Prop = FindPropertyByName(AnimProps, "PlayRate"))
					{
						RenderCountedProperty(SingleNode, Prop);
					}

					if (const FProperty* Prop = FindPropertyByName(AnimProps, "bLooping"))
					{
						RenderCountedProperty(SingleNode, Prop);
					}

					if (const FProperty* Prop = FindPropertyByName(AnimProps, "bAutoPlay"))
					{
						RenderCountedProperty(SingleNode, Prop);
					}
				}
			}

			else if (SkeletalComp->GetAnimationMode() == EAnimationMode::AnimationGraph)
			{
				if (const FProperty* Prop = FindPropertyByName(ReflectedProperties, "AnimGraphAssetPath"))
				{
					RenderCountedProperty(SelectedComponent, Prop);
				}
			}
		}
	}

	if (bRenderedSkeletalMeshSection)
	{
		DrawDetailsSeparator();
	}

	// Cycle 11: UParticleSystemComponent 단독 검증 섹션.
	// cascade editor를 거치지 않고 main editor detail panel만으로 Sprite/Mesh emitter 렌더링 확인 가능.
	// Runtime factory buttons work even when no particle asset is selected from disk.
	if (UParticleSystemComponent* ParticleComp = Cast<UParticleSystemComponent>(SelectedComponent))
	{
		if (DrawDetailsCategoryHeader("Particle System"))
		{
			ImGui::Spacing();

		const UParticleSystem* CurrentTemplate = ParticleComp->GetTemplate();
		ImGui::Text("Template: %s", CurrentTemplate ? CurrentTemplate->GetFName().ToString().c_str() : "<None>");
		ImGui::Spacing();

		// Component-level opacity multiplier — runtime fade in/out. AlphaBlend emitter 에 한해 적용 (Builder 에서 분기).
		{
			float CompOpacity = ParticleComp->GetOpacityMultiplier();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			if (ImGui::SliderFloat("Opacity Multiplier", &CompOpacity, 0.0f, 1.0f, "%.3f"))
			{
				ParticleComp->SetOpacityMultiplier(CompOpacity);
			}
			ImGui::TextDisabled("  AlphaBlend 모드인 emitter 에만 적용 (Additive/Opaque 무시).");
			ImGui::Spacing();
		}

		if (ImGui::Button("Apply Sprite Demo Template"))
		{
			if (UParticleSystem* Demo = UParticleSystem::CreateDefaultSpriteSystem())
			{
				ParticleComp->SetTemplate(Demo, true);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Apply Mesh Demo Template"))
		{
			if (UParticleSystem* Demo = UParticleSystem::CreateDefaultMeshSystem())
			{
				ParticleComp->SetTemplate(Demo, true);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Apply Ribbon Demo Template"))
		{
			if (UParticleSystem* Demo = UParticleSystem::CreateDefaultRibbonSystem())
			{
				ParticleComp->SetTemplate(Demo, true);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Apply Beam Demo Template"))
		{
			if (UParticleSystem* Demo = UParticleSystem::CreateDefaultBeamSystem())
			{
				ParticleComp->SetTemplate(Demo, true);
			}
		}

		// 각 mesh emitter의 material override를 직접 picker로 변경.
		// 현재 Template 안의 모든 mesh renderer properties를 emitter 순서대로 노출 — 사용자가 emitter별로 다른 material 선택 가능.
		// bOverrideMaterial=true로 강제 + OverrideMaterial 세팅. 다음 frame부터 Builder가 자동 반영.
		if (UParticleSystem* MutableTemplate = const_cast<UParticleSystem*>(CurrentTemplate))
		{
			FEditorAssetService& AssetService = EditorEngine->GetAssetService();
			const TArray<FString>& MaterialNames = AssetService.GetMaterialInterfaceNames();

			const TArray<UParticleEmitter*>& Emitters = MutableTemplate->GetEmitters();
			for (int32 EmitterIdx = 0; EmitterIdx < static_cast<int32>(Emitters.size()); ++EmitterIdx)
			{
				UParticleEmitter* Emitter = Emitters[EmitterIdx];
				if (!Emitter) continue;

				UParticleMeshRendererProperties* MeshRenderer = nullptr;
				for (UParticleLODLevel* LOD : Emitter->LODLevels)
				{
					if (LOD)
					{
						if (!MeshRenderer)
						{
							MeshRenderer = Cast<UParticleMeshRendererProperties>(LOD->GetEffectiveRendererProperties());
						}
						if (MeshRenderer)
						{
							break;
						}
					}
				}
				if (!MeshRenderer) continue;

				ImGui::Spacing();
				ImGui::PushID(EmitterIdx);

				UMaterialInterface* Current = MeshRenderer->GetEffectiveMaterial();
				const FString CurrentLabel = Current
					? (Current->GetFilePath().empty() ? Current->GetName() : FPaths::Normalize(Current->GetFilePath()))
					: FString("None");

				char SectionLabel[128];
				snprintf(SectionLabel, sizeof(SectionLabel), "Emitter %d - Mesh Material", EmitterIdx);
				ImGui::TextUnformatted(SectionLabel);
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::BeginCombo("##MeshMaterialPicker", CurrentLabel.c_str()))
				{
					if (ImGui::Selectable("None", Current == nullptr))
					{
						MeshRenderer->SetOverrideMaterial(false, nullptr);
					}
					for (int32 MatIdx = 0; MatIdx < static_cast<int32>(MaterialNames.size()); ++MatIdx)
					{
						ImGui::PushID(MatIdx);
						const FString& MatLabel = MaterialNames[MatIdx].empty()
							? FString("<Unnamed Material>")
							: MaterialNames[MatIdx];
						const bool bSelected = Current && CurrentLabel == MatLabel;
						if (ImGui::Selectable(MatLabel.c_str(), bSelected))
						{
							if (UMaterialInterface* Picked = AssetService.ResolveMaterialInterfaceByIndex(MatIdx))
							{
								MeshRenderer->SetOverrideMaterial(true, Picked);
							}
						}
						if (bSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}

				// Cycle 14 (M1): Alignment 모드 selector — PSA_Velocity / PSA_FacingCameraPosition 2값.
				// 결정 17 옵션 B 채택 결과: 후속 cycle 에서 enum 값 추가만으로 확장 가능.
				ImGui::Spacing();
				if (MeshRenderer)
				{
					ImGui::TextUnformatted("Alignment");
					const char* AlignmentItems[] = { "Velocity", "Facing Camera Position" };
					const EMeshAlignment CurrentAlignment = MeshRenderer->GetAlignment();
					int32 AlignmentIdx = static_cast<int32>(CurrentAlignment);
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::Combo("##MeshAlignmentPicker", &AlignmentIdx, AlignmentItems, IM_ARRAYSIZE(AlignmentItems)))
					{
						const EMeshAlignment NewAlignment = static_cast<EMeshAlignment>(AlignmentIdx);
						MeshRenderer->SetAlignment(NewAlignment);
					}
				}

				// Cycle 14 (M2): RotationRate module 편집 — emitter LOD 에서 lookup.
				// 모듈이 attach 되어 있으면 RotRateMin / RotRateMax 인라인 편집.
				// 없으면 "Add Mesh RotationRate Module" 버튼으로 즉시 추가 (detail panel 만으로 Cycle 14 셋업 가능).
				UParticleModuleMeshRotationRate* RotRateMod = nullptr;
				UParticleLODLevel* MeshLOD0 = Emitter->GetLODLevel(0);
				if (MeshLOD0)
				{
					for (UParticleModule* Module : MeshLOD0->GetModules())
					{
						if (UParticleModuleMeshRotationRate* Found = Cast<UParticleModuleMeshRotationRate>(Module))
						{
							RotRateMod = Found;
							break;
						}
					}
				}
				ImGui::Spacing();
				if (RotRateMod)
				{
					ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
						"Mesh RotationRate Module: attached (Rotation accumulating)");

					// Inline edit (Beam Noise 의 DragFloat3 패턴 답습 [line 1959]).
					// 단위: 라디안/sec — `MeshParticle.hlsl:39` 의 sin/cos 입력 기준 일관.
					const float RotRateDragWidth = ImGui::GetContentRegionAvail().x * 0.5f;
					FVector RotMin = RotRateMod->GetRotRateMin();
					ImGui::SetNextItemWidth(RotRateDragWidth);
					if (ImGui::DragFloat3("RotRate Min (rad/s)", &RotMin.X, 0.05f, -20.0f, 20.0f))
					{
						RotRateMod->SetRotRateMin(RotMin);
					}
					FVector RotMax = RotRateMod->GetRotRateMax();
					ImGui::SetNextItemWidth(RotRateDragWidth);
					if (ImGui::DragFloat3("RotRate Max (rad/s)", &RotMax.X, 0.05f, -20.0f, 20.0f))
					{
						RotRateMod->SetRotRateMax(RotMax);
					}

					// Quick preset: 0 / Z 축 spin 1 rad/s / 모든 축 spin 1 rad/s.
					if (ImGui::SmallButton("Preset: 0"))
					{
						RotRateMod->SetRotRateMin(FVector::ZeroVector);
						RotRateMod->SetRotRateMax(FVector::ZeroVector);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Preset: Z spin"))
					{
						RotRateMod->SetRotRateMin(FVector(0.0f, 0.0f, 1.0f));
						RotRateMod->SetRotRateMax(FVector(0.0f, 0.0f, 1.0f));
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Preset: XYZ random"))
					{
						RotRateMod->SetRotRateMin(FVector(-2.0f, -2.0f, -2.0f));
						RotRateMod->SetRotRateMax(FVector(2.0f, 2.0f, 2.0f));
					}
					ImGui::TextDisabled("  Preset 변경 후 spawn 되는 새 particle 부터 반영 (기존 particle 의 RotRate 는 spawn 시점 값 유지).");
				}
				else if (MeshLOD0)
				{
					ImGui::TextDisabled("Mesh RotationRate Module: not attached");
					if (ImGui::Button("Add Mesh RotationRate Module"))
					{
						if (UParticleModuleMeshRotationRate* NewMod = MeshLOD0->AddModule<UParticleModuleMeshRotationRate>())
						{
							// Cycle 14 inspection 편의: spin 이 immediately 시각화되도록 default Z 축 1 rad/s.
							NewMod->SetRotRateMin(FVector(0.0f, 0.0f, 1.0f));
							NewMod->SetRotRateMax(FVector(0.0f, 0.0f, 1.0f));
						}
					}
				}

				ImGui::PopID();
			}
		}
		// 각 Ribbon emitter 의 Material + renderer 파라미터를 detail panel 에 노출.
		// Mesh picker 블록과 평행 구조 — emitter 순서대로 ribbon renderer properties 찾아 UI 렌더.
		if (UParticleSystem* MutableTemplateRibbon = const_cast<UParticleSystem*>(CurrentTemplate))
		{
			FEditorAssetService& AssetServiceRibbon = EditorEngine->GetAssetService();
			const TArray<FString>& MaterialNamesRibbon = AssetServiceRibbon.GetMaterialInterfaceNames();
			const TArray<UParticleEmitter*>& EmittersRibbon = MutableTemplateRibbon->GetEmitters();
			for (int32 EmitterIdx = 0; EmitterIdx < static_cast<int32>(EmittersRibbon.size()); ++EmitterIdx)
			{
				UParticleEmitter* Emitter = EmittersRibbon[EmitterIdx];
				if (!Emitter) continue;
				UParticleRibbonRendererProperties* RibbonRenderer = nullptr;
				for (UParticleLODLevel* LOD : Emitter->LODLevels)
				{
					if (LOD)
					{
						if (UParticleRibbonRendererProperties* Found = Cast<UParticleRibbonRendererProperties>(LOD->GetEffectiveRendererProperties()))
						{
							RibbonRenderer = Found;
							break;
						}
					}
				}
				if (!RibbonRenderer) continue;
				ImGui::Spacing();
				ImGui::PushID(EmitterIdx + 0x10000); // Mesh PushID 와 충돌 회피
				char SectionLabel[128];
				snprintf(SectionLabel, sizeof(SectionLabel), "Emitter %d - Ribbon Settings", EmitterIdx);
				ImGui::TextUnformatted(SectionLabel);
				UMaterialInterface* Current = RibbonRenderer->GetMaterial();
				const FString CurrentLabel = Current
					? (Current->GetFilePath().empty() ? Current->GetName() : FPaths::Normalize(Current->GetFilePath()))
					: FString("None");
				ImGui::TextUnformatted("Material");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::BeginCombo("##RibbonMaterialPicker", CurrentLabel.c_str()))
				{
					if (ImGui::Selectable("None", Current == nullptr))
					{
						RibbonRenderer->SetMaterial(nullptr);
					}
					for (int32 MatIdx = 0; MatIdx < static_cast<int32>(MaterialNamesRibbon.size()); ++MatIdx)
					{
						ImGui::PushID(MatIdx);
						const FString& MatLabel = MaterialNamesRibbon[MatIdx].empty()
							? FString("<Unnamed Material>")
							: MaterialNamesRibbon[MatIdx];
						const bool bSelected = Current && CurrentLabel == MatLabel;
						if (ImGui::Selectable(MatLabel.c_str(), bSelected))
						{
							if (UMaterialInterface* Picked = AssetServiceRibbon.ResolveMaterialInterfaceByIndex(MatIdx))
							{
								RibbonRenderer->SetMaterial(Picked);
							}
						}
						if (bSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				// DragInt / DragFloat 는 label 이 입력칸 오른쪽에 inline 표시 — width 를 절반으로 제한해 label 공간 확보.
				const float DragItemWidth = ImGui::GetContentRegionAvail().x * 0.5f;

				int32 MaxTrails = RibbonRenderer->GetMaxTrailCount();
				ImGui::SetNextItemWidth(DragItemWidth);
				if (ImGui::DragInt("Max Trail Count", &MaxTrails, 1.0f, 1, 64))
				{
					RibbonRenderer->SetMaxTrailCount(MaxTrails);
				}
				int32 MaxInTrail = RibbonRenderer->GetMaxParticleInTrailCount();
				ImGui::SetNextItemWidth(DragItemWidth);
				if (ImGui::DragInt("Max Particle In Trail", &MaxInTrail, 1.0f, 1, 1024))
				{
					RibbonRenderer->SetMaxParticleInTrailCount(MaxInTrail);
				}
				float TangentScalar = RibbonRenderer->GetTangentSpawningScalar();
				ImGui::SetNextItemWidth(DragItemWidth);
				if (ImGui::DragFloat("Tangent Spawning Scalar", &TangentScalar, 0.01f, 0.0f, 10.0f))
				{
					RibbonRenderer->SetTangentSpawningScalar(TangentScalar);
				}

				// Blend Mode picker — Sprite/Ribbon/Beam 공통 (Mesh 는 Material 의 BlendType 따름).
				// NoColor 는 특수용 — 사용자 선택에서 제외.
				ImGui::TextUnformatted("Blend Mode");
				const char* BlendItems[] = { "Opaque", "Alpha Blend", "Additive" };
				int32 BlendIdx = static_cast<int32>(RibbonRenderer->GetBlendType());
				if (BlendIdx < 0 || BlendIdx > 2) { BlendIdx = static_cast<int32>(EBlendType::AlphaBlend); }
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::Combo("##RibbonBlendPicker", &BlendIdx, BlendItems, IM_ARRAYSIZE(BlendItems)))
				{
					RibbonRenderer->SetBlendType(static_cast<EBlendType>(BlendIdx));
				}

				// Emitter-level opacity (asset default) — Component OpacityMultiplier 와 곱연산.
				float RibbonOpacity = RibbonRenderer->GetOpacity();
				ImGui::SetNextItemWidth(DragItemWidth);
				if (ImGui::SliderFloat("Opacity", &RibbonOpacity, 0.0f, 1.0f, "%.3f"))
				{
					RibbonRenderer->SetOpacity(RibbonOpacity);
				}

				ImGui::PopID();
			}
		}
		// Cycle 13a/13b: 각 Beam emitter 의 renderer properties + Source/Target/Noise 파라미터를 detail panel 에 노출.
		// Mesh/Ribbon 블록과 평행 구조 — emitter 순서대로 UParticleBeamRendererProperties 찾아 UI 렌더.
		// Source/Target Component picker 는 owner actor 의 USceneComponent 들을 enumerate (RenderObjectPtrWidget 의 default 패턴 답습).
		if (UParticleSystem* MutableTemplateBeam = const_cast<UParticleSystem*>(CurrentTemplate))
		{
			FEditorAssetService& AssetServiceBeam = EditorEngine->GetAssetService();
			const TArray<FString>& MaterialNamesBeam = AssetServiceBeam.GetMaterialInterfaceNames();
			const TArray<UParticleEmitter*>& EmittersBeam = MutableTemplateBeam->GetEmitters();

			// Source/Target Component picker 공용 helper — **world 전체** 의 모든 actor 의 USceneComponent enumerate.
			// Beam Target 은 보통 다른 actor 를 가리키는 use case (예: particle 에서 enemy actor mesh 로 lightning).
			// 따라서 같은-actor 한정 (RenderObjectPtrWidget 의 default 패턴) 으로는 부족 — world 전체 순회.
			// label 형식: "ActorName::ComponentName" — cross-actor 식별 명확.
			AActor* BeamOwnerActor = ParticleComp->GetOwner();
			auto FormatComponentLabel = [](USceneComponent* Comp) -> FString
			{
				if (!Comp) { return FString("None"); }
				AActor* OwnerActor = Comp->GetOwner();
				const FString CompName = Comp->GetFName().ToString();
				if (OwnerActor)
				{
					return OwnerActor->GetFName().ToString() + "::" + CompName;
				}
				return CompName;
			};
			auto RenderSceneComponentPicker = [&](const char* PickerId, USceneComponent* Current) -> USceneComponent*
			{
				const FString CurrentLabel = FormatComponentLabel(Current);
				USceneComponent* NewSelection = Current;
				if (ImGui::BeginCombo(PickerId, CurrentLabel.c_str()))
				{
					if (ImGui::Selectable("None", Current == nullptr))
					{
						NewSelection = nullptr;
					}
					// World 전체 순회 — BeamOwnerActor->GetFocusedWorld() 로 owner actor 가 속한 world 진입.
					UWorld* World = BeamOwnerActor ? BeamOwnerActor->GetFocusedWorld() : nullptr;
					if (World)
					{
						for (AActor* Actor : World->GetActors())
						{
							if (!Actor) { continue; }
							for (UActorComponent* Comp : Actor->GetComponents())
							{
								USceneComponent* SceneComp = Cast<USceneComponent>(Comp);
								if (!SceneComp) { continue; }
								const FString FullLabel = FormatComponentLabel(SceneComp);
								const bool bSelected = (SceneComp == Current);
								char SelectableId[256];
								snprintf(SelectableId, sizeof(SelectableId), "%s##%p", FullLabel.c_str(), static_cast<void*>(SceneComp));
								if (ImGui::Selectable(SelectableId, bSelected))
								{
									NewSelection = SceneComp;
								}
								if (bSelected)
								{
									ImGui::SetItemDefaultFocus();
								}
							}
						}
					}
					ImGui::EndCombo();
				}
				return NewSelection;
			};

			for (int32 EmitterIdx = 0; EmitterIdx < static_cast<int32>(EmittersBeam.size()); ++EmitterIdx)
			{
				UParticleEmitter* Emitter = EmittersBeam[EmitterIdx];
				if (!Emitter) continue;

				UParticleBeamRendererProperties* BeamRenderer = nullptr;
				UParticleModuleBeamSource* BeamSource = nullptr;
				UParticleModuleBeamTarget* BeamTarget = nullptr;
				UParticleModuleBeamNoise* BeamNoise = nullptr;
				for (UParticleLODLevel* LOD : Emitter->LODLevels)
				{
					if (!LOD) continue;
					if (!BeamRenderer) { BeamRenderer = Cast<UParticleBeamRendererProperties>(LOD->GetEffectiveRendererProperties()); }
					for (UParticleModule* Module : LOD->GetModules())
					{
						if (!BeamSource) { BeamSource = Cast<UParticleModuleBeamSource>(Module); }
						if (!BeamTarget) { BeamTarget = Cast<UParticleModuleBeamTarget>(Module); }
						if (!BeamNoise)  { BeamNoise  = Cast<UParticleModuleBeamNoise>(Module); }
					}
					if (BeamRenderer && BeamSource && BeamTarget && BeamNoise) { break; }
				}
				if (!BeamRenderer) continue;

				ImGui::Spacing();
				ImGui::PushID(EmitterIdx + 0x20000); // Mesh / Ribbon PushID 와 충돌 회피

				char SectionLabel[128];
				snprintf(SectionLabel, sizeof(SectionLabel), "Emitter %d - Beam Settings", EmitterIdx);
				ImGui::TextUnformatted(SectionLabel);

				// Material picker (Mesh / Ribbon 와 동일 패턴).
				UMaterialInterface* CurrentMat = BeamRenderer->GetMaterial();
				const FString CurrentMatLabel = CurrentMat
					? (CurrentMat->GetFilePath().empty() ? CurrentMat->GetName() : FPaths::Normalize(CurrentMat->GetFilePath()))
					: FString("None");
				ImGui::TextUnformatted("Material");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::BeginCombo("##BeamMaterialPicker", CurrentMatLabel.c_str()))
				{
					if (ImGui::Selectable("None", CurrentMat == nullptr))
					{
						BeamRenderer->SetMaterial(nullptr);
					}
					for (int32 MatIdx = 0; MatIdx < static_cast<int32>(MaterialNamesBeam.size()); ++MatIdx)
					{
						ImGui::PushID(MatIdx);
						const FString& MatLabel = MaterialNamesBeam[MatIdx].empty()
							? FString("<Unnamed Material>")
							: MaterialNamesBeam[MatIdx];
						const bool bSelected = CurrentMat && CurrentMatLabel == MatLabel;
						if (ImGui::Selectable(MatLabel.c_str(), bSelected))
						{
							if (UMaterialInterface* Picked = AssetServiceBeam.ResolveMaterialInterfaceByIndex(MatIdx))
							{
								BeamRenderer->SetMaterial(Picked);
							}
						}
						if (bSelected) { ImGui::SetItemDefaultFocus(); }
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}

				// UParticleBeamRendererProperties 의 수치 멤버들 — Mesh/Ribbon 패턴과 동일 (DragInt / DragFloat).
				const float DragItemWidth = ImGui::GetContentRegionAvail().x * 0.5f;

				int32 MaxBeams = BeamRenderer->GetMaxBeamCount();
				ImGui::SetNextItemWidth(DragItemWidth);
				if (ImGui::DragInt("Max Beam Count", &MaxBeams, 1.0f, 1, 64))
				{
					BeamRenderer->SetMaxBeamCount(MaxBeams);
				}

				int32 InterpPoints = BeamRenderer->GetInterpolationPoints();
				ImGui::SetNextItemWidth(DragItemWidth);
				if (ImGui::DragInt("Interpolation Points", &InterpPoints, 1.0f, 0, 64))
				{
					BeamRenderer->SetInterpolationPoints(InterpPoints);
				}

				float FallbackDist = BeamRenderer->GetFallbackDistance();
				ImGui::SetNextItemWidth(DragItemWidth);
				if (ImGui::DragFloat("Fallback Distance", &FallbackDist, 1.0f, 0.0f, 100000.0f))
				{
					BeamRenderer->SetFallbackDistance(FallbackDist);
				}

				float TexTile = BeamRenderer->GetTextureTile();
				ImGui::SetNextItemWidth(DragItemWidth);
				if (ImGui::DragFloat("Texture Tile", &TexTile, 0.1f, 0.0f, 100.0f))
				{
					BeamRenderer->SetTextureTile(TexTile);
				}

				float TexTileDist = BeamRenderer->GetTextureTileDistance();
				ImGui::SetNextItemWidth(DragItemWidth);
				if (ImGui::DragFloat("Texture Tile Distance", &TexTileDist, 1.0f, 0.0f, 100000.0f))
				{
					BeamRenderer->SetTextureTileDistance(TexTileDist);
				}

				// Blend Mode picker — Sprite/Ribbon/Beam 공통 (Mesh 는 Material 의 BlendType 따름).
				ImGui::TextUnformatted("Blend Mode");
				const char* BeamBlendItems[] = { "Opaque", "Alpha Blend", "Additive" };
				int32 BeamBlendIdx = static_cast<int32>(BeamRenderer->GetBlendType());
				if (BeamBlendIdx < 0 || BeamBlendIdx > 2) { BeamBlendIdx = static_cast<int32>(EBlendType::AlphaBlend); }
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::Combo("##BeamBlendPicker", &BeamBlendIdx, BeamBlendItems, IM_ARRAYSIZE(BeamBlendItems)))
				{
					BeamRenderer->SetBlendType(static_cast<EBlendType>(BeamBlendIdx));
				}

				// Emitter-level opacity (asset default) — Component OpacityMultiplier 와 곱연산.
				float BeamOpacity = BeamRenderer->GetOpacity();
				ImGui::SetNextItemWidth(DragItemWidth);
				if (ImGui::SliderFloat("Opacity", &BeamOpacity, 0.0f, 1.0f, "%.3f"))
				{
					BeamRenderer->SetOpacity(BeamOpacity);
				}

				// Source / Target Component picker — 모듈이 존재할 때만 노출 (모듈 없는 경우 fallback 사용).
				if (BeamSource)
				{
					ImGui::Spacing();
					ImGui::TextUnformatted("Source Component");
					ImGui::SetNextItemWidth(-1.0f);
					USceneComponent* NewSource = RenderSceneComponentPicker("##BeamSourceCompPicker", BeamSource->GetSourceComponent());
					if (NewSource != BeamSource->GetSourceComponent())
					{
						BeamSource->SetSourceComponent(NewSource);
					}

					bool bUseLocalSource = BeamSource->IsUseLocalSource();
					if (ImGui::Checkbox("Use Local Source", &bUseLocalSource))
					{
						BeamSource->SetUseLocalSource(bUseLocalSource);
					}

					FVector SourceLocalVec = BeamSource->GetSourceLocalVector();
					ImGui::SetNextItemWidth(DragItemWidth);
					if (ImGui::DragFloat3("Source Local Vector", &SourceLocalVec.X, 1.0f, -10000.0f, 10000.0f))
					{
						BeamSource->SetSourceLocalVector(SourceLocalVec);
					}
				}
				if (BeamTarget)
				{
					ImGui::Spacing();
					ImGui::TextUnformatted("Target Component");
					ImGui::SetNextItemWidth(-1.0f);
					USceneComponent* NewTarget = RenderSceneComponentPicker("##BeamTargetCompPicker", BeamTarget->GetTargetComponent());
					if (NewTarget != BeamTarget->GetTargetComponent())
					{
						BeamTarget->SetTargetComponent(NewTarget);
					}

					// Local target 옵션 (BuildVertexBuffer Target lookup priority 1).
					// true 면 TargetComponent 무시 + TargetLocalVector 사용 (emitter local space → world).
					bool bUseLocal = BeamTarget->IsUseLocalTarget();
					if (ImGui::Checkbox("Use Local Target", &bUseLocal))
					{
						BeamTarget->SetUseLocalTarget(bUseLocal);
					}

					FVector LocalVec = BeamTarget->GetTargetLocalVector();
					ImGui::SetNextItemWidth(DragItemWidth);
					if (ImGui::DragFloat3("Target Local Vector", &LocalVec.X, 1.0f, -10000.0f, 10000.0f))
					{
						BeamTarget->SetTargetLocalVector(LocalVec);
					}
				}

				// Cycle 13b: Noise 모듈 노출 — 모듈이 존재할 때만 (없으면 perturbation skip).
				if (BeamNoise)
				{
					ImGui::Spacing();
					ImGui::TextUnformatted("Noise");

					int32 Freq = BeamNoise->GetFrequency();
					ImGui::SetNextItemWidth(DragItemWidth);
					if (ImGui::DragInt("Frequency", &Freq, 1.0f, 1, 8))
					{
						BeamNoise->SetFrequency(Freq);
					}

					FVector Range = BeamNoise->GetNoiseRange();
					ImGui::SetNextItemWidth(DragItemWidth);
					if (ImGui::DragFloat3("Noise Range", &Range.X, 1.0f, 0.0f, 10000.0f))
					{
						BeamNoise->SetNoiseRange(Range);
					}

					bool bTargetNoise = BeamNoise->IsTargetNoise();
					if (ImGui::Checkbox("Target Noise", &bTargetNoise))
					{
						BeamNoise->SetTargetNoise(bTargetNoise);
					}

					bool bSmooth = BeamNoise->IsSmooth();
					if (ImGui::Checkbox("Smooth", &bSmooth))
					{
						BeamNoise->SetSmooth(bSmooth);
					}
				}

				ImGui::PopID();
			}
		}

		// Sprite emitter 의 BlendType 노출 — Ribbon/Beam 패턴과 동일 (emitter 순서대로 sprite renderer properties 찾아 UI 렌더).
		// Sprite 는 Material 이 RequiredModule 에 묶여있어 본 섹션은 BlendType picker 만 제공.
		if (UParticleSystem* MutableTemplateSprite = const_cast<UParticleSystem*>(CurrentTemplate))
		{
			const TArray<UParticleEmitter*>& EmittersSprite = MutableTemplateSprite->GetEmitters();
			for (int32 EmitterIdx = 0; EmitterIdx < static_cast<int32>(EmittersSprite.size()); ++EmitterIdx)
			{
				UParticleEmitter* Emitter = EmittersSprite[EmitterIdx];
				if (!Emitter) continue;

				UParticleSpriteRendererProperties* SpriteRenderer = nullptr;
				for (UParticleLODLevel* LOD : Emitter->LODLevels)
				{
					if (LOD)
					{
						if (UParticleSpriteRendererProperties* Found = Cast<UParticleSpriteRendererProperties>(LOD->GetEffectiveRendererProperties()))
						{
							SpriteRenderer = Found;
							break;
						}
					}
				}
				if (!SpriteRenderer) continue;

				ImGui::Spacing();
				ImGui::PushID(EmitterIdx + 0x30000); // Mesh / Ribbon / Beam PushID 와 충돌 회피

				char SectionLabel[128];
				snprintf(SectionLabel, sizeof(SectionLabel), "Emitter %d - Sprite Settings", EmitterIdx);
				ImGui::TextUnformatted(SectionLabel);

				ImGui::TextUnformatted("Blend Mode");
				const char* SpriteBlendItems[] = { "Opaque", "Alpha Blend", "Additive" };
				int32 SpriteBlendIdx = static_cast<int32>(SpriteRenderer->GetBlendType());
				if (SpriteBlendIdx < 0 || SpriteBlendIdx > 2) { SpriteBlendIdx = static_cast<int32>(EBlendType::AlphaBlend); }
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::Combo("##SpriteBlendPicker", &SpriteBlendIdx, SpriteBlendItems, IM_ARRAYSIZE(SpriteBlendItems)))
				{
					SpriteRenderer->SetBlendType(static_cast<EBlendType>(SpriteBlendIdx));
				}

				// Emitter-level opacity (asset default) — Component OpacityMultiplier 와 곱연산.
				float SpriteOpacity = SpriteRenderer->GetOpacity();
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
				if (ImGui::SliderFloat("Opacity", &SpriteOpacity, 0.0f, 1.0f, "%.3f"))
				{
					SpriteRenderer->SetOpacity(SpriteOpacity);
				}

				ImGui::PopID();
			}
		}

		// Cycle 14 (M1+M2) Runtime Status inspection 섹션.
		// 사용자가 cascade editor 를 거치지 않고 main panel detail 만으로 다음을 점검할 수 있도록:
		//   (A) Component-cached camera 상태 (결정 18 옵션 β 검증) — bCachedCameraValid + 4 vector 값
		//   (B) 모든 emitter instance 의 runtime 상태 — type, active count, stride
		//   (C) Mesh emitter 한정: alignment 모드, RotationRate module 부착 여부, 첫 particle 의 payload 값
		// 위험 12 (camera fallback) 가시화: bCachedCameraValid=false 시 warning 색.
		DrawDetailsSeparator();
		DrawDetailsSectionLabel("Particle Runtime Status (Cycle 14)");
		ImGui::Spacing();

		// (A) Component cached camera 상태.
		ImGui::TextUnformatted("Component Cached Camera (결정 18 옵션 β):");
		const bool bCamValid = ParticleComp->IsCachedCameraValid();
		if (bCamValid)
		{
			const FVector& CamPos = ParticleComp->GetCachedCameraPosition();
			const FVector& CamFwd = ParticleComp->GetCachedCameraForward();
			const FVector& CamUp = ParticleComp->GetCachedCameraUp();
			const FVector& CamRight = ParticleComp->GetCachedCameraRight();
			ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "  Cache Valid: Yes");
			ImGui::Text("  Position : (%.2f, %.2f, %.2f)", CamPos.X, CamPos.Y, CamPos.Z);
			ImGui::Text("  Forward  : (%.2f, %.2f, %.2f)", CamFwd.X, CamFwd.Y, CamFwd.Z);
			ImGui::Text("  Up       : (%.2f, %.2f, %.2f)", CamUp.X, CamUp.Y, CamUp.Z);
			ImGui::Text("  Right    : (%.2f, %.2f, %.2f)", CamRight.X, CamRight.Y, CamRight.Z);
		}
		else
		{
			// 첫 frame / 외부 호출 (EditorMainPanelDebug 등) 경로에서는 cache 미갱신 → PSA_FacingCameraPosition 이 PSA_Velocity 로 fallback.
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
				"  Cache Valid: No  (PSA_FacingCameraPosition will fallback to PSA_Velocity — 위험 12 방어)");
		}

		// (B) 모든 emitter instance 의 runtime 상태.
		ImGui::Spacing();
		ImGui::TextUnformatted("Emitter Instances:");
		const int32 NumEmittersStatus = ParticleComp->GetEmitterInstanceCount();
		if (NumEmittersStatus == 0)
		{
			ImGui::TextDisabled("  (no runtime emitter instances — set Template first)");
		}
		for (int32 StatusEmitterIdx = 0; StatusEmitterIdx < NumEmittersStatus; ++StatusEmitterIdx)
		{
			FParticleEmitterInstance* Instance = ParticleComp->GetEmitterInstance(StatusEmitterIdx);
			if (!Instance) continue;

			ImGui::PushID(StatusEmitterIdx + 0x40000); // Cycle 14 namespace ID

			UParticleLODLevel* StatusLOD = Instance->GetCurrentLODLevel();
			const EParticleEmitterRenderMode StatusRenderMode =
				StatusLOD ? StatusLOD->GetEffectiveRenderMode() : EParticleEmitterRenderMode::Sprite;
			const char* StatusRenderModeStr =
				StatusRenderMode == EParticleEmitterRenderMode::Sprite ? "Sprite" :
				StatusRenderMode == EParticleEmitterRenderMode::Mesh   ? "Mesh"   :
				StatusRenderMode == EParticleEmitterRenderMode::Ribbon ? "Ribbon" :
				StatusRenderMode == EParticleEmitterRenderMode::Beam   ? "Beam"   : "?";

			ImGui::Text("  [%d] Type=%s  Active=%d/%d  Stride=%dB",
				StatusEmitterIdx,
				StatusRenderModeStr,
				Instance->GetActiveParticleCount(),
				Instance->GetMaxActiveParticleCount(),
				Instance->GetParticleStride());

			// (C) Mesh emitter 한정 — alignment / RotationRate module / 첫 particle payload sample.
			if (StatusRenderMode == EParticleEmitterRenderMode::Mesh && StatusLOD)
			{
				const UParticleMeshRendererProperties* StatusMeshRenderer =
					Cast<UParticleMeshRendererProperties>(StatusLOD->GetEffectiveRendererProperties());
				if (StatusMeshRenderer)
				{
					const EMeshAlignment StatusAlign = StatusMeshRenderer->GetAlignment();
					const char* AlignStr =
						StatusAlign == EMeshAlignment::PSA_Velocity ? "PSA_Velocity" :
						StatusAlign == EMeshAlignment::PSA_FacingCameraPosition ? "PSA_FacingCameraPosition" : "?";
					ImGui::Text("        Alignment: %s", AlignStr);

					// camera invalid + PSA_FacingCameraPosition → effective fallback 안내.
					if (StatusAlign == EMeshAlignment::PSA_FacingCameraPosition && !bCamValid)
					{
						ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
							"        (camera invalid → effective alignment = PSA_Velocity)");
					}
				}

				// RotationRate module 부착 여부.
				bool bHasRotRate = false;
				for (UParticleModule* Module : StatusLOD->GetModules())
				{
					if (Cast<UParticleModuleMeshRotationRate>(Module))
					{
						bHasRotRate = true;
						break;
					}
				}
				if (bHasRotRate)
				{
					ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "        RotationRate Module: attached");
				}
				else
				{
					ImGui::TextDisabled("        RotationRate Module: not attached (Rotation = 0)");
				}

				// 첫 active particle 의 payload sample — Spawn 시 셋팅된 RotRate + Update 누적된 Rotation 확인.
				FParticleMeshEmitterInstance* MeshInst = dynamic_cast<FParticleMeshEmitterInstance*>(Instance);
				if (MeshInst && MeshInst->GetActiveParticleCount() > 0)
				{
					if (FMeshRotationPayload* Payload = MeshInst->GetMeshPayloadAt(0))
					{
						ImGui::Text("        Sample[0] Rotation = (%.3f, %.3f, %.3f) rad",
							Payload->Rotation.X, Payload->Rotation.Y, Payload->Rotation.Z);
						ImGui::Text("        Sample[0] RotRate  = (%.3f, %.3f, %.3f) rad/s",
							Payload->RotRate.X, Payload->RotRate.Y, Payload->RotRate.Z);
					}
				}
			}

			ImGui::PopID();
		}

		}
		DrawDetailsSeparator();
	}

	if (DrawDetailsCategoryHeader("Component Tags"))
	{
		ImGui::Spacing();
		RenderComponentTags(SelectedComponent);
	}

	TMap<FString, TArray<const FProperty*>> PropertiesByCategory;
	TArray<FString> CategoryOrder;
	for (const FProperty* Property : ReflectedProperties)
	{
		if (!Property || !Property->Name || strcmp(Property->Name, "Tags") == 0)
		{
			continue;
		}
		if (!ShouldRenderReflectedProperty(SelectedComponent, Property))
		{
			continue;
		}

		if (bUseSkeletalMeshSection && IsSkeletalMeshSectionProperty(Property))
		{
			continue;
		}

		const FString Category = GetPropertyCategoryLabel(Property);
		if (PropertiesByCategory.find(Category) == PropertiesByCategory.end())
		{
			PropertiesByCategory[Category] = {};
			CategoryOrder.push_back(Category);
		}
		PropertiesByCategory[Category].push_back(Property);
	}

	for (const FString& Category : CategoryOrder)
	{
		DrawDetailsSeparator();
		ImGui::PushID(SelectedComponent);
		ImGui::PushID(Category.c_str());
		const FString HeaderLabel = Category + "##PropertyCategory";
		if (DrawDetailsCategoryHeader(HeaderLabel.c_str()))
		{
			ImGui::Spacing();
			for (const FProperty* Property : PropertiesByCategory[Category])
			{
				const FDetailsPerfClock::time_point PropStart = bDetailsPerfTraceFrame ? FDetailsPerfClock::now() : FDetailsPerfClock::time_point{};
				RenderReflectionProperty(FPropertyHandle{ SelectedComponent, Property });
				++RenderedPropertyCount;
				if (bDetailsPerfTraceFrame)
				{
					PropertyWidgetMs += DetailsPerfMs(PropStart, FDetailsPerfClock::now());
				}
			}
		}
		ImGui::PopID();
		ImGui::PopID();
	}

	double SkeletalDebugMs = 0.0;
	RenderDebugDetails(SelectedComponent, Owner, TArray<AActor*>{ Owner });

	// 프로퍼티 직접 편집 후 월드 행렬 갱신
	if (SelectedComponent->IsA<USceneComponent>())
	{
		UWorld* World = Owner->GetFocusedWorld();
		const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(World);
		static_cast<USceneComponent*>(SelectedComponent)->MarkTransformDirty();
		Ctx->SelectionManager->GetGizmo()->UpdateGizmoTransform();
	}

	if (bDetailsPerfTraceFrame)
	{
		const double CollectPropertiesMs = DetailsPerfMs(PropertiesStart, PropertiesEnd);
		const double TotalMs = DetailsPerfMs(TotalStart, FDetailsPerfClock::now());
		UE_LOG(
			"[DetailsPerf] Component=%s Type=%s Total=%.2fms CollectProperties=%.2fms PropertyWidgets=%.2fms SkeletalDebug=%.2fms Props=%d",
			SelectedComponent ? SelectedComponent->GetFName().ToString().c_str() : "<None>",
			SelectedComponent ? SelectedComponent->GetClassName() : "<None>",
			TotalMs,
			CollectPropertiesMs,
			PropertyWidgetMs,
			SkeletalDebugMs,
			RenderedPropertyCount);
	}
}

void FEditorPropertyWidget::RenderReflectionProperties(UObject* Object)
{
	TArray<const FProperty*> Properties;
	CollectEditableReflectedProperties(Object, Properties);
	RenderReflectionPropertiesByCategory(Object, Properties);
}

void FEditorPropertyWidget::RenderReflectionPropertiesByCategory(UObject* Object, const TArray<const FProperty*>& Properties)
{
	if (!Object)
	{
		return;
	}

	TMap<FString, TArray<const FProperty*>> PropertiesByCategory;
	TArray<FString> CategoryOrder;

	for (const FProperty* Property : Properties)
	{
		if (!ShouldRenderReflectedProperty(Object, Property))
		{
			continue;
		}

		const FString Category = GetPropertyCategoryLabel(Property);
		if (PropertiesByCategory.find(Category) == PropertiesByCategory.end())
		{
			PropertiesByCategory[Category] = {};
			CategoryOrder.push_back(Category);
		}
		PropertiesByCategory[Category].push_back(Property);
	}

	for (const FString& Category : CategoryOrder)
	{
		DrawDetailsSeparator();
		ImGui::PushID(Object);
		ImGui::PushID(Category.c_str());
		const FString HeaderLabel = Category + "##PropertyCategory";
		if (DrawDetailsCategoryHeader(HeaderLabel.c_str()))
		{
			ImGui::Spacing();
			for (const FProperty* Property : PropertiesByCategory[Category])
			{
				RenderReflectionProperty(FPropertyHandle{ Object, Property });
			}
		}
		ImGui::PopID();
		ImGui::PopID();
	}
}

void FEditorPropertyWidget::RenderReflectionProperty(const FPropertyHandle& Handle)
{
	if (!Handle.IsValid() || !Handle.IsEditable())
	{
		return;
	}

	RenderPropertyWidget(Handle);
}

void FEditorPropertyWidget::RenderDebugDetails(UObject* Object, AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	(void)PrimaryActor;
	if (!Object)
	{
		return;
	}

	FDebugDetailsBuilder Builder;
	Object->BuildDebugDetails(Builder);

	if (AActor* Actor = Cast<AActor>(Object))
	{
		Builder.AddTagEditor("Actor Tags", [this, Actor, &SelectedActors]()
		{
			RenderActorTags(Actor, SelectedActors);
		});

		if (UBillboardComponent* BillboardComp = Cast<UBillboardComponent>(Actor->GetRootComponent()))
		{
			if (!Cast<USubUVComponent>(Actor->GetRootComponent()))
			{
				Builder.AddCustom([this, BillboardComp, &SelectedActors]()
				{
					DrawDetailsSeparator();
					DrawDetailsSectionLabel("Sprite Texture");

					const TArray<FString>& TextureList = EditorEngine
						? EditorEngine->GetAssetService().GetTextureAssetPaths()
						: EmptyAssetNames();
					const FString CurrentName = BillboardComp->GetTextureName();

					if (ImGui::BeginCombo("##SpriteTexture", CurrentName.empty() ? "None" : CurrentName.c_str()))
					{
						for (const FString& TexPath : TextureList)
						{
							const bool bSelected = TexPath == CurrentName;
							if (ImGui::Selectable(TexPath.c_str(), bSelected))
							{
								TArray<FEditorSerializedActorState> BeforeActorStates;
								if (EditorEngine)
								{
									BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(SelectedActors);
								}
								for (AActor* SelectedActor : SelectedActors)
								{
									if (UBillboardComponent* Comp = SelectedActor
										? Cast<UBillboardComponent>(SelectedActor->GetRootComponent())
										: nullptr)
									{
										Comp->SetTextureName(TexPath);
									}
								}
								if (EditorEngine)
								{
									EditorEngine->GetUndoSystem().RecordActorStateChange(
										BeforeActorStates,
										EditorEngine->GetUndoSystem().CaptureActorStates(SelectedActors),
										"Edit Billboard");
									EditorEngine->GetSceneService().MarkDirty();
								}
							}
							if (bSelected)
							{
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
				});
			}
		}

	}
	else if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		if (USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(Component))
		{
			Builder.AddCustom([this, SkeletalComp]()
				{
					RenderSkeletalBonePoseDebug(SkeletalComp);
				});
		}

		if (UInterpToMovementComponent* InterpComp = Cast<UInterpToMovementComponent>(Component))
		{
			Builder.AddCustom([this, InterpComp]()
			{
				RenderInterpControlPoints(InterpComp);
			});
		}

		if (UActorSequenceComponent* SequenceComp = Cast<UActorSequenceComponent>(Component))
		{
			Builder.AddCustom([this, SequenceComp]()
			{
				ActorSequenceDetails.Render(SequenceComp, LastDeltaTime);
			});
		}

		if (ULightComponent* LightComp = Cast<ULightComponent>(Component))
		{
			Builder.AddButton("Override camera with light's perspective", [LightComp]()
			{
				UWorld* World = GEngine ? GEngine->GetWorld() : nullptr;
				FViewportCamera* Camera = World ? World->GetActiveCamera() : nullptr;
				if (Camera)
				{
					Camera->SetLocation(LightComp->GetWorldLocation());
					Camera->SetRotation(LightComp->GetRelativeQuat());
				}
			});
		}
		else if (UScriptComponent* ScriptComp = Cast<UScriptComponent>(Component))
		{
			Builder.AddCustom([this, ScriptComp]()
			{
				FScriptManager& ScriptMgr = FScriptManager::Get();
				DrawDetailsSeparator();
				DrawDetailsSectionLabel("Script Actions");

				if (ImGui::Button("Create Script"))
				{
					FString ScriptPath = ScriptComp->GetScriptName();
					if (ScriptPath.empty() || IsBlankString(ScriptPath))
					{
						if (EditorEngine)
						{
							EditorEngine->GetNotificationService().Warning("Script name is empty");
						}
					}
					else
					{
						FString SelectedScriptPath;
						if (!PromptCreateScriptAs(EditorEngine, ScriptPath, SelectedScriptPath))
						{
							return;
						}

						if (!ScriptMgr.CreateScript(SelectedScriptPath))
						{
							if (EditorEngine)
							{
								EditorEngine->GetNotificationService().Error("Script create failed");
							}
							return;
						}

						ScriptComp->SetScriptName(MakeScriptReferenceFromPath(SelectedScriptPath));
						ScriptComp->ReloadLuaProperties();
						if (EditorEngine)
						{
							EditorEngine->GetNotificationService().Info("Script created");
						}
					}
				}

				if (ImGui::Button("Edit Script"))
				{
					FString ScriptPath = ScriptComp->GetScriptName();
					if (ScriptPath.empty() || IsBlankString(ScriptPath))
					{
						if (EditorEngine)
						{
							EditorEngine->GetNotificationService().Warning("No script selected");
						}
					}
					else if (!ScriptMgr.EditScript(ScriptPath) && EditorEngine)
					{
						EditorEngine->GetNotificationService().Warning("Script file not found");
					}
				}
			});
		}
	}

	if (Builder.IsEmpty())
	{
		return;
	}

	for (const FDebugDetailsItem& Item : Builder.GetItems())
	{
		RenderDebugDetailsItem(Item);
	}
}

void FEditorPropertyWidget::RenderDebugDetailsItem(const FDebugDetailsItem& Item)
{
	switch (Item.Type)
	{
	case EDebugDetailsItemType::Text:
		DrawDetailsSeparator();
		DrawDetailsSectionLabel(Item.Label.c_str());
		ImGui::TextUnformatted(Item.Value.c_str());
		break;
	case EDebugDetailsItemType::Button:
		DrawDetailsSeparator();
		if (ImGui::Button(Item.Label.c_str()) && Item.Callback)
		{
			Item.Callback();
		}
		break;
	case EDebugDetailsItemType::SRVPreview:
		if (Item.SRVPreview.SRV)
		{
			const FSRVDisplayInfo& Info = Item.SRVPreview.DisplayInfo;
			DrawDetailsSeparator();
			DrawDetailsSectionLabel(Item.Label.c_str());
			ImGui::Image(
				Item.SRVPreview.SRV,
				ImVec2(Info.ImageWidth, Info.ImageHeight),
				ImVec2(Info.UV0X, Info.UV0Y),
				ImVec2(Info.UV1X, Info.UV1Y));
		}
		break;
	case EDebugDetailsItemType::CubeSRVPreview:
	{
		static const char* FaceLabels[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
		const FSRVDisplayInfo& Info = Item.CubeSRVPreview.DisplayInfo;
		bool bHasAnyFace = false;
		for (ID3D11ShaderResourceView* FaceSRV : Item.CubeSRVPreview.FaceSRVs)
		{
			bHasAnyFace = bHasAnyFace || FaceSRV != nullptr;
		}
		if (!bHasAnyFace)
		{
			break;
		}

		DrawDetailsSeparator();
		DrawDetailsSectionLabel(Item.Label.c_str());
		for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
		{
			ID3D11ShaderResourceView* FaceSRV = Item.CubeSRVPreview.FaceSRVs[FaceIndex];
			if (!FaceSRV)
			{
				continue;
			}

			ImGui::BeginGroup();
			ImGui::TextUnformatted(FaceLabels[FaceIndex]);
			ImGui::Image(
				FaceSRV,
				ImVec2(Info.ImageWidth, Info.ImageHeight),
				ImVec2(Info.UV0X, Info.UV0Y),
				ImVec2(Info.UV1X, Info.UV1Y));
			ImGui::EndGroup();

			if ((FaceIndex % 3) != 2)
			{
				ImGui::SameLine();
			}
		}
		break;
	}
	case EDebugDetailsItemType::TagEditor:
	case EDebugDetailsItemType::Custom:
		if (Item.Callback)
		{
			Item.Callback();
		}
		break;
	default:
		break;
	}
}

bool FEditorPropertyWidget::RenderObjectPtrWidget(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget, const char* Label, int32 ArrayIndex)
{
	if (!Property.ObjectPtrOps || !ValuePtr)
	{
		return false;
	}

	UObject* CurrentObject = Property.ObjectPtrOps->GetObject(ValuePtr);
	const bool bMaterialAsset = Property.ReferenceKind == EObjectReferenceKind::Asset
		&& Property.ObjectClass
		&& Property.ObjectClass->IsChildOf(UMaterialInterface::StaticClass());
	// Cycle 11: raw UStaticMesh* with ReferenceKind=Asset도 asset picker로 처리.
	// 기존엔 UMaterialInterface만 special-case였고 다른 asset 타입은 component 순회로 잘못 빠졌음.
	// 패턴은 bMaterialAsset 블록과 동일 (Combo + EditorAssetService 경유).
	const bool bStaticMeshAsset = Property.ReferenceKind == EObjectReferenceKind::Asset
		&& Property.ObjectClass
		&& Property.ObjectClass->IsChildOf(UStaticMesh::StaticClass());
	const bool bParticleSystemAsset = Property.ReferenceKind == EObjectReferenceKind::Asset
		&& Property.ObjectClass
		&& Property.ObjectClass->IsChildOf(UParticleSystem::StaticClass());

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

		if (ImGui::BeginCombo(Label, CurrentLabel.c_str()))
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
				if (ImGui::IsItemHovered())
				{
					if (UMaterialInterface* Candidate = AssetService.ResolveMaterialInterfaceByIndex(MaterialIndex))
					{
						RenderMaterialPreviewTooltip(Candidate);
					}
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered() && CurrentMaterial)
		{
			RenderMaterialPreviewTooltip(CurrentMaterial);
		}

		if (ArrayIndex >= 0)
		{
			ImGui::SameLine();
			if (ImGui::Button("Edit"))
			{
				if (UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(SelectedComponent))
				{
					EditorEngine->GetMainPanel().OpenMaterialSlot(PrimitiveComp, ArrayIndex);
				}
			}
		}

		return bChanged;
	}

	// Cycle 11: UStaticMesh asset picker — bMaterialAsset 블록과 평행 구조.
	// UParticleMeshRendererProperties::Mesh 등 raw UStaticMesh* with ReferenceKind=Asset에 사용.
	// path 식별자는 UStaticMesh::GetAssetPathFileName() — UStaticMeshComponent와 동일 기준.
	if (bStaticMeshAsset && EditorEngine)
	{
		FEditorAssetService& AssetService = EditorEngine->GetAssetService();
		const TArray<FString>& StaticMeshPaths = AssetService.GetStaticMeshAssetPaths();
		UStaticMesh* CurrentMesh = Cast<UStaticMesh>(CurrentObject);
		const FString CurrentIdentifier = CurrentMesh
			? FPaths::Normalize(CurrentMesh->GetAssetPathFileName())
			: FString();
		const FString CurrentLabel = CurrentIdentifier.empty() ? FString("None") : CurrentIdentifier;
		bool bChanged = false;

		if (ImGui::BeginCombo(Label, CurrentLabel.c_str()))
		{
			if (ImGui::Selectable("None", CurrentMesh == nullptr))
			{
				Property.ObjectPtrOps->SetObject(ValuePtr, nullptr);
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
						bChanged = true;
					}
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		return bChanged;
	}

	if (bParticleSystemAsset && EditorEngine)
	{
		FEditorAssetService& AssetService = EditorEngine->GetAssetService();
		const TArray<FString>& ParticleSystemPaths = AssetService.GetParticleSystemAssetPaths();
		UParticleSystem* CurrentParticleSystem = Cast<UParticleSystem>(CurrentObject);
		const FString CurrentIdentifier = CurrentParticleSystem
			? FPaths::Normalize(CurrentParticleSystem->GetAssetPath())
			: FString();
		const FString CurrentLabel = CurrentIdentifier.empty() ? FString("None") : CurrentIdentifier;
		bool bChanged = false;

		if (ImGui::BeginCombo(Label, CurrentLabel.c_str()))
		{
			if (ImGui::Selectable("None", CurrentParticleSystem == nullptr))
			{
				Property.ObjectPtrOps->SetObject(ValuePtr, nullptr);
				bChanged = true;
			}

			for (int32 AssetIndex = 0; AssetIndex < static_cast<int32>(ParticleSystemPaths.size()); ++AssetIndex)
			{
				ImGui::PushID(AssetIndex);
				const FString& AssetPath = ParticleSystemPaths[AssetIndex];
				const FString NormalizedPath = FPaths::Normalize(AssetPath);
				const bool bSelected = CurrentIdentifier == NormalizedPath;
				if (ImGui::Selectable(AssetPath.c_str(), bSelected))
				{
					if (UParticleSystem* Candidate = AssetService.LoadParticleSystem(AssetPath))
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
			ImGui::EndCombo();
		}

		if (ImGui::BeginDragDropTarget())
		{
			FString DroppedPath;
			if (TryNormalizeDroppedAssetPath(ImGui::AcceptDragDropPayload("ParticleSystemContentItem"), DroppedPath))
			{
				if (UParticleSystem* Candidate = AssetService.LoadParticleSystem(DroppedPath))
				{
					Property.ObjectPtrOps->SetObject(ValuePtr, Candidate);
					bChanged = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		return bChanged;
	}

	AActor* Owner = nullptr;
	if (AActor* Actor = Cast<AActor>(NotifyTarget))
	{
		Owner = Actor;
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(NotifyTarget))
	{
		Owner = Component->GetOwner();
	}
	else if (SelectedComponent)
	{
		Owner = SelectedComponent->GetOwner();
	}

	TArray<UObject*> Choices;
	Choices.push_back(nullptr);
	if (Owner)
	{
		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (!Component)
			{
				continue;
			}
			if (!Property.ObjectClass || Component->IsA(Property.ObjectClass))
			{
				Choices.push_back(Component);
			}
		}
	}

	auto GetLabel = [&](UObject* Object) -> FString
	{
		if (!Object)
		{
			return "None";
		}
		FString Name = Object->GetFName().ToString();
		if (Name.empty())
		{
			Name = Object->GetClassName();
		}
		if (Owner && Object == Owner->GetRootComponent())
		{
			return "[Root] " + Name;
		}
		return Name;
	};

	bool bChanged = false;
	if (ImGui::BeginCombo(Label, GetLabel(CurrentObject).c_str()))
	{
		for (UObject* Candidate : Choices)
		{
			const bool bSelected = Candidate == CurrentObject;
			const FString CandidateLabel = GetLabel(Candidate);
			char SelectableId[128];
			snprintf(SelectableId, sizeof(SelectableId), "%s##%p", CandidateLabel.c_str(), static_cast<void*>(Candidate));
			if (ImGui::Selectable(SelectableId, bSelected))
			{
				Property.ObjectPtrOps->SetObject(ValuePtr, Candidate);
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

bool FEditorPropertyWidget::RenderSoftObjectPtrWidget(const FProperty& Property, void* ValuePtr, const char* Label)
{
	if (!Property.SoftObjectOps || !ValuePtr)
	{
		return false;
	}

	bool bChanged = false;
	FString Current = Property.SoftObjectOps->GetPath(ValuePtr);
	TArray<FString> LocalOptions;
	const TArray<FString>* Options = nullptr;
	if (EditorEngine && Property.ObjectClass)
	{
		if (Property.ObjectClass->IsChildOf(UStaticMesh::StaticClass()))
		{
			Options = &EditorEngine->GetAssetService().GetStaticMeshAssetPaths();
		}
		else if (Property.ObjectClass->IsChildOf(USkeletalMesh::StaticClass()))
		{
			Options = &EditorEngine->GetAssetService().GetSkeletalMeshAssetPaths();
		}
		else if (Property.ObjectClass->IsChildOf(UMaterialInterface::StaticClass()))
		{
			Options = &EditorEngine->GetAssetService().GetMaterialInterfaceNames();
		}
		else if (Property.ObjectClass->IsChildOf(UAnimationAsset::StaticClass()))
		{
			Options = &EditorEngine->GetAssetService().GetAnimSequenceAssetPaths();
		}
		else if (Property.ObjectClass->IsChildOf(UAnimGraphAsset::StaticClass()))
		{
			Options = &EditorEngine->GetAssetService().GetAnimGraphAssetPaths();
		}
		else if (Property.ObjectClass->IsChildOf(UParticleSystem::StaticClass()))
		{
			Options = &EditorEngine->GetAssetService().GetParticleSystemAssetPaths();
		}
		else if (Property.ObjectClass->IsChildOf(UAnimLuaProgramAsset::StaticClass()))
		{
			Options = &EditorEngine->GetAssetService().GetLuaAnimGraphAssetPaths();
		}
	}

	if (Options && !Options->empty())
	{
		if (ImGui::BeginCombo(Label, Current.empty() ? "<None>" : Current.c_str()))
		{
			if (ImGui::Selectable("<None>", Current.empty()))
			{
				Property.SoftObjectOps->SetPath(ValuePtr, FString());
				bChanged = true;
			}
			for (const FString& Path : *Options)
			{
				const bool bSelected = Current == Path;
				if (ImGui::Selectable(Path.c_str(), bSelected))
				{
					Property.SoftObjectOps->SetPath(ValuePtr, Path);
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
	else
	{
		char Buf[512];
		strncpy_s(Buf, sizeof(Buf), Current.c_str(), _TRUNCATE);
		if (ImGui::InputText(Label, Buf, sizeof(Buf)))
		{
			Property.SoftObjectOps->SetPath(ValuePtr, Buf);
			bChanged = true;
		}
	}

	if (Property.ObjectClass
		&& Property.ObjectClass->IsChildOf(UAnimGraphAsset::StaticClass())
		&& ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("AnimGraphContentItem"))
		{
			if (Payload->Data && Payload->DataSize > 0)
			{
				const FString PayloadPath = static_cast<const char*>(Payload->Data);
				const std::filesystem::path DroppedPath = FPaths::ToWide(PayloadPath);
				Property.SoftObjectOps->SetPath(
					ValuePtr,
					DroppedPath.is_absolute()
						? FPaths::Normalize(FPaths::ToRelativeString(DroppedPath.wstring()))
						: FPaths::Normalize(PayloadPath));
				bChanged = true;
			}
		}
		ImGui::EndDragDropTarget();
	}
	else if (Property.ObjectClass
		&& Property.ObjectClass->IsChildOf(UAnimLuaProgramAsset::StaticClass())
		&& ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("LuaAnimGraphContentItem"))
		{
			if (Payload->Data && Payload->DataSize > 0)
			{
				const FString PayloadPath = static_cast<const char*>(Payload->Data);
				const std::filesystem::path DroppedPath = FPaths::ToWide(PayloadPath);
				Property.SoftObjectOps->SetPath(
					ValuePtr,
					DroppedPath.is_absolute()
						? FPaths::Normalize(FPaths::ToRelativeString(DroppedPath.wstring()))
						: FPaths::Normalize(PayloadPath));
				bChanged = true;
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (Property.ObjectClass
		&& Property.ObjectClass->IsChildOf(UParticleSystem::StaticClass()))
	{
		if (ImGui::BeginDragDropTarget())
		{
			FString DroppedPath;
			if (TryNormalizeDroppedAssetPath(ImGui::AcceptDragDropPayload("ParticleSystemContentItem"), DroppedPath))
			{
				Property.SoftObjectOps->SetPath(ValuePtr, DroppedPath);
				Current = DroppedPath;
				bChanged = true;
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();
		const bool bHasParticleSystemAsset = !Current.empty();
		ImGui::PushID(Property.Name ? Property.Name : Label);
		ImGui::BeginDisabled(!bHasParticleSystemAsset);
		if (ImGui::Button("Edit") && bHasParticleSystemAsset && EditorEngine)
		{
			EditorEngine->GetMainPanel().OpenParticleSystemAsset(FPaths::Normalize(Current));
		}
		ImGui::EndDisabled();
		ImGui::PopID();
		ImGui::Dummy(ImVec2(0.0f, 0.0f));
	}

	return bChanged;
}

bool FEditorPropertyWidget::RenderArrayPropertyWidget(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget)
{
	if (!Property.ArrayOps || !Property.InnerProperty || !ValuePtr)
	{
		return false;
	}

	bool bChanged = false;
	int32 ToRemove = -1;
	DrawDetailsSeparator();
	DrawDetailsSectionLabel(GetPropertyDisplayName(Property));
	ImGui::PushID(Property.Name);

	const int32 Count = Property.ArrayOps->Num(ValuePtr);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		ImGui::PushID(Index);
		void* ElementPtr = Property.ArrayOps->GetElementPtr(ValuePtr, Index);
		char ItemLabel[32];
		snprintf(ItemLabel, sizeof(ItemLabel), "[%d]", Index);

		ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - UIConstants::XButtonSize - 8.0f));
		if (RenderPropertyValueWidget(*Property.InnerProperty, ElementPtr, NotifyTarget, ItemLabel, Index))
		{
			bChanged = true;
		}

		ImGui::SameLine();
		char XId[32];
		snprintf(XId, sizeof(XId), "##rm_%d", Index);
		if (DrawXButton(XId))
		{
			ToRemove = Index;
		}
		ImGui::PopID();
	}

	if (ToRemove >= 0)
	{
		Property.ArrayOps->RemoveAt(ValuePtr, ToRemove);
		bChanged = true;
	}

	char AddLabel[64];
	snprintf(AddLabel, sizeof(AddLabel), "+ Add##%s", Property.Name);
	if (ImGui::Button(AddLabel, ImVec2(-1, 0)))
	{
		Property.ArrayOps->AddDefaulted(ValuePtr);
		bChanged = true;
	}

	ImGui::PopID();
	return bChanged;
}

bool FEditorPropertyWidget::RenderStructPropertyWidget(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget, const char* Label)
{
	if (!ValuePtr || Property.Type != EPropertyType::Struct)
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
	if (Hint && std::strcmp(Hint, "FGuid") == 0)
	{
		FGuid* Val = static_cast<FGuid*>(ValuePtr);
		char Buf[64];
		strncpy_s(Buf, sizeof(Buf), Val->ToString().c_str(), _TRUNCATE);
		if (ImGui::InputText(Label, Buf, sizeof(Buf), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			FGuid ParsedGuid;
			if (FGuid::Parse(Buf, ParsedGuid))
			{
				*Val = ParsedGuid;
				return true;
			}
		}
		return false;
	}
	if (Hint && std::strcmp(Hint, "FQuat") == 0)
	{
		FQuat* Val = static_cast<FQuat*>(ValuePtr);
		float Components[4] = { Val->X, Val->Y, Val->Z, Val->W };
		if (ImGui::DragFloat4(Label, Components, Property.Speed))
		{
			*Val = FQuat(Components[0], Components[1], Components[2], Components[3]);
			Val->Normalize();
			return true;
		}
		return false;
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
			const FString ChildLabel = MakePropertyWidgetLabel(*Child);
			if (RenderPropertyValueWidget(*Child, ChildPtr, NotifyTarget, ChildLabel.c_str()))
			{
				bChanged = true;
			}
		}
		ImGui::TreePop();
	}
	return bChanged;
}

bool FEditorPropertyWidget::RenderPropertyValueWidget(const FProperty& Property, void* ValuePtr, UObject* NotifyTarget, const char* Label, int32 ArrayIndex)
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
		return ImGui::DragInt(Label, static_cast<int32*>(ValuePtr));
	case EPropertyType::Float:
		if (Property.Min != 0.0f || Property.Max != 0.0f)
		{
			return ImGui::DragFloat(Label, static_cast<float*>(ValuePtr), Property.Speed, Property.Min, Property.Max);
		}
		return ImGui::DragFloat(Label, static_cast<float*>(ValuePtr), Property.Speed);
	case EPropertyType::Struct:
		return RenderStructPropertyWidget(Property, ValuePtr, NotifyTarget, Label);
	case EPropertyType::ObjectPtr:
		return RenderObjectPtrWidget(Property, ValuePtr, NotifyTarget, Label, ArrayIndex);
	case EPropertyType::SoftObjectPtr:
		return RenderSoftObjectPtrWidget(Property, ValuePtr, Label);
	case EPropertyType::Array:
		return RenderArrayPropertyWidget(Property, ValuePtr, NotifyTarget);
	case EPropertyType::String:
	{
		FString* Val = static_cast<FString*>(ValuePtr);
		if (Property.Name && std::strcmp(Property.Name, "ScriptName") == 0 && Cast<UScriptComponent>(NotifyTarget))
		{
			return RenderLuaScriptComboWidget(*Val, Label);
		}

		if (Property.Name && std::strcmp(Property.Name, "AnimGraphAssetPath") == 0)
		{
			const TArray<FString>& AnimGraphPaths = EditorEngine
				? EditorEngine->GetAssetService().GetAnimGraphAssetPaths()
				: EmptyAssetNames();
			return RenderAnimGraphAssetPathWidget(*Val, Label, AnimGraphPaths);
		}

		char Buf[512];
		strncpy_s(Buf, sizeof(Buf), Val->c_str(), _TRUNCATE);
		if (ImGui::InputText(Label, Buf, sizeof(Buf)))
		{
			*Val = Buf;
			return true;
		}
		return false;
	}
	case EPropertyType::Name:
	{
		FName* Val = static_cast<FName*>(ValuePtr);
		FString Current = Val->ToString();
		TArray<FString> Names;
		if (Property.Name && strcmp(Property.Name, "Font") == 0)
		{
			Names = EditorEngine ? EditorEngine->GetAssetService().GetFontNames() : EmptyAssetNames();
		}
		else if (Property.Name && strcmp(Property.Name, "SubUV") == 0)
		{
			Names = EditorEngine ? EditorEngine->GetAssetService().GetSubUVNames() : EmptyAssetNames();
		}

		if (!Names.empty())
		{
			bool bChanged = false;
			if (ImGui::BeginCombo(Label, Current.c_str()))
			{
				for (const FString& Name : Names)
				{
					const bool bSelected = Current == Name;
					if (ImGui::Selectable(Name.c_str(), bSelected))
					{
						*Val = FName(Name);
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

		char Buf[256];
		strncpy_s(Buf, sizeof(Buf), Current.c_str(), _TRUNCATE);
		if (ImGui::InputText(Label, Buf, sizeof(Buf)))
		{
			*Val = FName(Buf);
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

		if (ImGui::Combo(Label, &CurrentIndex, ComboGetter, const_cast<UEnum*>(Property.EnumMeta), static_cast<int>(Property.EnumMeta->Count)))
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
	default:
		break;
	}
	return false;
}

void FEditorPropertyWidget::RenderPropertyWidget(const FPropertyHandle& Handle)
{
	if (!Handle.IsValid() || !Handle.Property->Name)
	{
		return;
	}

	UObject* Object = Handle.Owner;
	const FProperty& Property = *Handle.Property;
	void* ValuePtr = Handle.GetValuePtr();
	if (!ValuePtr)
	{
		return;
	}

	UObject* NotifyTarget = Object;
	const FString WidgetLabel = MakePropertyWidgetLabel(Property);
	bool bChanged = RenderPropertyValueWidget(Property, ValuePtr, NotifyTarget, WidgetLabel.c_str());

	if (ImGui::IsItemActivated() && !bPropertyEditUndoCaptured && EditorEngine)
	{
		BeginReflectedPropertyEdit(NotifyTarget, Property, "Edit Property");
	}

	if (bChanged && NotifyTarget)
	{
		if (!bPropertyEditUndoCaptured && EditorEngine)
		{
			BeginReflectedPropertyEdit(NotifyTarget, Property, "Edit Property");
		}
		NotifyTarget->PostEditProperty(Property.Name);
		if (EditorEngine)
		{
			EditorEngine->GetSceneService().MarkDirty();
		}
	}

	if (bPropertyEditUndoCaptured && (ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsAnyItemActive()))
	{
		EndReflectedPropertyEdit(NotifyTarget, Property, "Edit Property");
	}
}


void FEditorPropertyWidget::RenderMaterialPreviewTooltip(UMaterialInterface* Material)
{
	if (!EditorEngine || !Material)
	{
		return;
	}

	ImGui::BeginTooltip();
	ImGui::TextUnformatted(Material->GetName().c_str());
	if (!Material->GetFilePath().empty())
	{
		ImGui::TextDisabled("%s", FPaths::Normalize(Material->GetFilePath()).c_str());
	}
	ImGui::Separator();

	if (UTexture* PreviewTexture = EditorEngine->GetAssetService().GetMaterialPreviewTexture(Material))
	{
		if (ID3D11ShaderResourceView* SRV = PreviewTexture->GetSRV())
		{
			ImGui::Image(reinterpret_cast<ImTextureID>(SRV), ImVec2(128.0f, 128.0f));
			ImGui::Separator();
		}
	}
	else
	{
		ImGui::TextDisabled("No texture preview.");
		ImGui::Separator();
	}

	FMaterialParamValue ParamValue;
	auto DrawColorParam = [](const char* Label, const ImVec4& Color)
	{
		ImGui::ColorButton(Label, Color, ImGuiColorEditFlags_NoTooltip, ImVec2(34.0f, 18.0f));
		ImGui::SameLine();
		ImGui::TextUnformatted(Label);
	};

	if (Material->GetParam("DiffuseColor", ParamValue)
		&& ParamValue.Type == EMaterialParamType::Vector3
		&& std::holds_alternative<FVector>(ParamValue.Value))
	{
		const FVector Color = std::get<FVector>(ParamValue.Value);
		DrawColorParam("Diffuse", ImVec4(Color.X, Color.Y, Color.Z, 1.0f));
	}
	if (Material->GetParam("SpecularColor", ParamValue)
		&& ParamValue.Type == EMaterialParamType::Vector3
		&& std::holds_alternative<FVector>(ParamValue.Value))
	{
		const FVector Color = std::get<FVector>(ParamValue.Value);
		DrawColorParam("Specular", ImVec4(Color.X, Color.Y, Color.Z, 1.0f));
	}
	if (Material->GetParam("EmissiveColor", ParamValue)
		&& ParamValue.Type == EMaterialParamType::Vector3
		&& std::holds_alternative<FVector>(ParamValue.Value))
	{
		const FVector Color = std::get<FVector>(ParamValue.Value);
		DrawColorParam("Emissive", ImVec4(Color.X, Color.Y, Color.Z, 1.0f));
	}
	ImGui::EndTooltip();
}

void FEditorPropertyWidget::RenderSkeletalStateMachinePreview(USkeletalMeshComponent* Comp)
{
	if (!Comp)
	{
		return;
	}

	DrawDetailsSeparator();
	DrawDetailsSectionLabel("Animation StateMachine");

	UAnimationStateMachine* StateMachine = Comp->GetAnimationStateMachine();
	if (!StateMachine)
	{
		ImGui::TextDisabled("No active StateMachine.");
		ImGui::TextDisabled("Run the script to preview states.");
		return;
	}

	const FString CurrentState = StateMachine->GetCurrentStateName();
	const FString NextState = StateMachine->GetNextStateName();
	const bool bBlending = StateMachine->IsBlending();
	const float BlendAlpha = StateMachine->GetBlendAlpha();
	const float BlendElapsed = StateMachine->GetBlendElapsed();
	const float BlendDuration = StateMachine->GetBlendDuration();

	ImGui::Text("Current State: %s", CurrentState.empty() ? "None" : CurrentState.c_str());
	ImGui::Text("Next State: %s", NextState.empty() ? "None" : NextState.c_str());

	if (bBlending)
	{
		ImGui::Text("Blending: True (%.2f / %.2fs)", BlendElapsed, BlendDuration);
	}
	else
	{
		ImGui::Text("Blending: False");
	}

	ImGui::ProgressBar(BlendAlpha, ImVec2(-1.0f, 0.0f));

	const TArray<FString> StateNames = StateMachine->GetStateNames();
	if (!StateNames.empty())
	{
		ImGui::Spacing();
		ImGui::TextUnformatted("States");

		if (!StateMachinePreviewBlendTimeByComponent.contains(Comp->GetUUID()))
		{
			StateMachinePreviewBlendTimeByComponent[Comp->GetUUID()] = 0.2f;
		}
		float& PreviewBlendTime = StateMachinePreviewBlendTimeByComponent[Comp->GetUUID()];

		ImGui::DragFloat("Preview Blend Time", &PreviewBlendTime, 0.01f, 0.0f, 10.0f, "%.2f");

		const float ButtonWidth = 88.0f;
		const float Spacing = ImGui::GetStyle().ItemSpacing.x;
		const float Available = ImGui::GetContentRegionAvail().x;
		int32 ButtonsInRow = 0;

		for (const FString& StateName : StateNames)
		{
			ImGui::PushID(StateName.c_str());

			if (ButtonsInRow > 0 && ButtonsInRow * (ButtonWidth + Spacing) + ButtonWidth <= Available)
			{
				ImGui::SameLine();
			}
			else
			{
				ButtonsInRow = 0;
			}

			const bool bIsCurrentState = (StateName == CurrentState);
			if (bIsCurrentState)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.25f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.30f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.40f, 0.22f, 1.0f));
			}

			if (ImGui::Button(StateName.c_str(), ImVec2(ButtonWidth, 0.0f)))
			{
				StateMachine->SetStateByName(StateName, PreviewBlendTime);
			}

			if (bIsCurrentState)
			{
				ImGui::PopStyleColor(3);
			}

			++ButtonsInRow;
			ImGui::PopID();
		}
	}
}

void FEditorPropertyWidget::RenderSkeletalBonePoseDebug(USkeletalMeshComponent* Comp)
{
	const FDetailsPerfClock::time_point DebugStart = bDetailsPerfTraceFrame ? FDetailsPerfClock::now() : FDetailsPerfClock::time_point{};
	if (!Comp)
	{
		return;
	}

	USkeletalMesh* Mesh = Comp->GetSkeletalMesh();
	if (!Mesh)
	{
		return;
	}

	const TArray<FBoneInfo>& Bones = Mesh->GetBones();
	if (Bones.empty())
	{
		return;
	}

	const uint32 ComponentId = Comp->GetUUID();
	int32& SelectedBoneIndex = SelectedSkeletalBoneByComponent[ComponentId];
	if (SelectedBoneIndex < 0 || SelectedBoneIndex >= static_cast<int32>(Bones.size()))
	{
		SelectedBoneIndex = 0;
	}

	DrawDetailsSeparator();
	DrawDetailsSectionLabel("Bone Pose Debug");
	ImGui::Spacing();

	ImGui::PushID(Comp);

	const auto MakeBoneLabel = [&Bones](int32 BoneIndex) -> FString
	{
		if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
		{
			return "None";
		}

		return std::to_string(BoneIndex) + ": " + Bones[BoneIndex].Name;
	};

	const FString CurrentLabel = MakeBoneLabel(SelectedBoneIndex);
	if (ImGui::BeginCombo("Bone", CurrentLabel.c_str()))
	{
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
		{
			const FString Label = MakeBoneLabel(BoneIndex);
			const bool bSelected = SelectedBoneIndex == BoneIndex;
			if (ImGui::Selectable(Label.c_str(), bSelected))
			{
				SelectedBoneIndex = BoneIndex;
			}

			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}

	const uint32 MeshId = Mesh->GetUUID();
	TMap<int32, FSkeletalBonePoseEditState>& BonePoseEditStates = SkeletalBonePoseEditStatesByComponent[ComponentId];
	FSkeletalBonePoseEditState& EditState = BonePoseEditStates[SelectedBoneIndex];

	const auto ResetEditStateToIdentityOffset = [](FSkeletalBonePoseEditState& State, uint32 InMeshId, int32 InBoneIndex)
	{
		State.MeshId = InMeshId;
		State.BoneIndex = InBoneIndex;
		State.LocationOffset = FVector::ZeroVector;
		State.RotationOffset = FVector::ZeroVector;
		State.ScaleOffset = FVector::OneVector;
	};

	const auto InitializeEditStateFromCurrentPose = [Comp, Mesh, MeshId](FSkeletalBonePoseEditState& State, int32 BoneIndex)
	{
		State.MeshId = MeshId;
		State.BoneIndex = BoneIndex;

		const FMatrix& BindLocalTransform = Mesh->GetLocalBindTransform(BoneIndex);
		const FMatrix CurrentLocalTransform = Comp->GetBoneLocalTransform(BoneIndex);
		const FMatrix OffsetTransformMatrix = CurrentLocalTransform * BindLocalTransform.GetInverse();
		const FTransform OffsetTransform(OffsetTransformMatrix);

		State.LocationOffset = OffsetTransform.GetTranslation();
		State.RotationOffset = OffsetTransform.Rotator().Euler();
		State.ScaleOffset = OffsetTransform.GetScale3D();
	};

	if (EditState.MeshId != MeshId || EditState.BoneIndex != SelectedBoneIndex)
	{
		InitializeEditStateFromCurrentPose(EditState, SelectedBoneIndex);
	}
	else if (!bSkeletalBonePoseEditUndoCaptured && !ImGui::IsAnyItemActive())
	{
		InitializeEditStateFromCurrentPose(EditState, SelectedBoneIndex);
	}

	bool bBonePoseEditDeactivatedAfterEdit = false;
	bool bBonePoseEditDeactivated = false;
	auto DrawVec3 = [this, Comp, SelectedBoneIndex, &bBonePoseEditDeactivatedAfterEdit, &bBonePoseEditDeactivated](
		const char* Label,
		FVector& Value,
		float Speed) -> bool
	{
		float Values[3] = { Value.X, Value.Y, Value.Z };
		const bool bEdited = ImGui::DragFloat3(Label, Values, Speed);
		if (ImGui::IsItemActivated() && EditorEngine && !bSkeletalBonePoseEditUndoCaptured)
		{
			SkeletalBonePoseBeforeState = EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp, SelectedBoneIndex);
			bSkeletalBonePoseEditUndoCaptured = SkeletalBonePoseBeforeState.IsValid();
			SkeletalBonePoseEditComponent = bSkeletalBonePoseEditUndoCaptured ? Comp : nullptr;
			SkeletalBonePoseEditBoneIndex = bSkeletalBonePoseEditUndoCaptured ? SelectedBoneIndex : -1;
		}

		if (bEdited)
		{
			Value = FVector(Values[0], Values[1], Values[2]);
		}

		if (bSkeletalBonePoseEditUndoCaptured
			&& SkeletalBonePoseEditComponent == Comp
			&& SkeletalBonePoseEditBoneIndex == SelectedBoneIndex
			&& ImGui::IsItemDeactivatedAfterEdit())
		{
			bBonePoseEditDeactivatedAfterEdit = true;
		}
		else if (bSkeletalBonePoseEditUndoCaptured
			&& SkeletalBonePoseEditComponent == Comp
			&& SkeletalBonePoseEditBoneIndex == SelectedBoneIndex
			&& ImGui::IsItemDeactivated())
		{
			bBonePoseEditDeactivated = true;
		}

		return bEdited;
	};

	const bool bTranslationEdited = DrawVec3("Location Offset", EditState.LocationOffset, 0.1f);
	const bool bRotationEdited = DrawVec3("Rotation Offset", EditState.RotationOffset, 0.1f);
	const bool bScaleEdited = DrawVec3("Scale Offset", EditState.ScaleOffset, 0.01f);

	if (bTranslationEdited || bRotationEdited || bScaleEdited)
	{
		const FTransform OffsetTransform(
			FQuat::MakeFromEuler(EditState.RotationOffset),
			EditState.LocationOffset,
			EditState.ScaleOffset);
		const FMatrix NewLocalTransform =
			OffsetTransform.ToMatrixWithScale() * Mesh->GetLocalBindTransform(SelectedBoneIndex);
		Comp->SetBoneLocalTransform(SelectedBoneIndex, NewLocalTransform);
	}

	if (bSkeletalBonePoseEditUndoCaptured
		&& SkeletalBonePoseEditComponent == Comp
		&& SkeletalBonePoseEditBoneIndex == SelectedBoneIndex)
	{
		if (bBonePoseEditDeactivatedAfterEdit && EditorEngine)
		{
			const FEditorSkeletalBonePoseState AfterState =
				EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp, SelectedBoneIndex);
			EditorEngine->GetUndoSystem().RecordSkeletalBonePose(
				SkeletalBonePoseBeforeState,
				AfterState,
				"Edit Bone Pose");
		}

		if (bBonePoseEditDeactivatedAfterEdit || bBonePoseEditDeactivated)
		{
			bSkeletalBonePoseEditUndoCaptured = false;
			SkeletalBonePoseEditComponent = nullptr;
			SkeletalBonePoseEditBoneIndex = -1;
			SkeletalBonePoseBeforeState = FEditorSkeletalBonePoseState();
		}
	}

	const float HalfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("Reset Bone", ImVec2(HalfWidth, 0.0f)))
	{
		FEditorSkeletalBonePoseState BeforeState;
		if (EditorEngine)
		{
			BeforeState = EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp, SelectedBoneIndex);
		}
		ResetEditStateToIdentityOffset(EditState, MeshId, SelectedBoneIndex);
		Comp->SetBoneLocalTransform(SelectedBoneIndex, Mesh->GetLocalBindTransform(SelectedBoneIndex));
		if (EditorEngine)
		{
			const FEditorSkeletalBonePoseState AfterState =
				EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp, SelectedBoneIndex);
			EditorEngine->GetUndoSystem().RecordSkeletalBonePose(BeforeState, AfterState, "Reset Bone Pose");
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Pose", ImVec2(-1.0f, 0.0f)))
	{
		FEditorSkeletalBonePoseState BeforeState;
		if (EditorEngine)
		{
			BeforeState = EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp);
		}
		Comp->ResetToBindPose();
		BonePoseEditStates.clear();
		if (EditorEngine)
		{
			const FEditorSkeletalBonePoseState AfterState =
				EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp);
			EditorEngine->GetUndoSystem().RecordSkeletalBonePose(BeforeState, AfterState, "Reset Bone Pose");
		}
	}

	ImGui::PopID();

	if (bDetailsPerfTraceFrame)
	{
		UE_LOG(
			"[DetailsPerf] SkeletalBoneDebug Bones=%zu SelectedBone=%d Time=%.2fms",
			Bones.size(),
			SelectedBoneIndex,
			DetailsPerfMs(DebugStart, FDetailsPerfClock::now()));
	}
}

void FEditorPropertyWidget::RenderInterpControlPoints(UInterpToMovementComponent* Comp)
{
	// --- Playback actions -----------------------------------------------
	DrawDetailsSeparator();
	DrawDetailsSectionLabel("Playback");
	ImGui::Spacing();

	float HalfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("Initiate", ImVec2(HalfWidth, 0))) Comp->Initiate();
	ImGui::SameLine();
	if (ImGui::Button("Stop",     ImVec2(HalfWidth, 0))) Comp->ResetAndHalt();
	if (ImGui::Button("Reset",    ImVec2(-1,        0))) Comp->Reset();
}


void FEditorPropertyWidget::AttachAndSelectNewComponent(AActor* PrimaryActor, UActorComponent* NewComp, USceneComponent* AttachTargetOverride)
{
	if (!PrimaryActor || !NewComp) return;

	USceneComponent* AttachTarget = nullptr;
	if (AttachTargetOverride && AttachTargetOverride->GetOwner() == PrimaryActor)
	{
		AttachTarget = AttachTargetOverride;
	}
	else if (SelectedComponent && SelectedComponent->IsA<USceneComponent>() && SelectedComponent->GetOwner() == PrimaryActor)
	{
		AttachTarget = static_cast<USceneComponent*>(SelectedComponent);
	}
	else
	{
		AttachTarget = PrimaryActor->GetRootComponent();
	}

	if (USceneComponent* SceneComp = Cast<USceneComponent>(NewComp))
	{
		if (AttachTarget && SceneComp != AttachTarget)
		{
			SceneComp->AttachToComponent(AttachTarget);
		}
		else if (!PrimaryActor->GetRootComponent())
		{
			PrimaryActor->SetRootComponent(SceneComp);
		}
	}
	else if (UMovementComponent* MoveComp = Cast<UMovementComponent>(NewComp))
	{
		if (AttachTarget) MoveComp->SetUpdatedComponent(AttachTarget);
	}

	if (UScriptComponent* ScriptComp = Cast<UScriptComponent>(NewComp))
	{
		if (ScriptComp->GetScriptName().empty())
		{
			FString SceneName = "Default";
			if (EditorEngine)
			{
				SceneName = EditorEngine->GetSceneService().GetSceneName();
			}
			ScriptComp->SetScriptName(MakeDefaultScriptName(SceneName, PrimaryActor));
		}
	}

	SelectComponentForDetails(NewComp);
}

template<typename T>
void FEditorPropertyWidget::RenderEditableName(const char* Label, T* TargetObject, bool* bFocusNextFrame)
{
	if (!TargetObject) return;

	char NameBuf[256];
	strncpy_s(NameBuf, sizeof(NameBuf), TargetObject->GetFName().ToString().c_str(), _TRUNCATE);

	if (bFocusNextFrame && *bFocusNextFrame)
	{
		ImGui::SetKeyboardFocusHere();
		*bFocusNextFrame = false;
	}

	// Enter 키를 누르거나 포커스를 잃었을 경우에 이름이 변경되도록 설정
	if (ImGui::InputText(Label, NameBuf, sizeof(NameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
	{
		TArray<FEditorSerializedActorState> BeforeActorStates;
		if (EditorEngine)
		{
			if (AActor* OwnerActor = ResolveActorStateUndoOwner(TargetObject))
			{
				TArray<AActor*> Actors;
				Actors.push_back(OwnerActor);
				BeforeActorStates = EditorEngine->GetUndoSystem().CaptureActorStates(Actors);
			}
			else
			{
				EditorEngine->GetUndoSystem().CaptureSnapshot("Rename");
			}
		}
		TargetObject->SetFName(FName(NameBuf));
		if (EditorEngine)
		{
			if (!BeforeActorStates.empty())
			{
				if (AActor* OwnerActor = ResolveActorStateUndoOwner(TargetObject))
				{
					TArray<AActor*> Actors;
					Actors.push_back(OwnerActor);
					EditorEngine->GetUndoSystem().RecordActorStateChange(
						BeforeActorStates,
						EditorEngine->GetUndoSystem().CaptureActorStates(Actors),
						"Rename");
				}
			}
			EditorEngine->GetSceneService().MarkDirty();
		}
	}
}
