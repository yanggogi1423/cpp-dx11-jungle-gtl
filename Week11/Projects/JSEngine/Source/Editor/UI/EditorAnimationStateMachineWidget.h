#pragma once

#include "Animation/StateDatas/AnimTransitionCondition.h"
#include "Animation/StateDatas/StateMachineDefs.h"
#include "Editor/UI/EditorWidget.h"
#include "Math/Vector2.h"
#include "Object/FName.h"
#include "ImGui/imgui.h"

class UAnimationStateMachine;

class FEditorAnimationStateMachineWidget : public FEditorWidget
{
public:
    void Render(float DeltaTime) override;
    void RenderEmbedded(float DeltaTime);

    bool OpenAsset(const FString& AssetPath);
    bool IsVisible() const { return bVisible; }
    bool IsOpen() const { return bVisible; }
    void SetOpen(bool bInOpen) { bVisible = bInOpen; }
    const FString& GetAssetPath() const { return CurrentPath; }
    FString GetWindowName() const;
    bool ConsumeDockRequest();
    void Close();

private:
    enum class ESelectionType
    {
        None,
        State,
        Transition
    };

    struct FConditionUndoSnapshot
    {
        EAnimConditionType Type = EAnimConditionType::Bool;
        FName ParamName;
        EAnimCompareOp CompareOp = EAnimCompareOp::Equal;
        float FloatValue = 0.0f;
        bool BoolValue = true;
        EAnimConditionOp CompositeOp = EAnimConditionOp::And;
        TArray<FConditionUndoSnapshot> Children;
    };

    struct FTransitionUndoSnapshot
    {
        FName TransitionId;
        FName FromState;
        FName ToState;
        float BlendTime = 0.2f;
        bool bResetTime = true;
        TArray<FConditionUndoSnapshot> Conditions;
    };

    struct FStateMachineUndoSnapshot
    {
        FName InitialState;
        TArray<FAnimStateDef> States;
        TArray<FTransitionUndoSnapshot> Transitions;
        ESelectionType SelectionType = ESelectionType::None;
        FName SelectedState;
        FName SelectedTransitionId;
    };

    void DrawToolbar(bool bDetachedWindow);
    void DrawContent(float DeltaTime, bool bDetachedWindow = false);
    void HandleShortcuts();
    void DrawGraphCanvas();
    void DrawDetailsPanel();
    void DrawTransitionConditions(FAnimTransitionDef& Transition);
    bool DrawConditionEditor(UAnimTransitionCondition*& Condition, int32 Depth);

    void AddStateAt(const FVector2& GraphPosition);
    void DeleteSelection();
    void BeginConnectFromSelectedState();
    void TryCompleteConnectToState(const FName& TargetState);
    bool SaveAsset();
    void MarkDirty();
    FConditionUndoSnapshot MakeConditionUndoSnapshot(const UAnimTransitionCondition* Condition) const;
    UAnimTransitionCondition* CreateConditionFromUndoSnapshot(const FConditionUndoSnapshot& Snapshot) const;
    FStateMachineUndoSnapshot MakeUndoSnapshot() const;
    FString ComputeUndoFingerprint(const FStateMachineUndoSnapshot& Snapshot) const;
    void RestoreUndoSnapshot(const FStateMachineUndoSnapshot& Snapshot);
    void ResetUndoHistory();
    void CommitUndoSnapshot(bool bForce = false);
    bool CanUndoGraphEdit() const;
    bool CanRedoGraphEdit() const;
    void UndoGraphEdit();
    void RedoGraphEdit();

    FAnimStateDef* FindMutableState(FName Name);
    FAnimTransitionDef* FindMutableTransition(FName TransitionId);
    int32 FindStateIndex(FName Name) const;
    int32 FindTransitionIndex(FName TransitionId) const;
    FName MakeUniqueStateName() const;
    FName MakeUniqueTransitionId() const;

    ImVec2 GraphToScreen(const FVector2& GraphPosition, const ImVec2& CanvasOrigin) const;
    FVector2 ScreenToGraph(const ImVec2& ScreenPosition, const ImVec2& CanvasOrigin) const;

private:
    FString CurrentPath;
    UAnimationStateMachine* StateMachine = nullptr;
    ESelectionType SelectionType = ESelectionType::None;
    FName SelectedState;
    FName SelectedTransitionId;
    FName PendingConnectFromState;
    FName DraggingState;
    ImVec2 CanvasPan = ImVec2(40.0f, 40.0f);
    float CanvasZoom = 1.0f;
    float NodeWidth = 150.0f;
    float NodeHeight = 56.0f;
    bool bVisible = false;
    bool bDockRequested = false;
    bool bDirty = false;
    bool bRestoringUndoSnapshot = false;

    TArray<FStateMachineUndoSnapshot> UndoStack;
    TArray<FStateMachineUndoSnapshot> RedoStack;
    FStateMachineUndoSnapshot LastUndoSnapshot;
    FString LastUndoFingerprint;
    bool bUndoBaselineValid = false;
};
