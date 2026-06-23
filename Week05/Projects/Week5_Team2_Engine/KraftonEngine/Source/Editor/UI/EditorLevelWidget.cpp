#include "Editor/UI/EditorLevelWidget.h"

#include "Editor/EditorEngine.h"
#include "Engine/Core/Common.h"

#include "ImGui/imgui.h"

#define SEPARATOR(); ImGui::Spacing(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); ImGui::Spacing();

void FEditorLevelWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
}

void FEditorLevelWidget::Render(float DeltaTime)
{
	using namespace common::constants::ImGui;

	if (!EditorEngine)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(400.0f, 350.0f), ImGuiCond_Once);

	ImGui::Begin("Outliner");

	// New Level
	// if (ImGui::Button("New Level"))
	// {
	// 	EditorEngine->NewLevel();
	// 	NewLevelNotificationTimer = NotificationTimer;
	// }
	// if (NewLevelNotificationTimer > 0.0f)
	// {
	// 	NewLevelNotificationTimer -= DeltaTime;
	// 	ImGui::SameLine();
	// 	ImGui::Text("New Level created");
	// }

	// SEPARATOR();

	// Actor Outliner
	UWorld* World = EditorEngine->GetWorld();
	if (World)
	{
		const TArray<AActor*>& Actors = World->GetActors();
		ImGui::Text("Actors (%d)", static_cast<int32>(Actors.size()));
		ImGui::Separator();

		FSelectionManager& Selection = EditorEngine->GetSelectionManager();
		ImGui::BeginChild("ActorList", ImVec2(0, 0), ImGuiChildFlags_Borders);
		for (AActor* Actor : Actors)
		{
			if (!Actor)
			{
				continue;
			}

			FString ActorName = Actor->GetFName();
			if (ActorName.empty())
			{
				ActorName = Actor->GetTypeInfo()->name;
			}

			const bool bIsSelected = Selection.IsSelected(Actor);
			if (ImGui::Selectable(ActorName.c_str(), bIsSelected))
			{
				if (ImGui::GetIO().KeyShift)
				{
					Selection.SelectRange(Actor, Actors);
				}
				else if (ImGui::GetIO().KeyCtrl)
				{
					Selection.ToggleSelect(Actor);
				}
				else
				{
					Selection.Select(Actor);
				}
			}
		}
		ImGui::EndChild();
	}
	ImGui::End();
}
