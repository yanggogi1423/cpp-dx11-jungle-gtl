#include "Editor/UI/EditorPropertyWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/EditorRenderPipeline.h"
#include "ImGui/imgui.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "Component/BlueprintComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SkeletalMeshComponent.h"
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
#include "Component/PostProcess/Light/AmbientLightComponent.h"
#include "Component/PostProcess/Light/DirectionalLightComponent.h"
#include "Component/PostProcess/Light/PointLightComponent.h"
#include "Component/PostProcess/Light/SpotlightComponent.h"
#include "Core/EditorResourcePaths.h"
#include "Core/PropertyTypes.h"
#include "Core/Paths.h"
#include "Core/Logging/Log.h"
#include "Math/Color.h"
#include "Core/ResourceManager.h"
#include "Render/Resource/Material.h"
#include "Asset/StaticMesh.h"
#include "Object/FName.h"
#include "Object/Enum.h"
#include "Object/EnumRegistry.h"
#include "Object/Function.h"
#include "Object/ScriptStruct.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <functional>
#include "Animation/AnimLuaProgramAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationStateMachine.h"
#include "Animation/AnimStateMachineInstance.h"
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

	bool HasLowerExtension(const FString& Path, const char* Extension)
	{
		FString PathExtension = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(Path)).extension().wstring());
		std::transform(
			PathExtension.begin(),
			PathExtension.end(),
			PathExtension.begin(),
			[](unsigned char Ch)
			{
				return static_cast<char>(std::tolower(Ch));
			});
		return PathExtension == Extension;
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
                TargetName = UpdatedComp->GetClass()->ClassName;
            }
            return FString("MC_") + TargetName;
        }

        // 대상이 없는 경우
        FString DefaultName = MoveComp->GetFName().ToString();
        if (DefaultName.empty())
        {
            DefaultName = MoveComp->GetClass()->ClassName;
        }
        return DefaultName;
    }

	static FString MakeDefaultScriptName(const FString& SceneName, AActor* Actor)
    {
        FString ActorName = "Actor";
        FString ValidSceneName = SceneName.empty() ? "Default" : SceneName;

        if (Actor)
        {
            const UClass* Class = Actor->GetClass();
            ActorName = Class ? Class->ClassName : "Actor";
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
		if (ScriptPath.extension() != L".lua")
		{
			ScriptPath += L".lua";
		}

		if (ScriptPath.is_absolute())
		{
			return FPaths::ToRelativeString(ScriptPath.lexically_normal().wstring());
		}

		FString Normalized = FPaths::Normalize(FPaths::ToUtf8(ScriptPath.generic_wstring()));
		if (Normalized.rfind("LuaScript/", 0) == 0)
		{
			return FPaths::Normalize(FString("Asset/Script/") + Normalized.substr(10));
		}

		if (ScriptPath.has_parent_path())
		{
			return Normalized;
		}

		return FPaths::Normalize(FString("Asset/Script/") + FPaths::ToUtf8(ScriptPath.filename().generic_wstring()));
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
 }

// 1. 메뉴 항목의 이름과, 해당 컴포넌트를 생성&초기화할 함수(람다)를 담는 구조체
struct FComponentMenuEntry
{
	const char* DisplayName;
	std::function<UActorComponent*(AActor*)> CreateAndInitFunc;
};

// 2. 에디터에서 추가 가능한 컴포넌트 배열 (이 리스트만 관리하면 됩니다)
static const TArray<FComponentMenuEntry> ComponentMenuRegistry = {
	{
		"Scene Component",
		[](AActor* Actor) -> UActorComponent* {
			return Actor->AddComponent<USceneComponent>();
		}
	},
	{
		"StaticMesh Component",
		[](AActor* Actor) -> UActorComponent* {
			return Actor->AddComponent<UStaticMeshComponent>();
		}
	},
	{
		"SkeletalMesh Component",
		[](AActor* Actor) -> UActorComponent* {
			return Actor->AddComponent<USkeletalMeshComponent>();
		}
	},
	{
		"SubUV Component",
		[](AActor* Actor) -> UActorComponent* {
			USubUVComponent* Comp = Actor->AddComponent<USubUVComponent>();
			Comp->SetParticle(FName("Explosion"));
			Comp->SetSpriteSize(2.0f, 2.0f);
			Comp->SetFrameRate(30.f);
			return Comp;
		}
	},
	{
		"TextRender Component",
		[](AActor* Actor) -> UActorComponent* {
			UTextRenderComponent* Comp = Actor->AddComponent<UTextRenderComponent>();
			Comp->SetFont(FName("Default"));
			Comp->SetText("TextRender");
			return Comp;
		}
	},
	{
		"Billboard Component",
		[](AActor* Actor) -> UActorComponent* {
			UBillboardComponent* Comp = Actor->AddComponent<UBillboardComponent>();
			Comp->SetTextureName(FEditorResourcePaths::Icon("Pawn_64x.png"));
			return Comp;
		}
	},
	{
		"SpringArm Component",
		[](AActor* Actor) -> UActorComponent* {
			USpringArmComponent* Comp = Actor->AddComponent<USpringArmComponent>();
			Comp->SetRelativeLocation(FVector(0.0f, 0.0f, 1.6f));
			return Comp;
		}
	},
	{
		"Camera Component",
		[](AActor* Actor) -> UActorComponent* {
			UCameraComponent* Comp = Actor->AddComponent<UCameraComponent>();
			return Comp;
		}
	},
	{
		"Sound Component",
		[](AActor* Actor) -> UActorComponent* {
			USoundComponent* Comp = Actor->AddComponent<USoundComponent>();
			return Comp;
		}
	},
	{
		"RotatingMovement Component",
		[](AActor* Actor) -> UActorComponent* {
			URotatingMovementComponent* Comp = Actor->AddComponent<URotatingMovementComponent>();
			return Comp;
		}
	},
    {
		"InterpToMovement Component",
		[](AActor* Actor) -> UActorComponent* {
          UInterpToMovementComponent* Comp = Actor->AddComponent<UInterpToMovementComponent>();
          return Comp;
		}
	},
    {
		"PursuitMovement Component",
		[](AActor* Actor) -> UActorComponent* {
			UPursuitMovementComponent* Comp = Actor->AddComponent<UPursuitMovementComponent>();
			return Comp;
		}
	},
	{
		"ProjectileMovement Component",
		[](AActor* Actor) -> UActorComponent* {
			UProjectileMovementComponent* Comp = Actor->AddComponent<UProjectileMovementComponent>();
			return Comp;
		}
	},
	{
		"HeightFog Component",
		[](AActor* Actor) -> UActorComponent* {
			UHeightFogComponent* Comp = Actor->AddComponent<UHeightFogComponent>();
			Comp->SetFogDensity(0);
			Comp->SetFogInscatteringColor(FVector4(0.72f, 0.8f, 0.9f, 1.0f));
			Comp->SetHeightFalloff(0);
			Comp->SetFogHeight(0);
			return Comp;
		}
	},
	
	{
		"Fireball Component",
		[](AActor* Actor) -> UActorComponent* {
			UFireballComponent* Comp = Actor->AddComponent<UFireballComponent>();
			return Comp;
		}
	},

	{
		"AmbientLight Component",
		[](AActor* Actor) -> UActorComponent* {
			UAmbientLightComponent* Comp = Actor->AddComponent<UAmbientLightComponent>();
			return Comp;
		}
	},

	{
		"DirectionalLight Component",
		[](AActor* Actor) -> UActorComponent* {
			UDirectionalLightComponent* Comp = Actor->AddComponent<UDirectionalLightComponent>();
			return Comp;
		}
	},

	{
		"PointLight Component",
		[](AActor* Actor) -> UActorComponent* {
			UPointLightComponent* Comp = Actor->AddComponent<UPointLightComponent>();
			return Comp;
		}
	},

	{
		"Spotlight Component",
		[](AActor* Actor) -> UActorComponent* {
			USpotlightComponent* Comp = Actor->AddComponent<USpotlightComponent>();
			return Comp;
		}
	},

	{ 
		"Box Component",
		[](AActor* Actor) -> UActorComponent*
		{
			UBoxComponent* Comp = Actor->AddComponent<UBoxComponent>();
			return Comp;
		} 
	},

	{ 
		"Sphere Component",
		[](AActor* Actor) -> UActorComponent*
		{
			USphereComponent* Comp = Actor->AddComponent<USphereComponent>();
			return Comp;
		} 
	},

	{ 
		"Capsule Component",
		[](AActor* Actor) -> UActorComponent*
		{
			UCapsuleComponent* Comp = Actor->AddComponent<UCapsuleComponent>();
			return Comp;
		} 
	},
	{
		"Script Component",
		[](AActor* Actor) -> UActorComponent*
		{
            UScriptComponent* Comp = Actor->AddComponent<UScriptComponent>();
			return Comp;
		} 
	},
	{
		"ActorSequence Component",
		[](AActor* Actor) -> UActorComponent*
		{
			UActorSequenceComponent* Comp = Actor->AddComponent<UActorSequenceComponent>();
			return Comp;
		}
	},
	{
		"Blueprint Component",
		[](AActor* Actor) -> UActorComponent*
		{
			UBlueprintComponent* Comp = Actor->AddComponent<UBlueprintComponent>();
			return Comp;
		}
	},
};

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
		if (LockedName.empty()) LockedName = DisplayActor->GetClass()->ClassName;
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
	ImGui::Text("Class: %s", PrimaryActor->GetClass()->ClassName);

	FString PrimaryName = PrimaryActor->GetFName().ToString();
	if (PrimaryName.empty()) PrimaryName = PrimaryActor->GetClass()->ClassName;

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
		for (const FComponentMenuEntry& Entry : ComponentMenuRegistry)
		{
			if (ImGui::Selectable(Entry.DisplayName))
			{
				if (UActorComponent* NewComp = Entry.CreateAndInitFunc(PrimaryActor))
				{
					AttachAndSelectNewComponent(PrimaryActor, NewComp, AddAttachTarget);
					if (EditorEngine)
					{
						EditorEngine->GetUndoSystem().RecordCreateComponents({ NewComp });
						EditorEngine->GetSceneService().MarkDirty();
					}
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
    if (ActorName.empty()) ActorName = Actor->GetClass()->ClassName;

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
    if (Name.empty()) Name = Comp->GetClass()->ClassName;

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
				if (EditorEngine)
				{
					EditorEngine->GetUndoSystem().RecordSceneComponentAttachment(
						DraggedComp,
						DraggedComp->GetParent(),
						Comp,
						DraggedComp->GetAttachSocketName(),
						FName::None);
				}
                DraggedComp->AttachToComponent(Comp);
				if (EditorEngine)
				{
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
				if (EditorEngine)
				{
					EditorEngine->GetUndoSystem().RecordMovementUpdatedComponent(
						DraggedMoveComp,
						DraggedMoveComp->GetUpdatedComponent(),
						Comp);
				}
                DraggedMoveComp->SetUpdatedComponent(Comp);
				if (EditorEngine)
				{
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
	if (EditorEngine)
	{
		EditorEngine->GetUndoSystem().RecordDeleteComponents({ ComponentToDelete });
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
	ImGui::Text("Actor: %s", PrimaryActor->GetClass()->ClassName.c_str());
	RenderEditableName("Name##Actor", PrimaryActor, &bFocusActorNameNextFrame); // 편집 가능한 UI
	RenderActorTags(PrimaryActor, SelectedActors);

	DrawDetailsSeparator();
	PrimaryActor->SyncActorTransformProperties();
	RenderReflectedProperties(PrimaryActor);

	DrawDetailsSeparator();
	// Billboard 타입 체크
	if (UBillboardComponent* BillboardComp = dynamic_cast<UBillboardComponent*>(PrimaryActor->GetRootComponent()))
	{
		if (dynamic_cast<USubUVComponent*>(PrimaryActor->GetRootComponent()))
		{
			return;
		}
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
				// 경로 전체 대신 파일명만 표시
				FString DisplayName = TexPath;
				bool bSelected = (TexPath == CurrentName);

				if (ImGui::Selectable(DisplayName.c_str(), bSelected))
				{
					TArray<UObject*> ChangedObjects;
					for (AActor* Actor : SelectedActors)
					{
						if (Actor && dynamic_cast<UBillboardComponent*>(Actor->GetRootComponent()))
						{
							ChangedObjects.push_back(Actor);
						}
					}
					const TArray<FEditorObjectState> BeforeStates = EditorEngine
						? EditorEngine->GetUndoSystem().CaptureObjectStates(ChangedObjects, "Edit Billboard")
						: TArray<FEditorObjectState>();
					for (AActor* Actor : SelectedActors)
					{
						if (UBillboardComponent* Comp =
							dynamic_cast<UBillboardComponent*>(Actor->GetRootComponent()))
						{
							Comp->SetTextureName(TexPath);
						}
					}
					if (EditorEngine)
					{
						const TArray<FEditorObjectState> AfterStates =
							EditorEngine->GetUndoSystem().CaptureObjectStates(ChangedObjects, "Edit Billboard");
						EditorEngine->GetUndoSystem().RecordObjectStates(BeforeStates, AfterStates, "Edit Billboard");
						EditorEngine->GetSceneService().MarkDirty();
					}
				}
				if (bSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
	}

	if (PrimaryActor->IsA<ADecalSpotLightActor>())
	{
		ADecalSpotLightActor* SpotActor = static_cast<ADecalSpotLightActor*>(PrimaryActor);
		DrawDetailsSeparator();
		DrawDetailsSectionLabel("Spot Light Properties");
		float Range = SpotActor->GetRange();
		const bool bRangeEdited = ImGui::DragFloat("Range", &Range, 0.1f, 0.0f, 1000.0f);
		if (bRangeEdited)
		{
			TArray<UObject*> ChangedObjects;
			for (AActor* Actor : SelectedActors)
			{
				if (dynamic_cast<ADecalSpotLightActor*>(Actor))
				{
					ChangedObjects.push_back(Actor);
				}
			}
			const TArray<FEditorObjectState> BeforeStates = EditorEngine
				? EditorEngine->GetUndoSystem().CaptureObjectStates(ChangedObjects, "Edit Light")
				: TArray<FEditorObjectState>();
			for (AActor* Actor : SelectedActors)
			{
				if (ADecalSpotLightActor* SA = dynamic_cast<ADecalSpotLightActor*>(Actor))
				{
					SA->SetRange(Range);
				}
			}
			if (EditorEngine)
			{
				const TArray<FEditorObjectState> AfterStates =
					EditorEngine->GetUndoSystem().CaptureObjectStates(ChangedObjects, "Edit Light");
				EditorEngine->GetUndoSystem().RecordObjectStates(BeforeStates, AfterStates, "Edit Light");
				EditorEngine->GetSceneService().MarkDirty();
			}
		}
		float Angle = SpotActor->GetAngle();
		const bool bAngleEdited = ImGui::DragFloat("Angle", &Angle, 0.1f, 0.0f, 180.0f);
		if (bAngleEdited)
		{
			TArray<UObject*> ChangedObjects;
			for (AActor* Actor : SelectedActors)
			{
				if (dynamic_cast<ADecalSpotLightActor*>(Actor))
				{
					ChangedObjects.push_back(Actor);
				}
			}
			const TArray<FEditorObjectState> BeforeStates = EditorEngine
				? EditorEngine->GetUndoSystem().CaptureObjectStates(ChangedObjects, "Edit Light")
				: TArray<FEditorObjectState>();
			for (AActor* Actor : SelectedActors)
			{
				if (ADecalSpotLightActor* SA = dynamic_cast<ADecalSpotLightActor*>(Actor))
				{
					SA->SetAngle(Angle);
				}
			}
			if (EditorEngine)
			{
				const TArray<FEditorObjectState> AfterStates =
					EditorEngine->GetUndoSystem().CaptureObjectStates(ChangedObjects, "Edit Light");
				EditorEngine->GetUndoSystem().RecordObjectStates(BeforeStates, AfterStates, "Edit Light");
				EditorEngine->GetSceneService().MarkDirty();
			}
		}
	}
}

void FEditorPropertyWidget::RenderActorTags(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	if (!PrimaryActor)
	{
		return;
	}

	DrawDetailsSeparator();
	DrawDetailsSectionLabel("Actor Tags");
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
				TArray<UObject*> ChangedObjects;
				for (AActor* Actor : SelectedActors)
				{
					if (Actor && Actor->HasTag(Tag))
					{
						ChangedObjects.push_back(Actor);
					}
				}
				if (!ChangedObjects.empty())
				{
					const TArray<FEditorObjectTagsState> BeforeTags = EditorEngine
						? EditorEngine->GetUndoSystem().CaptureObjectTags(ChangedObjects)
						: TArray<FEditorObjectTagsState>();
					for (UObject* Object : ChangedObjects)
					{
						if (AActor* Actor = Cast<AActor>(Object))
						{
							Actor->RemoveTag(Tag);
						}
					}
					if (EditorEngine)
					{
						const TArray<FEditorObjectTagsState> AfterTags =
							EditorEngine->GetUndoSystem().CaptureObjectTags(ChangedObjects);
						EditorEngine->GetUndoSystem().RecordObjectTags(BeforeTags, AfterTags);
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
		TArray<UObject*> ChangedObjects;
		for (AActor* Actor : SelectedActors)
		{
			if (Actor && !Actor->HasTag(NewTag))
			{
				ChangedObjects.push_back(Actor);
			}
		}
		if (!ChangedObjects.empty())
		{
			const TArray<FEditorObjectTagsState> BeforeTags = EditorEngine
				? EditorEngine->GetUndoSystem().CaptureObjectTags(ChangedObjects)
				: TArray<FEditorObjectTagsState>();
			for (UObject* Object : ChangedObjects)
			{
				if (AActor* Actor = Cast<AActor>(Object))
				{
					Actor->AddTag(NewTag);
				}
			}
			NewActorTagBuffer[0] = '\0';
			if (EditorEngine)
			{
				const TArray<FEditorObjectTagsState> AfterTags =
					EditorEngine->GetUndoSystem().CaptureObjectTags(ChangedObjects);
				EditorEngine->GetUndoSystem().RecordObjectTags(BeforeTags, AfterTags);
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

	DrawDetailsSeparator();
	DrawDetailsSectionLabel("Component Tags");

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
					TArray<UObject*> ChangedObjects;
					ChangedObjects.push_back(Component);
					const TArray<FEditorObjectTagsState> BeforeTags = EditorEngine
						? EditorEngine->GetUndoSystem().CaptureObjectTags(ChangedObjects)
						: TArray<FEditorObjectTagsState>();
					Component->RemoveTag(Tag);
					if (EditorEngine)
					{
						const TArray<FEditorObjectTagsState> AfterTags =
							EditorEngine->GetUndoSystem().CaptureObjectTags(ChangedObjects);
						EditorEngine->GetUndoSystem().RecordObjectTags(BeforeTags, AfterTags);
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
			TArray<UObject*> ChangedObjects;
			ChangedObjects.push_back(Component);
			const TArray<FEditorObjectTagsState> BeforeTags = EditorEngine
				? EditorEngine->GetUndoSystem().CaptureObjectTags(ChangedObjects)
				: TArray<FEditorObjectTagsState>();
			Component->AddTag(NewTag);
			NewComponentTagBuffer[0] = '\0';
			if (EditorEngine)
			{
				const TArray<FEditorObjectTagsState> AfterTags =
					EditorEngine->GetUndoSystem().CaptureObjectTags(ChangedObjects);
				EditorEngine->GetUndoSystem().RecordObjectTags(BeforeTags, AfterTags);
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

	ImGui::Text("Component: %s", SelectedComponent->GetClass()->ClassName.c_str());
	RenderEditableName("Name##Component", SelectedComponent, &bFocusComponentNameNextFrame); // 편집 가능한 UI
	RenderComponentTags(SelectedComponent);

	DrawDetailsSeparator();

	// Reflection 테스트
	const FDetailsPerfClock::time_point PropertiesStart = bDetailsPerfTraceFrame ? FDetailsPerfClock::now() : FDetailsPerfClock::time_point{};
	RenderReflectedProperties(SelectedComponent);
	RenderReflectedFunctions(SelectedComponent);
	const FDetailsPerfClock::time_point PropertiesEnd = bDetailsPerfTraceFrame ? FDetailsPerfClock::now() : FDetailsPerfClock::time_point{};

	AActor* Owner = SelectedComponent->GetOwner();
	double PropertyWidgetMs = 0.0;

	// Special: InterpToMovementComponent control points + behaviour + actions
	double SkeletalDebugMs = 0.0;
	if (USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(SelectedComponent))
	{
		const FDetailsPerfClock::time_point SkeletalStart = bDetailsPerfTraceFrame ? FDetailsPerfClock::now() : FDetailsPerfClock::time_point{};
		RenderSkeletalAnimationStateMachineProperty(SkeletalComp);
		RenderSkeletalBonePoseDebug(SkeletalComp);
		if (bDetailsPerfTraceFrame)
		{
			SkeletalDebugMs = DetailsPerfMs(SkeletalStart, FDetailsPerfClock::now());
		}
	}

	if (UActorSequenceComponent* SequenceComp = Cast<UActorSequenceComponent>(SelectedComponent))
	{
		ActorSequenceDetails.Render(SequenceComp, LastDeltaTime);
	}

	if (ULightComponent* LightComp = Cast<ULightComponent>(SelectedComponent))
	{
		if (ImGui::Button("Override camera with light's perspective"))
		{
			UWorld* World = GEngine ? GEngine->GetWorld() : nullptr;
			FViewportCamera* Camera = World ? World->GetActiveCamera() : nullptr;
			if (Camera)
			{
				Camera->SetLocation(LightComp->GetWorldLocation());
				Camera->SetRotation(LightComp->GetRelativeQuat());
			}
		}
    }
    else if (UScriptComponent* ScriptComp = Cast<UScriptComponent>(SelectedComponent))
    {
        TArray<FPropertyDescriptor> ScriptProperties;
        ScriptComp->GetEditableProperties(ScriptProperties);

        bool bDrewScriptSection = false;
        for (FPropertyDescriptor& Prop : ScriptProperties)
        {
            if (std::strcmp(Prop.Name, "Tags") == 0)
            {
                continue;
            }

            if (!bDrewScriptSection)
            {
                DrawDetailsSeparator();
                DrawDetailsSectionLabel("Script");
                bDrewScriptSection = true;
            }

            RenderPropertyWidget(Prop);
        }

        FScriptManager& ScriptMgr = FScriptManager::Get();
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
                const FEditorObjectState BeforeScriptComponentState = EditorEngine
                    ? EditorEngine->GetUndoSystem().CaptureObjectState(ScriptComp, "Create Script")
                    : FEditorObjectState{};
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

                if (EditorEngine)
                {
                    const FEditorFileSystemState AfterFileState =
                        EditorEngine->GetUndoSystem().CaptureFileSystemState(SelectedScriptPath, "Create Script");
                    EditorEngine->GetUndoSystem().RecordCreateFileSystemPath(AfterFileState, "Create Script");
                }

                ScriptComp->SetScriptName(MakeScriptReferenceFromPath(SelectedScriptPath));
                ScriptComp->ReloadLuaProperties();
                if (EditorEngine)
                {
                    const FEditorObjectState AfterScriptComponentState =
                        EditorEngine->GetUndoSystem().CaptureObjectState(ScriptComp, "Create Script");
                    EditorEngine->GetUndoSystem().RecordObjectState(
                        BeforeScriptComponentState,
                        AfterScriptComponentState,
                        "Assign Script");
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
	}
	ImGui::Separator();

	// 프로퍼티 직접 편집 후 월드 행렬 갱신
	if (SelectedComponent->IsA<USceneComponent>())
	{
        UWorld* World = Owner->GetFocusedWorld();
        const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(World);
		static_cast<USceneComponent*>(SelectedComponent)->MarkTransformDirty();
		Ctx->SelectionManager->GetGizmo()->UpdateGizmoTransform();
	}

	//if (bDetailsPerfTraceFrame)
	//{
	//	const double GetEditablePropertiesMs = DetailsPerfMs(PropertiesStart, PropertiesEnd);
	//	const double TotalMs = DetailsPerfMs(TotalStart, FDetailsPerfClock::now());
	//	UE_LOG(
	//		"[DetailsPerf] Component=%s Type=%s Total=%.2fms GetEditableProperties=%.2fms PropertyWidgets=%.2fms SkeletalDebug=%.2fms Props=%zu",
	//		SelectedComponent ? SelectedComponent->GetFName().ToString().c_str() : "<None>",
	//		SelectedComponent ? SelectedComponent->GetClass()->ClassName : "<None>",
	//		TotalMs,
	//		GetEditablePropertiesMs,
	//		PropertyWidgetMs,
	//		SkeletalDebugMs,
	//		Properties.size());
	//}
}

void FEditorPropertyWidget::RenderSceneComponentRefWidget(FPropertyDescriptor& Prop, AActor* Owner)
{
	// ValuePtr은 USceneComponent* 변수의 주소 (USceneComponent**)
	USceneComponent** ValuePtr = reinterpret_cast<USceneComponent**>(Prop.ValuePtr);
	USceneComponent* CurrentComp = *ValuePtr;

	// 액터 소유 SceneComponent 목록 수집
	TArray<USceneComponent*> SceneComps;
	SceneComps.push_back(nullptr); // "None" 선택지
	if (Owner)
	{
		for (UActorComponent* Comp : Owner->GetComponents())
		{
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
				SceneComps.push_back(SceneComp);
		}
	}

	// 드롭다운 레이블 생성: "[Root] ClassName" 또는 "ClassName [FName]"
	auto GetLabel = [&](USceneComponent* Comp) -> FString {
		if (!Comp) return "None";
		FString Name = Comp->GetFName().ToString();
		if (Name.empty()) Name = Comp->GetClass()->ClassName;
		bool bIsRoot = Owner && (Comp == Owner->GetRootComponent());
		return bIsRoot ? ("[Root] " + Name) : Name;
	};

	FString CurrentLabel = GetLabel(CurrentComp);
	if (ImGui::BeginCombo(Prop.Name, CurrentLabel.c_str()))
	{
		for (USceneComponent* SceneComp : SceneComps)
		{
			bool bSelected = (SceneComp == CurrentComp);
			// ##ptr 으로 포인터를 ID로 사용하여 동일 이름 컴포넌트를 구별
			char SelectableId[128];
			snprintf(SelectableId, sizeof(SelectableId), "%s##%p",
				GetLabel(SceneComp).c_str(), static_cast<void*>(SceneComp));
			if (ImGui::Selectable(SelectableId, bSelected))
			{
				if (EditorEngine)
				{
					if (UMovementComponent* MovementComponent = Cast<UMovementComponent>(SelectedComponent))
					{
						EditorEngine->GetUndoSystem().RecordMovementUpdatedComponent(
							MovementComponent,
							CurrentComp,
							SceneComp);
					}
				}
				*ValuePtr = SceneComp;
				SelectedComponent->PostEditChangeProperty({ Prop.Name, EPropertyChangeType::ValueSet });
				if (EditorEngine)
				{
					EditorEngine->GetSceneService().MarkDirty();
				}
			}
			if (bSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
}

void FEditorPropertyWidget::RenderPropertyWidget(FPropertyDescriptor& Prop)
{
	bool bChanged = false;
	const FEditorObjectState BeforeState = (EditorEngine && SelectedComponent)
		? EditorEngine->GetUndoSystem().CaptureObjectState(SelectedComponent, "Edit Property")
		: FEditorObjectState();

	switch (Prop.Type)
	{
	case EPropertyType::Bool:
	{
		bool* Val = static_cast<bool*>(Prop.ValuePtr);
		bChanged = ImGui::Checkbox(Prop.Name, Val);
		break;
	}
	case EPropertyType::Int:
	{
		int32* Val = static_cast<int32*>(Prop.ValuePtr);
		bChanged = ImGui::DragInt(Prop.Name, Val);
		break;
	}
	case EPropertyType::Float:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		if (Prop.Min != 0.0f || Prop.Max != 0.0f)
			bChanged = ImGui::DragFloat(Prop.Name, Val, Prop.Speed, Prop.Min, Prop.Max);
		else
			bChanged = ImGui::DragFloat(Prop.Name, Val, Prop.Speed);
		break;
	}
	case EPropertyType::Vec3:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		bChanged = ImGui::DragFloat3(Prop.Name, Val, Prop.Speed);
		break;
	}
	case EPropertyType::Vec4:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		bChanged = ImGui::ColorEdit4(Prop.Name, Val);
		break;
	}
	case EPropertyType::Color:
	{
		FColor* Val = static_cast<FColor*>(Prop.ValuePtr);
		bChanged = ImGui::ColorEdit4(Prop.Name, &Val->R);
		break;
	}
	case EPropertyType::String:
	{
		FString* Val = static_cast<FString*>(Prop.ValuePtr);

		if (strcmp(Prop.Name, "StaticMesh") == 0)
		{
			const TArray<FString>& MeshPaths = EditorEngine
				? EditorEngine->GetAssetService().GetStaticMeshAssetPaths()
				: EmptyAssetNames();
			if (!MeshPaths.empty())
			{
				const FString Current = *Val;
				if (ImGui::BeginCombo(Prop.Name, Current.empty() ? "<None>" : Current.c_str()))
				{
					for (const FString& Path : MeshPaths)
					{
						const bool bSelected = (Current == Path);
						if (ImGui::Selectable(Path.c_str(), bSelected))
						{
							*Val = Path;
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
		}
		else if (strcmp(Prop.Name, "SkeletalMesh") == 0)
		{
			if (!Val->empty() && !HasLowerExtension(*Val, ".uasset"))
			{
				Val->clear();
				bChanged = true;
			}

			const TArray<FString>& MeshPaths = EditorEngine
				? EditorEngine->GetAssetService().GetSkeletalMeshAssetPaths()
				: EmptyAssetNames();
			if (!MeshPaths.empty())
			{
				const FString Current = *Val;
				if (ImGui::BeginCombo(Prop.Name, Current.empty() ? "<None>" : Current.c_str()))
				{
					for (const FString& Path : MeshPaths)
					{
						const bool bSelected = (Current == Path);
						if (ImGui::Selectable(Path.c_str(), bSelected))
						{
							*Val = Path;
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
				ImGui::TextDisabled("No SkeletalMesh .uasset assets");
			}
		}
		else if (strcmp(Prop.Name, "Animation") == 0)
		{
			if (!Val->empty() && !HasLowerExtension(*Val, ".uasset"))
			{
				Val->clear();
				bChanged = true;
			}

			const TArray<FString>& AnimationPaths = EditorEngine
				? EditorEngine->GetAssetService().GetAnimSequenceAssetPaths()
				: EmptyAssetNames();
			if (!AnimationPaths.empty())
			{
				const FString Current = *Val;
				if (ImGui::BeginCombo(Prop.Name, Current.empty() ? "<None>" : Current.c_str()))
				{
					if (ImGui::Selectable("<None>", Current.empty()))
					{
						Val->clear();
						bChanged = true;
					}
					for (const FString& Path : AnimationPaths)
					{
						const bool bSelected = (Current == Path);
						if (ImGui::Selectable(Path.c_str(), bSelected))
						{
							*Val = Path;
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
				ImGui::TextDisabled("No AnimSequence .uasset assets");
			}
		}
        else if (strcmp(Prop.Name, "ScriptName") == 0)
        {
            if (!Val)
            {
                return;
            }

            FScriptManager::Get().RefreshLuaScriptFiles();

            TMap<FName, FLuaScriptInfo, FName::Hash>& ScriptArray =
                FScriptManager::Get().GetScriptArray();

            ImGui::PushID(Val);

            const FString Current = MakeScriptReferenceFromPath(*Val);
            if (Current != *Val)
            {
                *Val = Current;
                bChanged = true;
            }

            FString Preview = "SelectScript";
            FString CurrentResolvedPath = Current;
            if (FLuaScriptInfo* CurrentInfo = FScriptManager::Get().GetScriptInfo(FName(Current)))
            {
                if (!CurrentInfo->ScriptPath.empty())
                {
                    CurrentResolvedPath = FPaths::ToRelativeString(CurrentInfo->ScriptPath);
                }
            }

            for (const auto& [ScriptName, ScriptInfo] : ScriptArray)
            {
                const FString Key = ScriptName.ToString();
                const FString RelativePath = ScriptInfo.ScriptPath.empty()
                    ? FString()
                    : FPaths::ToRelativeString(ScriptInfo.ScriptPath);

                if (Key == Current || RelativePath == Current || RelativePath == CurrentResolvedPath)
                {
                    if (!ScriptInfo.ScriptPath.empty())
                    {
                        Preview = RelativePath;
                    }

                    if (Preview.empty())
                    {
                        Preview = Key;
                    }

                    break;
                }
            }

            const float ClearButtonWidth = 46.0f;
            const float ScriptSlotWidth = std::max(
                120.0f,
                ImGui::GetContentRegionAvail().x - ClearButtonWidth - ImGui::GetStyle().ItemSpacing.x);

            ImGui::SetNextItemWidth(ScriptSlotWidth);
            const bool bComboOpen = ImGui::BeginCombo("ScriptName", Preview.c_str());
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("LuaScriptContentItem"))
                {
                    const char* PayloadPath = static_cast<const char*>(Payload->Data);
                    if (PayloadPath && PayloadPath[0] != '\0')
                    {
                        const FString NewScriptRef = MakeScriptReferenceFromPath(PayloadPath);
                        if (!NewScriptRef.empty())
                        {
                            *Val = NewScriptRef;
                            bChanged = true;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemHovered() && !Current.empty())
            {
                ImGui::SetTooltip("%s", Current.c_str());
            }

            if (bComboOpen)
            {
                if (ImGui::Selectable("<None>", Current.empty()))
                {
                    Val->clear();
                    bChanged = true;
                }

                for (const auto& [ScriptName, ScriptInfo] : ScriptArray)
                {
                    const FString Key = ScriptName.ToString();

                    if (Key.empty())
                    {
                        continue;
                    }

                    if (ScriptInfo.ScriptPath.empty())
                    {
                        continue;
                    }

                    if (!std::filesystem::exists(ScriptInfo.ScriptPath))
                    {
                        continue;
                    }

                    FString RelativePath = FPaths::ToRelativeString(ScriptInfo.ScriptPath);

                    // ImGui label은 절대 빈 문자열이면 안 됨
                    FString DisplayName = RelativePath.empty() ? Key : RelativePath;

                    const bool bSelected = (Current == Key || Current == RelativePath || CurrentResolvedPath == RelativePath);

                    ImGui::PushID(Key.c_str());

                    if (ImGui::Selectable(DisplayName.c_str(), bSelected))
                    {
                        *Val = Key;
                        bChanged = true;
                    }

                    ImGui::PopID();

                    if (bSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(Current.empty());
            if (ImGui::Button("Clear", ImVec2(ClearButtonWidth, 0.0f)))
            {
                Val->clear();
                bChanged = true;
            }
            ImGui::EndDisabled();

            const FString NewCurrent = *Val;

            if (!NewCurrent.empty() && !FScriptManager::Get().HasScript(FName(NewCurrent)))
            {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.42f, 0.35f, 1.0f),
                    "Missing script file.");

                ImGui::SameLine();

                if (ImGui::SmallButton("Clear##MissingScript"))
                {
                    Val->clear();
                    bChanged = true;
                }
            }

            ImGui::PopID();
        }
		else
		{
			char Buf[256];
			strncpy_s(Buf, sizeof(Buf), Val->c_str(), _TRUNCATE);
			if (ImGui::InputText(Prop.Name, Buf, sizeof(Buf)))
			{
				*Val = Buf;
				bChanged = true;
			}
		}
		break;
	}
	case EPropertyType::Name:
	{
		FName* Val = static_cast<FName*>(Prop.ValuePtr);
		FString Current = Val->ToString();

		if (strcmp(Prop.Name, "Particle") == 0)
		{
			if (UBillboardComponent* BillboardComp = Cast<UBillboardComponent>(SelectedComponent))
			{
				if (!Cast<USubUVComponent>(SelectedComponent))
				{
					const TArray<FString>& TexturePaths = EditorEngine
						? EditorEngine->GetAssetService().GetTextureAssetPaths()
						: EmptyAssetNames();
					if (ImGui::BeginCombo("Sprite Texture", Current.empty() ? "<None>" : Current.c_str()))
					{
						for (const FString& TexturePath : TexturePaths)
						{
							const bool bSelected = (Current == TexturePath);
							if (ImGui::Selectable(TexturePath.c_str(), bSelected))
							{
								BillboardComp->SetTextureName(TexturePath);
								bChanged = true;
							}
							if (bSelected)
							{
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
					break;
				}
			}
		}

		// 리소스 키와 매칭되는 프로퍼티면 콤보 박스로 렌더링
		TArray<FString> Names;
		if (strcmp(Prop.Name, "Font") == 0)
			Names = EditorEngine ? EditorEngine->GetAssetService().GetFontNames() : EmptyAssetNames();
		else if (strcmp(Prop.Name, "Particle") == 0)
			Names = EditorEngine ? EditorEngine->GetAssetService().GetParticleNames() : EmptyAssetNames();

		if (!Names.empty())
		{
			if (ImGui::BeginCombo(Prop.Name, Current.c_str()))
			{
				for (const auto& Name : Names)
				{
					bool bSelected = (Current == Name);
					if (ImGui::Selectable(Name.c_str(), bSelected))
					{
						*Val = FName(Name);
						bChanged = true;
					}
					if (bSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		else
		{
			char Buf[256];
			strncpy_s(Buf, sizeof(Buf), Current.c_str(), _TRUNCATE);
			if (ImGui::InputText(Prop.Name, Buf, sizeof(Buf)))
			{
				*Val = FName(Buf);
				bChanged = true;
			}
		}
		break;
	}
	case EPropertyType::Material:
	{
		TArray<UMaterialInterface*>* Slots = static_cast<TArray<UMaterialInterface*>*>(Prop.ValuePtr);
		if (!Slots || !EditorEngine)
		{
			break;
		}
		FEditorAssetService& AssetService = EditorEngine->GetAssetService();
		const TArray<FString>& MaterialNames = AssetService.GetMaterialInterfaceNames();

		DrawDetailsSeparator();
		DrawDetailsSectionLabel(Prop.Name);
		ImGui::PushID(Prop.Name);
		for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(Slots->size()); ++SlotIndex)
		{
			ImGui::PushID(SlotIndex);
			UMaterialInterface* CurrentMaterial = (*Slots)[SlotIndex];
			const FString CurrentMaterialIdentifier = CurrentMaterial
				? (CurrentMaterial->GetFilePath().empty() ? CurrentMaterial->GetName() : FPaths::Normalize(CurrentMaterial->GetFilePath()))
				: FString();
			const FString CurrentLabel = CurrentMaterial
				? (CurrentMaterial->GetFilePath().empty() ? CurrentMaterial->GetName() : FPaths::Normalize(CurrentMaterial->GetFilePath()))
				: FString("None");

			ImGui::Text("Slot %d", SlotIndex);
			ImGui::SameLine();

			const float EditButtonWidth = 48.0f;
			ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - EditButtonWidth - ImGui::GetStyle().ItemSpacing.x));
			if (ImGui::BeginCombo("##MaterialSlot", CurrentLabel.c_str()))
			{
				const bool bNoneSelected = CurrentMaterial == nullptr;
				if (ImGui::Selectable("None", bNoneSelected))
				{
					if (EditorEngine)
					{
						if (UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(SelectedComponent))
						{
							EditorEngine->GetUndoSystem().RecordMaterialSlot(
								PrimitiveComp,
								SlotIndex,
								CurrentMaterial,
								nullptr);
						}
						bPropertyEditUndoCaptured = true;
					}
					(*Slots)[SlotIndex] = nullptr;
					bChanged = true;
				}
				if (bNoneSelected)
				{
					ImGui::SetItemDefaultFocus();
				}

				for (int32 MaterialIndex = 0; MaterialIndex < static_cast<int32>(MaterialNames.size()); ++MaterialIndex)
				{
					ImGui::PushID(MaterialIndex);
					const FString& MaterialLabel = MaterialNames[MaterialIndex].empty()
						? FString("<Unnamed Material>")
						: MaterialNames[MaterialIndex];
					const bool bSelected = (CurrentMaterialIdentifier == MaterialLabel);
					if (ImGui::Selectable(MaterialLabel.c_str(), bSelected))
					{
						UMaterialInterface* Candidate = AssetService.ResolveMaterialInterfaceByIndex(MaterialIndex);
						if (!Candidate)
						{
							ImGui::PopID();
							continue;
						}
						if (EditorEngine)
						{
							if (UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(SelectedComponent))
							{
								EditorEngine->GetUndoSystem().RecordMaterialSlot(
									PrimitiveComp,
									SlotIndex,
									CurrentMaterial,
									Candidate);
							}
							bPropertyEditUndoCaptured = true;
						}
						(*Slots)[SlotIndex] = Candidate;
						bChanged = true;
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

			ImGui::SameLine();
			if (ImGui::Button("Edit"))
			{
				if (UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(SelectedComponent))
				{
					EditorEngine->GetMainPanel().OpenMaterialSlot(PrimitiveComp, SlotIndex);
				}
			}
			if (ImGui::IsItemHovered())
			{
				if (CurrentMaterial)
				{
					RenderMaterialPreviewTooltip(CurrentMaterial);
				}
				else
				{
					ImGui::SetTooltip("Open this material slot in Material Editor");
				}
			}
			ImGui::PopID();
		}
		ImGui::PopID();
		break;
	}
    case EPropertyType::Enum:
	{
		int* Val = static_cast<int*>(Prop.ValuePtr);
		if (Prop.EnumNames && Prop.EnumCount)
			bChanged = ImGui::Combo(Prop.Name, Val, Prop.EnumNames, Prop.EnumCount);
		break;
	}
	case EPropertyType::Vec3Array:
	{
		TArray<FVector>* Arr = static_cast<TArray<FVector>*>(Prop.ValuePtr);
		int32 ToRemove = -1;

		ImGui::Text("%s", Prop.Name);
		ImGui::Spacing();

		for (int32 i = 0; i < static_cast<int32>(Arr->size()); i++)
		{
			ImGui::PushID(i);

			float Val[3] = { (*Arr)[i].X, (*Arr)[i].Y, (*Arr)[i].Z };
			char Label[32];
			snprintf(Label, sizeof(Label), "[%d]", i);

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - UIConstants::XButtonSize - 8.0f);
			if (ImGui::DragFloat3(Label, Val, 1.0f))
			{
				(*Arr)[i] = FVector(Val[0], Val[1], Val[2]);
				bChanged = true;
			}

			ImGui::SameLine();
			char XId[32];
			snprintf(XId, sizeof(XId), "##rm_%d", i);
			if (DrawXButton(XId)) ToRemove = i;

			ImGui::PopID();
		}

		if (ToRemove >= 0)
		{
			Arr->erase(Arr->begin() + ToRemove);
			bChanged = true;
		}

		char AddLabel[64];
		snprintf(AddLabel, sizeof(AddLabel), "+ Add##%s", Prop.Name);
		if (ImGui::Button(AddLabel, ImVec2(-1, 0)))
		{
			Arr->push_back(Arr->empty() ? FVector(0.f, 0.f, 0.f) : Arr->back());
			bChanged = true;
		}
		break;
	}
	case EPropertyType::SRV:
	{
		ID3D11ShaderResourceView* SRV = static_cast<ID3D11ShaderResourceView*>(Prop.ValuePtr);
		if (SRV)
		{
			const FSRVDisplayInfo* Info = static_cast<const FSRVDisplayInfo*>(Prop.ExtraData);
			if (Info)
			{
				ImGui::Image(SRV,
					ImVec2(Info->ImageWidth, Info->ImageHeight),
					ImVec2(Info->UV0X, Info->UV0Y),
					ImVec2(Info->UV1X, Info->UV1Y));
			}
			else
			{
				ImGui::Image(SRV, ImVec2(256, 256));
			}
		}
		break;
	}
    case EPropertyType::CubeSRV:
    {
        auto CubeSRV = static_cast<ID3D11ShaderResourceView**>(Prop.ValuePtr);
        if (CubeSRV)
        {
            for (int i = 0; i < 6; i++)
            {
                if (!CubeSRV[i])
                {
                    continue;
                }

                ImGui::Image(CubeSRV[i], ImVec2(64, 64));

                if ((i % 3) != 2)
                {
                    ImGui::SameLine();
                }
            }
        }
        break;
    }
	}

	if (bChanged && SelectedComponent)
	{
		const bool bInteractiveEdit = ImGui::IsAnyItemActive();
		if (bInteractiveEdit && !bPropertyEditStateCaptured && BeforeState.IsValid())
		{
			PendingPropertyEditObject = SelectedComponent;
			PendingPropertyEditLabel = "Edit Property";
			PendingPropertyEditBeforeState = BeforeState;
			bPropertyEditStateCaptured = true;
			bPropertyEditUndoCaptured = true;
		}
		SelectedComponent->PostEditChangeProperty({ Prop.Name, EPropertyChangeType::ValueSet });
		if (EditorEngine)
		{
			EditorEngine->GetSceneService().MarkDirty();
		}
		if (!bInteractiveEdit)
		{
			RecordImmediateObjectStateEdit(SelectedComponent, BeforeState, "Edit Property");
		}
	}

	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		CommitPropertyEditState(SelectedComponent, "Edit Property");
	}

	if (!ImGui::IsAnyItemActive())
	{
		bPropertyEditUndoCaptured = false;
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

void FEditorPropertyWidget::RenderReflectedProperties(UObject* Object)
{
	if (!Object || !Object->GetClass()) return;

	TArray<FProperty*> Props;
	Object->GetClass()->GetAllProperties(Props);

	TMap<FString, TArray<FProperty*>> PropertiesByCategory;
	TArray<FString> CategoryOrder;

	for (FProperty* Prop : Props)
	{
		if (!Prop) continue;
		if (!Prop->IsVisible()) continue;

		FString Category = Prop->Category.empty() ? "Default" : Prop->Category;

		if (PropertiesByCategory.find(Category) == PropertiesByCategory.end())
		{
			PropertiesByCategory[Category] = {};
			CategoryOrder.push_back(Category);
		}

		PropertiesByCategory[Category].push_back(Prop);
	}

	for (const FString& Category : CategoryOrder)
	{
		ImGui::PushID(Category.c_str());

		const FString CategoryHeaderLabel = Category + "##PropertyCategory";
		if (ImGui::CollapsingHeader(CategoryHeaderLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (FProperty* Prop : PropertiesByCategory[Category])
			{
				RenderReflectedPropertyWidget(Object, Prop);
			}
		}

		ImGui::PopID();
	}
}

void FEditorPropertyWidget::RenderReflectedFunctions(UObject* Object)
{
	if (!Object || !Object->GetClass()) return;

	TArray<FFunction*> Functions;
	Object->GetClass()->GetAllFunctions(Functions);

	bool bDrewAny = false;

	for (FFunction* Function : Functions)
	{
		if (!Function) continue;
		if (!Function->HasAnyFunctionFlags(static_cast<uint32>(EFunctionFlags::CallInEditor))) continue;
		if (!Function->Parameters.empty()) continue;

		if (!bDrewAny)
		{
			DrawDetailsSeparator();
			DrawDetailsSectionLabel("Functions");
			bDrewAny = true;
		}

		const char* Label = Function->DisplayName.empty()
			? Function->Name.c_str()
			: Function->DisplayName.c_str();

		if (ImGui::Button(Label))
		{
			const FEditorObjectState BeforeState = EditorEngine
				? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Call Function")
				: FEditorObjectState();

			Function->Invoke(Object);
			Object->PostEditChangeProperty({ Label, EPropertyChangeType::ValueSet });
			RecordImmediateObjectStateEdit(Object, BeforeState, "Call Function");

			if (EditorEngine)
			{
				EditorEngine->GetSceneService().MarkDirty();
				EditorEngine->GetNotificationService().Info("Function called");
			}
		}
	}
}

void FEditorPropertyWidget::RenderReflectedPropertyWidget(UObject* Object, FProperty* Property)
{
	if (!Object || !Property) return;
	
	ImGui::PushID(Property);
	RenderPropertyValue(Object, Property, Property->GetValueAddress(Object), Property->DisplayName.c_str(), Property);
	ImGui::PopID();
}

void FEditorPropertyWidget::RenderPropertyValue(UObject* OwnerObject, FProperty* Property, void* ValueAddress, const char* Label, FProperty* NotifyProperty)
{
	const float Speed = Property->Delta > 0.0f ? Property->Delta : 0.1f;
	const float Min = Property->bHasClampMin ? Property->ClampMin : -FLT_MAX;
	const float Max = Property->bHasClampMax ? Property->ClampMax : FLT_MAX;

	FProperty* EditPolicyProperty = NotifyProperty ? NotifyProperty : Property;
	const bool bEditable = EditPolicyProperty->IsEditable();

	if (!bEditable)
	{
		ImGui::BeginDisabled();
	}

	if (FBoolProperty* BoolProp = dynamic_cast<FBoolProperty*>(Property))
	{
		bool Value = *reinterpret_cast<bool*>(ValueAddress);
		if (ImGui::Checkbox(Label, &Value))
		{
			BeginPropertyEditState(OwnerObject, "Edit Property");
			*reinterpret_cast<bool*>(ValueAddress) = Value;
			PostEditReflectedProperty(OwnerObject, NotifyProperty);
			CommitPropertyEditState(OwnerObject, "Edit Property");
		}
	}

	if (FFloatProperty* FloatProp = dynamic_cast<FFloatProperty*>(Property))
	{
		float Value = *reinterpret_cast<float*>(ValueAddress);
		if (ImGui::DragFloat(Label, &Value, Speed, Min, Max))
		{
			BeginPropertyEditState(OwnerObject, "Edit Property");
			*reinterpret_cast<float*>(ValueAddress) = Value;
			PostEditReflectedProperty(OwnerObject, NotifyProperty);
		}
	}

	if (FIntProperty* IntProp = dynamic_cast<FIntProperty*>(Property))
	{
		int32 Value = *reinterpret_cast<int32*>(ValueAddress);
		if (ImGui::DragInt(Label, &Value))
		{
			BeginPropertyEditState(OwnerObject, "Edit Property");
			*reinterpret_cast<int32*>(ValueAddress) = Value;
			PostEditReflectedProperty(OwnerObject, NotifyProperty);
		}
	}

	if (FVectorProperty* VecProp = dynamic_cast<FVectorProperty*>(Property))
	{
		FVector Value = *reinterpret_cast<FVector*>(ValueAddress);
		if (ImGui::DragFloat3(Label, &Value.X, Speed, Min, Max))
		{
			BeginPropertyEditState(OwnerObject, "Edit Property");
			*reinterpret_cast<FVector*>(ValueAddress) = Value;
			PostEditReflectedProperty(OwnerObject, NotifyProperty);
		}
	}

	if (FVector2Property* Vec2Prop = dynamic_cast<FVector2Property*>(Property))
	{
		FVector2 Value = *reinterpret_cast<FVector2*>(ValueAddress);
		if (ImGui::DragFloat2(Label, &Value.X, Speed, Min, Max))
		{
			BeginPropertyEditState(OwnerObject, "Edit Property");
			*reinterpret_cast<FVector2*>(ValueAddress) = Value;
			PostEditReflectedProperty(OwnerObject, NotifyProperty);
		}
	}

	if (FVector4Property* Vec4Prop = dynamic_cast<FVector4Property*>(Property))
	{
		FVector4 Value = *reinterpret_cast<FVector4*>(ValueAddress);
		if (ImGui::DragFloat4(Label, &Value.X, Speed, Min, Max))
		{
			BeginPropertyEditState(OwnerObject, "Edit Property");
			*reinterpret_cast<FVector4*>(ValueAddress) = Value;
			PostEditReflectedProperty(OwnerObject, NotifyProperty);
		}
	}

	if (FStringProperty* StringProp = dynamic_cast<FStringProperty*>(Property))
	{
		FString Value = *reinterpret_cast<FString*>(ValueAddress);
		char Buffer[256];
		strncpy_s(Buffer, sizeof(Buffer), Value.c_str(), _TRUNCATE);

		if (ImGui::InputText(Label, Buffer, sizeof(Buffer)))
		{
			BeginPropertyEditState(OwnerObject, "Edit Property");
			*reinterpret_cast<FString*>(ValueAddress) = FString(Buffer);
			PostEditReflectedProperty(OwnerObject, NotifyProperty);
		}
	}

	if (FNameProperty* NameProp = dynamic_cast<FNameProperty*>(Property))
	{
		FName Value = *reinterpret_cast<FName*>(ValueAddress);
		char Buffer[256];
		strncpy_s(Buffer, sizeof(Buffer), Value.ToString().c_str(), _TRUNCATE);
		if (ImGui::InputText(Label, Buffer, sizeof(Buffer)))
		{
			BeginPropertyEditState(OwnerObject, "Edit Property");
			*reinterpret_cast<FName*>(ValueAddress) = FName(Buffer);
			PostEditReflectedProperty(OwnerObject, NotifyProperty);
		}
	}

	if (FColorProperty* ColorProp = dynamic_cast<FColorProperty*>(Property))
	{
		FColor Value = *reinterpret_cast<FColor*>(ValueAddress);
		if (ImGui::ColorEdit4(Label, &Value.R))
		{
			BeginPropertyEditState(OwnerObject, "Edit Property");
			*reinterpret_cast<FColor*>(ValueAddress) = Value;
			PostEditReflectedProperty(OwnerObject, NotifyProperty);
		}
	}

	if (FEnumProperty* EnumProp = dynamic_cast<FEnumProperty*>(Property))
	{
		UEnum* Enum = FEnumRegistry::Get().FindEnum(EnumProp->EnumName);
		if (!Enum)
		{
			if (!bEditable)
			{
				ImGui::EndDisabled();
			}
		}

		int32 Value = *reinterpret_cast<int32*>(ValueAddress);

		int32 CurrentIndex = 0;
		for (int32 i = 0; i < Enum->NumEnums(); ++i)
		{
			if (Enum->GetValueByIndex(i) == Value)
			{
				CurrentIndex = i;
				break;
			}
		}

		const char* Preview = Enum->GetNameByIndex(CurrentIndex).c_str();
		if (ImGui::BeginCombo(Label, Preview))
		{
			for (int32 i = 0; i < Enum->NumEnums(); ++i)
			{
				bool bSelected = i == CurrentIndex;
				if (ImGui::Selectable(Enum->GetNameByIndex(i).c_str(), bSelected))
				{
					BeginPropertyEditState(OwnerObject, "Edit Property");
					*reinterpret_cast<int32*>(ValueAddress) = static_cast<int32>(Enum->GetValueByIndex(i));
					PostEditReflectedProperty(OwnerObject, NotifyProperty);
					CommitPropertyEditState(OwnerObject, "Edit Property");
				}

				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}
	}

	if (FObjectProperty* ObjectProp = dynamic_cast<FObjectProperty*>(Property))
	{
		RenderObjectPropertyValue(OwnerObject, ObjectProp, ValueAddress, Label, NotifyProperty);
	}

	if (FArrayProperty* ArrayProp = dynamic_cast<FArrayProperty*>(Property))
	{
		RenderArrayProperty(OwnerObject, ArrayProp);
	}

	if (FStructProperty* StructProp = dynamic_cast<FStructProperty*>(Property))
	{
		RenderStructPropertyValue(OwnerObject, StructProp, ValueAddress, Label, NotifyProperty);
	}

	if (!bEditable)
	{
		ImGui::EndDisabled();
	}

	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		CommitPropertyEditState(OwnerObject, "Edit Property");
	}
}

void FEditorPropertyWidget::RenderObjectPropertyValue(UObject* OwnerObject, FObjectProperty* Property, void* ValueAddress, const char* Label, FProperty* NotifyProperty)
{
	if (!Property || !ValueAddress) return;

	if (Property->PropertyClass && Property->PropertyClass->IsChildOf(UMaterialInterface::StaticClass()))
	{
		RenderMaterialAssetProperty(OwnerObject, Property, ValueAddress, Label, NotifyProperty);
	}
	else if (NotifyProperty == Property)
	{
		if (Property->AcceptsClass(UStaticMesh::StaticClass()))
		{
			RenderStaticMeshAssetProperty(OwnerObject, Property);
		}
		else if (Property->AcceptsClass(USkeletalMesh::StaticClass()))
		{
			RenderSkeletalMeshAssetProperty(OwnerObject, Property);
		}
		else if (Property->AcceptsClass(UAnimSequence::StaticClass()))
		{
			RenderAnimationAssetProperty(OwnerObject, Property);
		}
		else if (Property->AcceptsClass(UAnimLuaProgramAsset::StaticClass()))
		{
			RenderAnimLuaProgramAssetProperty(OwnerObject, Property);
		}
	}
	else
	{
		ImGui::TextDisabled("%s: Unsupported object type (%s)", Label, Property->TypeName.c_str());
	}
}

void FEditorPropertyWidget::RenderStructPropertyValue(UObject* OwnerObject, FStructProperty* Property, void* ValueAddress, const char* Label, FProperty* NotifyProperty)
{
	if (!Property || !Property->Struct || !ValueAddress) return;

	if (ImGui::TreeNode(Label))
	{
		TArray<FProperty*> Fields;
		Property->Struct->GetProperties(Fields);

		for (FProperty* Field : Fields)
		{
			if (!Field || !Field->IsVisible()) continue;

			void* FieldAddress = Field->GetValueAddress(ValueAddress);

			ImGui::PushID(Field->Name.c_str());
			RenderPropertyValue(OwnerObject, Field, FieldAddress, Field->DisplayName.c_str(), NotifyProperty);
			ImGui::PopID();
		}

		ImGui::TreePop();
	}
}

void FEditorPropertyWidget::PostEditReflectedProperty(UObject* Object, FProperty* Property)
{
	Object->PostEditChangeProperty({ Property->Name.c_str(), EPropertyChangeType::ValueSet });

	if (EditorEngine)
	{
		EditorEngine->GetSceneService().MarkDirty();

		if (AActor* Actor = Cast<AActor>(Object))
		{
			Actor->SyncActorTransformProperties();
			if (UWorld* World = Actor->GetFocusedWorld())
			{
				World->SyncSpatialIndex();
				if (const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(World))
				{
					Ctx->SelectionManager->GetGizmo()->UpdateGizmoTransform();
				}
			}
		}
		else if (USceneComponent* SceneComp = Cast<USceneComponent>(Object))
		{
			if (AActor* Owner = SceneComp->GetOwner())
			{
				if (SceneComp == Owner->GetRootComponent())
				{
					Owner->SyncActorTransformProperties();
				}

				if (UWorld* World = Owner->GetFocusedWorld())
				{
					World->SyncSpatialIndex();
					if (const FWorldContext* Ctx = EditorEngine->GetWorldContextFromWorld(World))
					{
						Ctx->SelectionManager->GetGizmo()->UpdateGizmoTransform();
					}
				}
			}
		}
	}
}

void FEditorPropertyWidget::BeginPropertyEditState(UObject* Object, const FString& Label)
{
	if (!EditorEngine || !Object || bPropertyEditStateCaptured)
	{
		return;
	}

	PendingPropertyEditObject = Object;
	PendingPropertyEditLabel = Label.empty() ? FString("Edit Property") : Label;
	PendingPropertyEditBeforeState = EditorEngine->GetUndoSystem().CaptureObjectState(Object, PendingPropertyEditLabel);
	bPropertyEditStateCaptured = PendingPropertyEditBeforeState.IsValid();
	bPropertyEditUndoCaptured = bPropertyEditStateCaptured;
}

void FEditorPropertyWidget::CommitPropertyEditState(UObject* Object, const FString& Label)
{
	if (!EditorEngine || !bPropertyEditStateCaptured || PendingPropertyEditObject != Object)
	{
		return;
	}

	const FString FinalLabel = Label.empty() ? PendingPropertyEditLabel : Label;
	const FEditorObjectState AfterState = EditorEngine->GetUndoSystem().CaptureObjectState(Object, FinalLabel);
	EditorEngine->GetUndoSystem().RecordObjectState(PendingPropertyEditBeforeState, AfterState, FinalLabel);

	PendingPropertyEditObject = nullptr;
	PendingPropertyEditLabel.clear();
	PendingPropertyEditBeforeState = FEditorObjectState();
	bPropertyEditStateCaptured = false;
	bPropertyEditUndoCaptured = false;
}

void FEditorPropertyWidget::RecordImmediateObjectStateEdit(
	UObject* Object,
	const FEditorObjectState& BeforeState,
	const FString& Label)
{
	if (!EditorEngine || !Object || !BeforeState.IsValid())
	{
		return;
	}

	const FEditorObjectState AfterState = EditorEngine->GetUndoSystem().CaptureObjectState(Object, Label);
	EditorEngine->GetUndoSystem().RecordObjectState(BeforeState, AfterState, Label);
}

void FEditorPropertyWidget::RenderArrayProperty(UObject* Object, FArrayProperty* ArrayProp)
{
	if (!Object || !ArrayProp || !ArrayProp->Inner) return;

	ImGui::TextUnformatted(ArrayProp->DisplayName.c_str());
	ImGui::SameLine();

	if (ImGui::SmallButton("+"))
	{
		const FEditorObjectState BeforeState = EditorEngine
			? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Edit Property")
			: FEditorObjectState();
		ArrayProp->AddDefaulted(Object);
		PostEditReflectedProperty(Object, ArrayProp);
		RecordImmediateObjectStateEdit(Object, BeforeState, "Edit Property");
	}

	const int32 Count = ArrayProp->GetNum(Object);

	ImGui::PushID(ArrayProp->Name.c_str());

	for (int32 Index = 0; Index < Count; ++Index)
	{
		void* ElementPtr = ArrayProp->GetElementPtr(Object, Index);
		if (!ElementPtr) continue;

		ImGui::PushID(Index);

		FString Label = "Element " + std::to_string(Index);
		RenderPropertyValue(Object, ArrayProp->Inner, ElementPtr, Label.c_str(), ArrayProp);

		ImGui::SameLine();

		if (ImGui::SmallButton("-"))
		{
			const FEditorObjectState BeforeState = EditorEngine
				? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Edit Property")
				: FEditorObjectState();
			ArrayProp->RemoveAt(Object, Index);
			PostEditReflectedProperty(Object, ArrayProp);
			RecordImmediateObjectStateEdit(Object, BeforeState, "Edit Property");

			ImGui::PopID();
			break;
		}

		ImGui::PopID();
	}

	ImGui::PopID();
}

void FEditorPropertyWidget::RenderStaticMeshAssetProperty(UObject* Object, FObjectProperty* Property)
{
	UStaticMesh* CurrentMesh = Cast<UStaticMesh>(Property->GetValue(Object));

	FString CurrentPath;
	if (CurrentMesh)
	{
		CurrentPath = CurrentMesh->GetAssetPathFileName();
	}

	const char* Preview = CurrentPath.empty() ? "<None>" : CurrentPath.c_str();

	ImGui::TextUnformatted(Property->DisplayName.c_str());
	ImGui::SameLine(160.0f);

	const TArray<FString>& MeshPaths = EditorEngine
		? EditorEngine->GetAssetService().GetStaticMeshAssetPaths()
		: EmptyAssetNames();

	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##StaticMeshAssetPicker", Preview))
	{
		const bool bNoneSelected = CurrentMesh == nullptr;
		if (ImGui::Selectable("<None>", bNoneSelected))
		{
			const FEditorObjectState BeforeState = EditorEngine
				? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Edit Property")
				: FEditorObjectState();
			Property->SetValue(Object, nullptr);
			PostEditReflectedProperty(Object, Property);
			RecordImmediateObjectStateEdit(Object, BeforeState, "Edit Property");
		}

		if (bNoneSelected)
		{
			ImGui::SetItemDefaultFocus();
		}

		for (const FString& Path : MeshPaths)
		{
			const bool bSelected = (CurrentPath == Path);
			if (ImGui::Selectable(Path.c_str(), bSelected))
			{
				UStaticMesh* NewMesh = EditorEngine
					? EditorEngine->GetAssetService().LoadStaticMesh(Path)
					: nullptr;

				const FEditorObjectState BeforeState = EditorEngine
					? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Edit Property")
					: FEditorObjectState();
				Property->SetValue(Object, NewMesh);
				PostEditReflectedProperty(Object, Property);
				RecordImmediateObjectStateEdit(Object, BeforeState, "Edit Property");
			}

			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}
}

void FEditorPropertyWidget::RenderSkeletalMeshAssetProperty(UObject* Object, FObjectProperty* Property)
{
	USkeletalMesh* CurrentMesh = Cast<USkeletalMesh>(Property->GetValue(Object));

	FString CurrentPath;
	if (CurrentMesh)
	{
		CurrentPath = CurrentMesh->GetAssetPathFileName();
	}

	const char* Preview = CurrentPath.empty() ? "<None>" : CurrentPath.c_str();

	ImGui::TextUnformatted(Property->DisplayName.c_str());
	ImGui::SameLine(160.0f);

	const TArray<FString>& MeshPaths = EditorEngine
		? EditorEngine->GetAssetService().GetSkeletalMeshAssetPaths()
		: EmptyAssetNames();

	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##SkeletalMeshAssetPicker", Preview))
	{
		const bool bNoneSelected = CurrentMesh == nullptr;
		if (ImGui::Selectable("<None>", bNoneSelected))
		{
			const FEditorObjectState BeforeState = EditorEngine
				? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Edit Property")
				: FEditorObjectState();
			Property->SetValue(Object, nullptr);
			PostEditReflectedProperty(Object, Property);
			RecordImmediateObjectStateEdit(Object, BeforeState, "Edit Property");
		}

		if (bNoneSelected)
		{
			ImGui::SetItemDefaultFocus();
		}

		for (const FString& Path : MeshPaths)
		{
			const bool bSelected = (CurrentPath == Path);
			if (ImGui::Selectable(Path.c_str(), bSelected))
			{
				USkeletalMesh* NewMesh = EditorEngine
					? EditorEngine->GetAssetService().LoadSkeletalMesh(Path)
					: nullptr;

				const FEditorObjectState BeforeState = EditorEngine
					? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Edit Property")
					: FEditorObjectState();
				Property->SetValue(Object, NewMesh);
				PostEditReflectedProperty(Object, Property);
				RecordImmediateObjectStateEdit(Object, BeforeState, "Edit Property");
			}

			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}
}

void FEditorPropertyWidget::RenderAnimationAssetProperty(UObject* Object, FObjectProperty* Property)
{
	USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(Object);
	if (!SkeletalComp)
	{
		ImGui::TextUnformatted(Property->DisplayName.c_str());
		ImGui::SameLine(160.0f);
		ImGui::TextDisabled("Animation requires SkeletalMeshComponent");
		return;
	}

	USkeletalMesh* TargetMesh = SkeletalComp->GetSkeletalMesh();
	if (!TargetMesh)
	{
		ImGui::TextUnformatted(Property->DisplayName.c_str());
		ImGui::SameLine(160.0f);
		ImGui::TextDisabled("Select Skeletal Mesh first");
		return;
	}

	UAnimSequence* CurrentAnim = Cast<UAnimSequence>(Property->GetValue(Object));

	FString CurrentPath;
	if (CurrentAnim)
	{
		CurrentPath = CurrentAnim->GetAssetPathFileName();
	}

	const char* Preview = CurrentPath.empty() ? "<None>" : CurrentPath.c_str();

	ImGui::TextUnformatted(Property->DisplayName.c_str());
	ImGui::SameLine(160.0f);

	const TArray<FString>& AnimPaths = EditorEngine
		? EditorEngine->GetAssetService().GetAnimSequenceAssetPaths()
		: EmptyAssetNames();

	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##AnimationAssetPicker", Preview))
	{
		const bool bNoneSelected = CurrentAnim == nullptr;
		if (ImGui::Selectable("<None>", bNoneSelected))
		{
			const FEditorObjectState BeforeState = EditorEngine
				? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Edit Property")
				: FEditorObjectState();
			Property->SetValue(Object, nullptr);
			PostEditReflectedProperty(Object, Property);
			RecordImmediateObjectStateEdit(Object, BeforeState, "Edit Property");
		}

		if (bNoneSelected)
		{
			ImGui::SetItemDefaultFocus();
		}

		for (const FString& Path : AnimPaths)
		{
			const bool bSelected = (CurrentPath == Path);
			if (ImGui::Selectable(Path.c_str(), bSelected))
			{
				UAnimSequence* NewAnim = EditorEngine
					? EditorEngine->GetAssetService().LoadAnimSequence(Path, TargetMesh)
					: nullptr;

				const FEditorObjectState BeforeState = EditorEngine
					? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Edit Property")
					: FEditorObjectState();
				Property->SetValue(Object, NewAnim);
				PostEditReflectedProperty(Object, Property);
				RecordImmediateObjectStateEdit(Object, BeforeState, "Edit Property");
			}

			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}
}

void FEditorPropertyWidget::RenderAnimLuaProgramAssetProperty(UObject* Object, FObjectProperty* Property)
{
	UAnimLuaProgramAsset* CurrentAsset = Cast<UAnimLuaProgramAsset>(Property->GetValue(Object));

	FString CurrentPath;
	if (CurrentAsset)
	{
		CurrentPath = CurrentAsset->GetAssetPathFileName();
	}
	else if (USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(Object))
	{
		CurrentPath = SkeletalComp->GetLuaAnimProgramAssetPath();
	}

	const char* Preview = CurrentPath.empty() ? "<None>" : CurrentPath.c_str();

	ImGui::TextUnformatted(Property->DisplayName.c_str());
	ImGui::SameLine(160.0f);

	const TArray<FString>& AssetPaths = EditorEngine
		? EditorEngine->GetAssetService().GetAnimLuaProgramAssetPaths()
		: EmptyAssetNames();

	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##AnimLuaProgramAssetPicker", Preview))
	{
		const bool bNoneSelected = CurrentPath.empty();
		if (ImGui::Selectable("<None>", bNoneSelected))
		{
			const FEditorObjectState BeforeState = EditorEngine
				? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Edit Property")
				: FEditorObjectState();
			Property->SetValue(Object, nullptr);
			PostEditReflectedProperty(Object, Property);
			RecordImmediateObjectStateEdit(Object, BeforeState, "Edit Property");
		}

		if (bNoneSelected)
		{
			ImGui::SetItemDefaultFocus();
		}

		for (const FString& Path : AssetPaths)
		{
			const bool bSelected = (CurrentPath == Path);
			if (ImGui::Selectable(Path.c_str(), bSelected))
			{
				UAnimLuaProgramAsset* NewAsset = EditorEngine
					? EditorEngine->GetAssetService().LoadAnimLuaProgramAsset(Path)
					: nullptr;

				const FEditorObjectState BeforeState = EditorEngine
					? EditorEngine->GetUndoSystem().CaptureObjectState(Object, "Edit Property")
					: FEditorObjectState();
				Property->SetValue(Object, NewAsset);
				PostEditReflectedProperty(Object, Property);
				RecordImmediateObjectStateEdit(Object, BeforeState, "Edit Property");
			}

			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}
}

void FEditorPropertyWidget::RenderSkeletalAnimationStateMachineProperty(USkeletalMeshComponent* SkeletalComp)
{
	if (!SkeletalComp)
	{
		return;
	}

	FString CurrentPath = SkeletalComp->GetAnimationStateMachineAssetPath();
	const char* Preview = CurrentPath.empty() ? "<None>" : CurrentPath.c_str();

	ImGui::TextUnformatted("Animation State Machine");
	ImGui::SameLine(160.0f);

	const TArray<FString>& StateMachinePaths = EditorEngine
		? FResourceManager::Get().GetAnimationStateMachinePaths()
		: EmptyAssetNames();

	auto AssignStateMachine = [&](const FString& Path)
	{
		const FEditorObjectState BeforeState = EditorEngine
			? EditorEngine->GetUndoSystem().CaptureObjectState(SkeletalComp, "Edit Property")
			: FEditorObjectState();

		SkeletalComp->SetAnimationStateMachineAssetPath(Path);
		SkeletalComp->SetAnimationMode(EAnimationMode::AnimationStateMachine);

		if (EditorEngine)
		{
			EditorEngine->GetSceneService().MarkDirty();
		}

		RecordImmediateObjectStateEdit(SkeletalComp, BeforeState, "Edit Property");
	};

	ImGui::SetNextItemWidth(-FLT_MIN);
	const bool bComboOpen = ImGui::BeginCombo("##AnimationStateMachineAssetPicker", Preview);
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("AnimationStateMachineContentItem"))
		{
			const char* PayloadPath = static_cast<const char*>(Payload->Data);
			if (PayloadPath && PayloadPath[0] != '\0')
			{
				AssignStateMachine(PayloadPath);
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (bComboOpen)
	{
		const bool bNoneSelected = CurrentPath.empty();
		if (ImGui::Selectable("<None>", bNoneSelected))
		{
			AssignStateMachine(FString());
		}

		if (bNoneSelected)
		{
			ImGui::SetItemDefaultFocus();
		}

		for (const FString& Path : StateMachinePaths)
		{
			const bool bSelected = (CurrentPath == Path);
			if (ImGui::Selectable(Path.c_str(), bSelected))
			{
				AssignStateMachine(Path);
			}

			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}

	UAnimInstance* AnimInstance = SkeletalComp->GetAnimInstance();
	ImGui::TextDisabled(
		"AnimInstance: %s",
		AnimInstance ? AnimInstance->GetClass()->ClassName.c_str() : "<None>");

	if (UAnimStateMachineInstance* StateMachineInstance = Cast<UAnimStateMachineInstance>(AnimInstance))
	{
		ImGui::TextDisabled(
			"StateMachine Runtime: Current=%s Players=%d",
			StateMachineInstance->GetCurrentState().ToString().c_str(),
			StateMachineInstance->GetLoadedPlayerCount());

		if (StateMachineInstance->IsInTransition())
		{
			ImGui::TextDisabled(
				"Transition: %s -> %s",
				StateMachineInstance->GetTransitionFromState().ToString().c_str(),
				StateMachineInstance->GetTransitionToState().ToString().c_str());
		}
	}
}

void FEditorPropertyWidget::RenderMaterialAssetProperty(UObject* OwnerObject, FObjectProperty* Property, void* ValueAddress, const char* Label, FProperty* NotifyProperty)
{
	if (!EditorEngine || !Property || !ValueAddress)
	{
		return;
	}

	UMaterialInterface* CurrentMaterial =
		Cast<UMaterialInterface>(Property->GetValueFromAddress(ValueAddress));

	FEditorAssetService& AssetService = EditorEngine->GetAssetService();
	const TArray<FString>& MaterialNames = AssetService.GetMaterialInterfaceNames();

	const FString CurrentMaterialIdentifier = CurrentMaterial
		? (CurrentMaterial->GetFilePath().empty()
			? CurrentMaterial->GetName()
			: FPaths::Normalize(CurrentMaterial->GetFilePath()))
		: FString();

	const FString CurrentLabel = CurrentMaterial
		? (CurrentMaterial->GetFilePath().empty()
			? CurrentMaterial->GetName()
			: FPaths::Normalize(CurrentMaterial->GetFilePath()))
		: FString("None");

	ImGui::TextUnformatted(Label);
	ImGui::SameLine(160.0f);

	const float EditButtonWidth = 48.0f;
	ImGui::SetNextItemWidth(std::max(
		120.0f,
		ImGui::GetContentRegionAvail().x - EditButtonWidth - ImGui::GetStyle().ItemSpacing.x));

	if (ImGui::BeginCombo("##MaterialAssetPicker", CurrentLabel.c_str()))
	{
		const bool bNoneSelected = CurrentMaterial == nullptr;

		if (ImGui::Selectable("None", bNoneSelected))
		{
			const FEditorObjectState BeforeState =
				EditorEngine->GetUndoSystem().CaptureObjectState(OwnerObject, "Edit Material");
			Property->SetValueAtAddress(ValueAddress, nullptr);
			PostEditReflectedProperty(OwnerObject, NotifyProperty);
			RecordImmediateObjectStateEdit(OwnerObject, BeforeState, "Edit Material");
		}

		if (bNoneSelected)
		{
			ImGui::SetItemDefaultFocus();
		}

		for (int32 MaterialIndex = 0; MaterialIndex < static_cast<int32>(MaterialNames.size()); ++MaterialIndex)
		{
			ImGui::PushID(MaterialIndex);

			const FString& MaterialLabel = MaterialNames[MaterialIndex].empty()
				? FString("<Unnamed Material>")
				: MaterialNames[MaterialIndex];

			const bool bSelected = (CurrentMaterialIdentifier == MaterialLabel);

			if (ImGui::Selectable(MaterialLabel.c_str(), bSelected))
			{
				if (UMaterialInterface* Candidate = AssetService.ResolveMaterialInterfaceByIndex(MaterialIndex))
				{
					const FEditorObjectState BeforeState =
						EditorEngine->GetUndoSystem().CaptureObjectState(OwnerObject, "Edit Material");
					Property->SetValueAtAddress(ValueAddress, Candidate);
					PostEditReflectedProperty(OwnerObject, NotifyProperty);
					RecordImmediateObjectStateEdit(OwnerObject, BeforeState, "Edit Material");
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

	ImGui::SameLine();

	if (ImGui::Button("Edit"))
	{
		if (UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(OwnerObject))
		{
			int32 SlotIndex = -1;

			if (FArrayProperty* ArrayProp = dynamic_cast<FArrayProperty*>(NotifyProperty))
			{
				const int32 Count = ArrayProp->GetNum(OwnerObject);
				for (int32 Index = 0; Index < Count; ++Index)
				{
					if (ArrayProp->GetElementPtr(OwnerObject, Index) == ValueAddress)
					{
						SlotIndex = Index;
						break;
					}
				}
			}

			if (SlotIndex >= 0)
			{
				EditorEngine->GetMainPanel().OpenMaterialSlot(PrimitiveComp, SlotIndex);
			}
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

	auto DrawVec3 = [](const char* Label, FVector& Value, float Speed) -> bool
	{
		float Values[3] = { Value.X, Value.Y, Value.Z };
		const bool bEdited = ImGui::DragFloat3(Label, Values, Speed);

		if (bEdited)
		{
			Value = FVector(Values[0], Values[1], Values[2]);
		}

		return bEdited;
	};

	const bool bTranslationEdited = DrawVec3("Location Offset", EditState.LocationOffset, 0.1f);
	const bool bRotationEdited = DrawVec3("Rotation Offset", EditState.RotationOffset, 0.1f);
	const bool bScaleEdited = DrawVec3("Scale Offset", EditState.ScaleOffset, 0.01f);

	if (bTranslationEdited || bRotationEdited || bScaleEdited)
	{
		FEditorSkeletalBonePoseState BeforePose;
		if (EditorEngine)
		{
			BeforePose = EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp, SelectedBoneIndex);
		}

		const FTransform OffsetTransform(
			FQuat::MakeFromEuler(EditState.RotationOffset),
			EditState.LocationOffset,
			EditState.ScaleOffset);
		const FMatrix NewLocalTransform =
			OffsetTransform.ToMatrixWithScale() * Mesh->GetLocalBindTransform(SelectedBoneIndex);
		Comp->SetBoneLocalTransform(SelectedBoneIndex, NewLocalTransform);

		if (EditorEngine)
		{
			const FEditorSkeletalBonePoseState AfterPose =
				EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp, SelectedBoneIndex);
			EditorEngine->GetUndoSystem().RecordSkeletalBonePose(BeforePose, AfterPose, "Edit Bone Pose");
		}
	}

	const float HalfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("Reset Bone", ImVec2(HalfWidth, 0.0f)))
	{
		FEditorSkeletalBonePoseState BeforePose;
		if (EditorEngine)
		{
			BeforePose = EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp, SelectedBoneIndex);
		}
		ResetEditStateToIdentityOffset(EditState, MeshId, SelectedBoneIndex);
		Comp->SetBoneLocalTransform(SelectedBoneIndex, Mesh->GetLocalBindTransform(SelectedBoneIndex));
		if (EditorEngine)
		{
			const FEditorSkeletalBonePoseState AfterPose =
				EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp, SelectedBoneIndex);
			EditorEngine->GetUndoSystem().RecordSkeletalBonePose(BeforePose, AfterPose, "Reset Bone Pose");
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Pose", ImVec2(-1.0f, 0.0f)))
	{
		FEditorSkeletalBonePoseState BeforePose;
		if (EditorEngine)
		{
			BeforePose = EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp);
		}
		Comp->ResetToBindPose();
		BonePoseEditStates.clear();
		if (EditorEngine)
		{
			const FEditorSkeletalBonePoseState AfterPose =
				EditorEngine->GetUndoSystem().CaptureSkeletalBonePose(Comp);
			EditorEngine->GetUndoSystem().RecordSkeletalBonePose(BeforePose, AfterPose, "Reset Bone Pose");
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
		const FName OldName = TargetObject->GetFName();
		const FName NewName(NameBuf);
		if (EditorEngine)
		{
			EditorEngine->GetUndoSystem().RecordObjectRename(TargetObject, OldName, NewName);
		}
		TargetObject->SetFName(NewName);
		if (EditorEngine)
		{
			EditorEngine->GetSceneService().MarkDirty();
		}
	}
}
