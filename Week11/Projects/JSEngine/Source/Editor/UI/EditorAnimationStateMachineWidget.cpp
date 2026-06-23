#include "Editor/UI/EditorAnimationStateMachineWidget.h"

#include "Animation/AnimationStateMachine.h"
#include "Animation/StateDatas/StateMachineDefs.h"
#include "Animation/StateDatas/AnimTransitionCondition.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Editor/EditorEngine.h"
#include "Editor/UI/EditorDetachedDocumentChrome.h"
#include "Editor/UI/EditorMainPanel.h"
#include "Object/ObjectFactory.h"
#include "ImGui/imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace
{
FString ToString(const FName& Name)
{
    return Name.ToString();
}

FString GetFileNameFromPath(const FString& Path)
{
    const size_t SlashIndex = Path.find_last_of("/\\");
    return SlashIndex == FString::npos ? Path : Path.substr(SlashIndex + 1);
}

void CopyToBuffer(char* Buffer, size_t BufferSize, const FString& Value)
{
    if (!Buffer || BufferSize == 0)
    {
        return;
    }

    std::snprintf(Buffer, BufferSize, "%s", Value.c_str());
}

const char* ConditionTypeName(EAnimConditionType Type)
{
    switch (Type)
    {
    case EAnimConditionType::Bool: return "Bool";
    case EAnimConditionType::FloatCompare: return "Float Compare";
    case EAnimConditionType::StateTime: return "State Time";
    case EAnimConditionType::Composite: return "Composite";
    default: return "Unknown";
    }
}

const char* CompareOpName(EAnimCompareOp Op)
{
    switch (Op)
    {
    case EAnimCompareOp::Equal: return "==";
    case EAnimCompareOp::NotEqual: return "!=";
    case EAnimCompareOp::Less: return "<";
    case EAnimCompareOp::LessEqual: return "<=";
    case EAnimCompareOp::Greater: return ">";
    case EAnimCompareOp::GreaterEqual: return ">=";
    default: return "?";
    }
}

const char* CompositeOpName(EAnimConditionOp Op)
{
    switch (Op)
    {
    case EAnimConditionOp::And: return "AND";
    case EAnimConditionOp::Or: return "OR";
    case EAnimConditionOp::Not: return "NOT";
    default: return "?";
    }
}

ImVec2 AddVec2(const ImVec2& A, const ImVec2& B)
{
    return ImVec2(A.x + B.x, A.y + B.y);
}

ImVec2 MulVec2(const ImVec2& V, float Scale)
{
    return ImVec2(V.x * Scale, V.y * Scale);
}

ImVec2 BezierPoint(const ImVec2& P0, const ImVec2& P1, const ImVec2& P2, const ImVec2& P3, float T)
{
    const float U = 1.0f - T;
    const float UU = U * U;
    const float UUU = UU * U;
    const float TT = T * T;
    const float TTT = TT * T;

    return ImVec2(
        UUU * P0.x + 3.0f * UU * T * P1.x + 3.0f * U * TT * P2.x + TTT * P3.x,
        UUU * P0.y + 3.0f * UU * T * P1.y + 3.0f * U * TT * P2.y + TTT * P3.y);
}

float DistancePointToSegmentSq(const ImVec2& Point, const ImVec2& A, const ImVec2& B)
{
    const float ABx = B.x - A.x;
    const float ABy = B.y - A.y;
    const float APx = Point.x - A.x;
    const float APy = Point.y - A.y;
    const float LenSq = ABx * ABx + ABy * ABy;
    if (LenSq <= 1e-4f)
    {
        return APx * APx + APy * APy;
    }

    const float T = std::clamp((APx * ABx + APy * ABy) / LenSq, 0.0f, 1.0f);
    const float ClosestX = A.x + ABx * T;
    const float ClosestY = A.y + ABy * T;
    const float Dx = Point.x - ClosestX;
    const float Dy = Point.y - ClosestY;
    return Dx * Dx + Dy * Dy;
}

float DistancePointToBezierSq(
    const ImVec2& Point,
    const ImVec2& P0,
    const ImVec2& P1,
    const ImVec2& P2,
    const ImVec2& P3)
{
    constexpr int32 SegmentCount = 24;

    float BestSq = FLT_MAX;
    ImVec2 Prev = P0;
    for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
    {
        const float T = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
        const ImVec2 Cur = BezierPoint(P0, P1, P2, P3, T);
        BestSq = std::min(BestSq, DistancePointToSegmentSq(Point, Prev, Cur));
        Prev = Cur;
    }
    return BestSq;
}

bool HasReverseTransition(const UAnimationStateMachine* StateMachine, const FAnimTransitionDef& Transition)
{
    if (!StateMachine)
    {
        return false;
    }

    for (const FAnimTransitionDef& Other : StateMachine->Transitions)
    {
        if (Other.TransitionId != Transition.TransitionId &&
            Other.FromState == Transition.ToState &&
            Other.ToState == Transition.FromState)
        {
            return true;
        }
    }
    return false;
}

int32 GetSameDirectionTransitionIndex(const UAnimationStateMachine* StateMachine, const FAnimTransitionDef& Transition)
{
    if (!StateMachine)
    {
        return 0;
    }

    int32 SameDirectionIndex = 0;
    for (const FAnimTransitionDef& Other : StateMachine->Transitions)
    {
        if (Other.TransitionId == Transition.TransitionId)
        {
            return SameDirectionIndex;
        }
        if (Other.FromState == Transition.FromState && Other.ToState == Transition.ToState)
        {
            ++SameDirectionIndex;
        }
    }
    return SameDirectionIndex;
}

void BuildTransitionCurve(
    const UAnimationStateMachine* StateMachine,
    const FAnimTransitionDef& Transition,
    const FAnimStateDef& FromState,
    const FAnimStateDef& ToState,
    const ImVec2& CanvasMin,
    const ImVec2& CanvasPan,
    float CanvasZoom,
    float NodeWidth,
    float NodeHeight,
    ImVec2& OutStart,
    ImVec2& OutControl0,
    ImVec2& OutControl1,
    ImVec2& OutEnd)
{
    const ImVec2 From(
        CanvasMin.x + CanvasPan.x + FromState.GraphPosition.X * CanvasZoom,
        CanvasMin.y + CanvasPan.y + FromState.GraphPosition.Y * CanvasZoom);
    const ImVec2 To(
        CanvasMin.x + CanvasPan.x + ToState.GraphPosition.X * CanvasZoom,
        CanvasMin.y + CanvasPan.y + ToState.GraphPosition.Y * CanvasZoom);
    const ImVec2 FromCenter(From.x + NodeWidth * 0.5f, From.y + NodeHeight * 0.5f);
    const ImVec2 ToCenter(To.x + NodeWidth * 0.5f, To.y + NodeHeight * 0.5f);
    const bool bTargetIsRight = ToCenter.x >= FromCenter.x;

    OutStart = ImVec2(bTargetIsRight ? From.x + NodeWidth : From.x, From.y + NodeHeight * 0.5f);
    OutEnd = ImVec2(bTargetIsRight ? To.x : To.x + NodeWidth, To.y + NodeHeight * 0.5f);

    const float Dx = OutEnd.x - OutStart.x;
    const float Dy = OutEnd.y - OutStart.y;
    const float Length = std::sqrt(Dx * Dx + Dy * Dy);
    ImVec2 Normal(0.0f, -1.0f);
    if (Length > 1e-4f)
    {
        Normal = ImVec2(-Dy / Length, Dx / Length);
    }

    float Offset = 0.0f;
    if (HasReverseTransition(StateMachine, Transition))
    {
        Offset = 22.0f * CanvasZoom;
    }

    const int32 SameDirectionIndex = GetSameDirectionTransitionIndex(StateMachine, Transition);
    if (SameDirectionIndex > 0)
    {
        Offset += static_cast<float>(SameDirectionIndex) * 14.0f * CanvasZoom;
    }

    const ImVec2 OffsetVector = MulVec2(Normal, Offset);
    OutStart = AddVec2(OutStart, OffsetVector);
    OutEnd = AddVec2(OutEnd, OffsetVector);
    OutControl0 = ImVec2(OutStart.x + (bTargetIsRight ? 70.0f : -70.0f) * CanvasZoom, OutStart.y);
    OutControl1 = ImVec2(OutEnd.x + (bTargetIsRight ? -70.0f : 70.0f) * CanvasZoom, OutEnd.y);
}

void AddArrowHead(ImDrawList* DrawList, const ImVec2& Tip, const ImVec2& Previous, ImU32 Color)
{
    float DirX = Tip.x - Previous.x;
    float DirY = Tip.y - Previous.y;
    const float Length = std::sqrt(DirX * DirX + DirY * DirY);
    if (Length <= 1e-4f)
    {
        DirX = 1.0f;
        DirY = 0.0f;
    }
    else
    {
        DirX /= Length;
        DirY /= Length;
    }

    const ImVec2 Normal(-DirY, DirX);
    const float ArrowLength = 10.0f;
    const float ArrowWidth = 6.0f;
    const ImVec2 Base(Tip.x - DirX * ArrowLength, Tip.y - DirY * ArrowLength);

    DrawList->AddTriangleFilled(
        Tip,
        ImVec2(Base.x + Normal.x * ArrowWidth, Base.y + Normal.y * ArrowWidth),
        ImVec2(Base.x - Normal.x * ArrowWidth, Base.y - Normal.y * ArrowWidth),
        Color);
}

void DestroyEditorConditionTree(UAnimTransitionCondition* Condition)
{
    if (!Condition)
    {
        return;
    }

    if (Condition->GetConditionType() == EAnimConditionType::Composite)
    {
        AnimCompositeCondition* Composite = static_cast<AnimCompositeCondition*>(Condition);
        for (UAnimTransitionCondition* Child : Composite->Childrens)
        {
            DestroyEditorConditionTree(Child);
        }
        Composite->Childrens.clear();
    }

    delete Condition;
}
}

bool FEditorAnimationStateMachineWidget::OpenAsset(const FString& AssetPath)
{
    CurrentPath = FPaths::Normalize(AssetPath);
    StateMachine = FResourceManager::Get().LoadAnimationStateMachine(CurrentPath);
    SelectionType = ESelectionType::None;
    SelectedState = FName::None;
    SelectedTransitionId = FName::None;
    PendingConnectFromState = FName::None;
    DraggingState = FName::None;
    bDirty = false;
    bVisible = StateMachine != nullptr;
    ResetUndoHistory();

    if (!StateMachine && EditorEngine)
    {
        EditorEngine->GetNotificationService().Error("Failed to open Animation State Machine asset.");
    }

    return StateMachine != nullptr;
}

void FEditorAnimationStateMachineWidget::Close()
{
    bVisible = false;
}

void FEditorAnimationStateMachineWidget::Render(float DeltaTime)
{
    if (!bVisible)
    {
        return;
    }

    bool bOpen = bVisible;
    bool bCloseRequested = false;

    FEditorDetachedDocumentChrome::PushDetachedWindowStyle();
    FEditorDetachedDocumentChrome::ApplyWindowClass();
    FEditorDetachedDocumentChrome::SetFirstUseWindowPlacement(ImVec2(1040.0f, 680.0f), ImVec2(140.0f, 110.0f));
    constexpr ImGuiWindowFlags WindowFlags =
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin(GetWindowName().c_str(), &bOpen, WindowFlags))
    {
        ImGui::End();
        FEditorDetachedDocumentChrome::PopDetachedWindowStyle();
        bVisible = bOpen;
        return;
    }

    FEditorDetachedDocumentChrome::RenderChrome(
        GetWindowName(),
        [this, &bOpen]()
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save", "Ctrl+S", false, StateMachine != nullptr))
                {
                    SaveAsset();
                }
                if (ImGui::MenuItem("Close"))
                {
                    bOpen = false;
                }
                ImGui::EndMenu();
            }
        },
        bDockRequested,
        bCloseRequested);

    DrawContent(DeltaTime, true);

    ImGui::End();
    FEditorDetachedDocumentChrome::PopDetachedWindowStyle();

    if (bCloseRequested)
    {
        bOpen = false;
    }

    bVisible = bOpen;
}

void FEditorAnimationStateMachineWidget::RenderEmbedded(float DeltaTime)
{
    DrawContent(DeltaTime, false);
}

FString FEditorAnimationStateMachineWidget::GetWindowName() const
{
    FString WindowName = "Animation State Machine";
    if (!CurrentPath.empty())
    {
        WindowName += " - ";
        WindowName += GetFileNameFromPath(CurrentPath);
    }
    if (bDirty)
    {
        WindowName += " *";
    }
    WindowName += "###AnimationStateMachineEditor";
    return WindowName;
}

bool FEditorAnimationStateMachineWidget::ConsumeDockRequest()
{
    const bool bRequested = bDockRequested;
    bDockRequested = false;
    return bRequested;
}

void FEditorAnimationStateMachineWidget::HandleShortcuts()
{
    const ImGuiIO& IO = ImGui::GetIO();
    const bool bInStateMachineContext =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    if (!bInStateMachineContext || !IO.KeyCtrl)
    {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        SaveAsset();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
    {
        if (IO.KeyShift)
        {
            RedoGraphEdit();
        }
        else
        {
            UndoGraphEdit();
        }
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
    {
        RedoGraphEdit();
    }
}

void FEditorAnimationStateMachineWidget::DrawContent(float DeltaTime, bool bDetachedWindow)
{
    (void)DeltaTime;

    HandleShortcuts();

    DrawToolbar(bDetachedWindow);
    ImGui::Separator();

    if (!StateMachine)
    {
        ImGui::TextDisabled("No state machine asset loaded.");
        return;
    }

    const ImVec2 LayoutSize = ImGui::GetContentRegionAvail();
    if (ImGui::BeginTable(
            "##AnimStateMachineEditorLayout",
            2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp,
            LayoutSize))
    {
        const float AvailableWidth = ImGui::GetContentRegionAvail().x;
        const float DetailsWidth = std::clamp(AvailableWidth * 0.30f, 280.0f, 360.0f);
        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, DetailsWidth);

        ImGui::TableNextColumn();
        DrawGraphCanvas();

        ImGui::TableNextColumn();
        ImGui::BeginChild(
            "##AnimStateMachineDetailsPanel",
            ImGui::GetContentRegionAvail(),
            false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);
        DrawDetailsPanel();
        ImGui::EndChild();

        ImGui::EndTable();
    }
}

void FEditorAnimationStateMachineWidget::DrawToolbar(bool bDetachedWindow)
{
    if (bDetachedWindow)
    {
        if (ImGui::Button("Dock"))
        {
            bDockRequested = true;
        }
        ImGui::SameLine(0.0f, 10.0f);
    }

    if (ImGui::Button("Add State"))
    {
        const float Offset = StateMachine ? static_cast<float>(StateMachine->States.size()) * 34.0f : 0.0f;
        AddStateAt(FVector2(80.0f + Offset, 80.0f + Offset));
    }
    ImGui::SameLine();
    if (ImGui::Button("Connect") && SelectionType == ESelectionType::State)
    {
        BeginConnectFromSelectedState();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
    {
        DeleteSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        SaveAsset();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", CurrentPath.c_str());
}

void FEditorAnimationStateMachineWidget::DrawGraphCanvas()
{
    const ImVec2 Available = ImGui::GetContentRegionAvail();
    const ImVec2 CanvasSize(std::max(Available.x, 320.0f), std::max(Available.y, 360.0f));
    ImGui::InvisibleButton(
        "##AnimStateMachineGraphCanvas",
        CanvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const ImVec2 CanvasMin = ImGui::GetItemRectMin();
    const ImVec2 CanvasMax = ImGui::GetItemRectMax();
    const ImVec2 MousePos = ImGui::GetIO().MousePos;
    const bool bCanvasHovered = ImGui::IsItemHovered();
    const bool bCanvasActive = ImGui::IsItemActive();

    if (bCanvasHovered)
    {
        const ImGuiIO& IO = ImGui::GetIO();
        constexpr float WheelPanSpeed = 48.0f;
        if (std::fabs(IO.MouseWheel) > 0.0f)
        {
            if (IO.KeyShift)
            {
                CanvasPan.x += IO.MouseWheel * WheelPanSpeed;
            }
            else
            {
                const float OldZoom = CanvasZoom;
                const float ZoomFactor = std::pow(1.12f, IO.MouseWheel);
                CanvasZoom = std::clamp(CanvasZoom * ZoomFactor, 0.25f, 2.5f);
                if (std::fabs(CanvasZoom - OldZoom) > 1e-4f)
                {
                    const ImVec2 MouseGraphBefore(
                        (MousePos.x - CanvasMin.x - CanvasPan.x) / OldZoom,
                        (MousePos.y - CanvasMin.y - CanvasPan.y) / OldZoom);
                    CanvasPan.x = MousePos.x - CanvasMin.x - MouseGraphBefore.x * CanvasZoom;
                    CanvasPan.y = MousePos.y - CanvasMin.y - MouseGraphBefore.y * CanvasZoom;
                }
            }
        }
        if (std::fabs(IO.MouseWheelH) > 0.0f)
        {
            CanvasPan.x += IO.MouseWheelH * WheelPanSpeed;
        }
    }

    if (bCanvasActive && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        const ImVec2 Delta = ImGui::GetIO().MouseDelta;
        CanvasPan.x += Delta.x;
        CanvasPan.y += Delta.y;
    }

    const float ScaledNodeWidth = NodeWidth * CanvasZoom;
    const float ScaledNodeHeight = NodeHeight * CanvasZoom;

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    DrawList->AddRectFilled(CanvasMin, CanvasMax, ImGui::GetColorU32(ImVec4(0.09f, 0.10f, 0.12f, 1.0f)));
    DrawList->AddRect(CanvasMin, CanvasMax, ImGui::GetColorU32(ImVec4(0.20f, 0.23f, 0.28f, 1.0f)));
    DrawList->PushClipRect(CanvasMin, CanvasMax, true);

    FName HoveredState = FName::None;
    for (int32 StateIndex = static_cast<int32>(StateMachine->States.size()) - 1; StateIndex >= 0; --StateIndex)
    {
        const FAnimStateDef& State = StateMachine->States[StateIndex];
        const ImVec2 NodeMin = GraphToScreen(State.GraphPosition, CanvasMin);
        const ImVec2 NodeMax(NodeMin.x + ScaledNodeWidth, NodeMin.y + ScaledNodeHeight);
        if (ImGui::IsMouseHoveringRect(NodeMin, NodeMax, true))
        {
            HoveredState = State.Name;
            break;
        }
    }

    const float GridStep = std::max(16.0f, 32.0f * CanvasZoom);
    const ImU32 GridColor = ImGui::GetColorU32(ImVec4(0.18f, 0.20f, 0.24f, 0.65f));
    for (float X = CanvasMin.x + std::fmod(CanvasPan.x, GridStep); X < CanvasMax.x; X += GridStep)
    {
        DrawList->AddLine(ImVec2(X, CanvasMin.y), ImVec2(X, CanvasMax.y), GridColor);
    }
    for (float Y = CanvasMin.y + std::fmod(CanvasPan.y, GridStep); Y < CanvasMax.y; Y += GridStep)
    {
        DrawList->AddLine(ImVec2(CanvasMin.x, Y), ImVec2(CanvasMax.x, Y), GridColor);
    }

    const bool bHasHoveredState = HoveredState != FName::None;
    const bool bIsConnecting = PendingConnectFromState != FName::None;
    FName HoveredTransitionId = FName::None;
    float BestTransitionHitSq = FLT_MAX;

    if (bCanvasHovered && !bHasHoveredState && !bIsConnecting)
    {
        constexpr float TransitionHitRadius = 10.0f;
        constexpr float TransitionHitRadiusSq = TransitionHitRadius * TransitionHitRadius;

        for (const FAnimTransitionDef& Transition : StateMachine->Transitions)
        {
            const FAnimStateDef* FromState = StateMachine->FindState(Transition.FromState);
            const FAnimStateDef* ToState = StateMachine->FindState(Transition.ToState);
            if (!FromState || !ToState)
            {
                continue;
            }

            ImVec2 Start;
            ImVec2 Control0;
            ImVec2 Control1;
            ImVec2 End;
            BuildTransitionCurve(
                StateMachine,
                Transition,
                *FromState,
                *ToState,
                CanvasMin,
                CanvasPan,
                CanvasZoom,
                ScaledNodeWidth,
                ScaledNodeHeight,
                Start,
                Control0,
                Control1,
                End);

            const float HitSq = DistancePointToBezierSq(MousePos, Start, Control0, Control1, End);
            if (HitSq <= TransitionHitRadiusSq && HitSq < BestTransitionHitSq)
            {
                BestTransitionHitSq = HitSq;
                HoveredTransitionId = Transition.TransitionId;
            }
        }
    }

    if (bCanvasHovered && bHasHoveredState && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        SelectionType = ESelectionType::State;
        SelectedState = HoveredState;
        SelectedTransitionId = FName::None;
        PendingConnectFromState = FName::None;
        DraggingState = FName::None;
        ImGui::OpenPopup("##AnimStateMachineGraphContext");
    }
    else if (bCanvasHovered && HoveredTransitionId != FName::None && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        SelectionType = ESelectionType::Transition;
        SelectedTransitionId = HoveredTransitionId;
        SelectedState = FName::None;
        PendingConnectFromState = FName::None;
        DraggingState = FName::None;
        ImGui::OpenPopup("##AnimStateMachineGraphContext");
    }

    if (bCanvasHovered && HoveredTransitionId != FName::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        SelectionType = ESelectionType::Transition;
        SelectedTransitionId = HoveredTransitionId;
        SelectedState = FName::None;
        PendingConnectFromState = FName::None;
        DraggingState = FName::None;
    }

    if (bCanvasHovered && !bHasHoveredState && HoveredTransitionId == FName::None && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        const ImVec2 Delta = ImGui::GetIO().MouseDelta;
        CanvasPan.x += Delta.x;
        CanvasPan.y += Delta.y;
    }

    if (bCanvasHovered && !bHasHoveredState && HoveredTransitionId == FName::None && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        AddStateAt(ScreenToGraph(MousePos, CanvasMin));
    }

    if (bCanvasHovered && bHasHoveredState && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (bIsConnecting)
        {
            TryCompleteConnectToState(HoveredState);
        }
        else
        {
            SelectionType = ESelectionType::State;
            SelectedState = HoveredState;
            SelectedTransitionId = FName::None;
            DraggingState = HoveredState;
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        DraggingState = FName::None;
    }

    for (const FAnimTransitionDef& Transition : StateMachine->Transitions)
    {
        const FAnimStateDef* FromState = StateMachine->FindState(Transition.FromState);
        const FAnimStateDef* ToState = StateMachine->FindState(Transition.ToState);
        if (!FromState || !ToState)
        {
            continue;
        }

        ImVec2 Start;
        ImVec2 Control0;
        ImVec2 Control1;
        ImVec2 End;
        BuildTransitionCurve(
            StateMachine,
            Transition,
            *FromState,
            *ToState,
            CanvasMin,
            CanvasPan,
            CanvasZoom,
            ScaledNodeWidth,
            ScaledNodeHeight,
            Start,
            Control0,
            Control1,
            End);
        const ImU32 Color = ImGui::GetColorU32(
            SelectionType == ESelectionType::Transition && SelectedTransitionId == Transition.TransitionId
                ? ImVec4(0.98f, 0.78f, 0.28f, 1.0f)
                : ImVec4(0.50f, 0.64f, 0.84f, 1.0f));

        DrawList->AddBezierCubic(
            Start,
            Control0,
            Control1,
            End,
            Color,
            2.0f);
        AddArrowHead(DrawList, End, BezierPoint(Start, Control0, Control1, End, 0.94f), Color);
    }

    if (bIsConnecting)
    {
        const FAnimStateDef* FromState = StateMachine->FindState(PendingConnectFromState);
        if (FromState)
        {
            const ImVec2 From = GraphToScreen(FromState->GraphPosition, CanvasMin);
            DrawList->AddLine(
                ImVec2(From.x + ScaledNodeWidth, From.y + ScaledNodeHeight * 0.5f),
                MousePos,
                ImGui::GetColorU32(ImVec4(0.98f, 0.78f, 0.28f, 1.0f)),
                2.0f);
        }
    }

    for (FAnimStateDef& State : StateMachine->States)
    {
        const bool bSelected = SelectionType == ESelectionType::State && SelectedState == State.Name;
        const ImVec2 NodeMin = GraphToScreen(State.GraphPosition, CanvasMin);
        const ImVec2 NodeMax(NodeMin.x + ScaledNodeWidth, NodeMin.y + ScaledNodeHeight);
        const ImU32 NodeColor = ImGui::GetColorU32(
            bSelected ? ImVec4(0.18f, 0.30f, 0.46f, 1.0f) : ImVec4(0.15f, 0.17f, 0.21f, 1.0f));
        const ImU32 BorderColor = ImGui::GetColorU32(
            State.Name == StateMachine->InitialState
                ? ImVec4(0.44f, 0.78f, 0.46f, 1.0f)
                : (bSelected ? ImVec4(0.44f, 0.66f, 0.96f, 1.0f) : ImVec4(0.28f, 0.32f, 0.39f, 1.0f)));

        DrawList->AddRectFilled(NodeMin, NodeMax, NodeColor, 6.0f);
        DrawList->AddRect(NodeMin, NodeMax, BorderColor, 6.0f, 0, 2.0f);
        const float TextScale = std::clamp(CanvasZoom, 0.65f, 1.0f);
        const float FontSize = ImGui::GetFontSize() * TextScale;
        DrawList->AddText(
            nullptr,
            FontSize,
            ImVec2(NodeMin.x + 10.0f * CanvasZoom, NodeMin.y + 10.0f * CanvasZoom),
            ImGui::GetColorU32(ImGuiCol_Text),
            State.Name.ToString().c_str());
        if (CanvasZoom >= 0.55f)
        {
            DrawList->AddText(
                nullptr,
                FontSize,
                ImVec2(NodeMin.x + 10.0f * CanvasZoom, NodeMin.y + 30.0f * CanvasZoom),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                State.AnimationPath.empty() ? "No animation" : State.AnimationPath.c_str());
        }

        if (DraggingState == State.Name && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !bIsConnecting)
        {
            const ImVec2 Delta = ImGui::GetIO().MouseDelta;
            if (Delta.x != 0.0f || Delta.y != 0.0f)
            {
                State.GraphPosition.X += Delta.x / CanvasZoom;
                State.GraphPosition.Y += Delta.y / CanvasZoom;
                MarkDirty();
            }
        }
    }

    DrawList->PopClipRect();

    if (ImGui::BeginPopup("##AnimStateMachineGraphContext"))
    {
        if (SelectionType == ESelectionType::State)
        {
            ImGui::TextDisabled("%s", SelectedState.ToString().c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Node"))
            {
                DeleteSelection();
            }
        }
        else if (SelectionType == ESelectionType::Transition)
        {
            ImGui::TextDisabled("%s", SelectedTransitionId.ToString().c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Connection"))
            {
                DeleteSelection();
            }
        }
        ImGui::EndPopup();
    }
}

void FEditorAnimationStateMachineWidget::DrawDetailsPanel()
{
    ImGui::TextUnformatted("Details");
    ImGui::Separator();

    if (!StateMachine)
    {
        ImGui::TextDisabled("No asset.");
        return;
    }

    ImGui::Text("States: %d", static_cast<int32>(StateMachine->States.size()));
    ImGui::Text("Transitions: %d", static_cast<int32>(StateMachine->Transitions.size()));

    ImGui::Spacing();
    if (SelectionType == ESelectionType::State)
    {
        FAnimStateDef* State = FindMutableState(SelectedState);
        if (!State)
        {
            ImGui::TextDisabled("State not found.");
            return;
        }

        char NameBuffer[128];
        CopyToBuffer(NameBuffer, sizeof(NameBuffer), State->Name.ToString());
        if (ImGui::InputText("Name", NameBuffer, sizeof(NameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const FName NewName(NameBuffer);
            if (NewName.IsValid() && !StateMachine->FindState(NewName))
            {
                const FName OldName = State->Name;
                State->Name = NewName;
                for (FAnimTransitionDef& Transition : StateMachine->Transitions)
                {
                    if (Transition.FromState == OldName)
                    {
                        Transition.FromState = NewName;
                    }
                    if (Transition.ToState == OldName)
                    {
                        Transition.ToState = NewName;
                    }
                }
                if (StateMachine->InitialState == OldName)
                {
                    StateMachine->InitialState = NewName;
                }
                SelectedState = NewName;
                MarkDirty();
            }
        }

        char PathBuffer[260];
        CopyToBuffer(PathBuffer, sizeof(PathBuffer), State->AnimationPath);
        if (ImGui::InputText("Animation", PathBuffer, sizeof(PathBuffer)))
        {
            State->AnimationPath = PathBuffer;
            MarkDirty();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("AnimSequenceContentItem"))
            {
                if (Payload->Data && Payload->DataSize > 0)
                {
                    State->AnimationPath = static_cast<const char*>(Payload->Data);
                    MarkDirty();
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::Checkbox("Loop", &State->bLoop))
        {
            MarkDirty();
        }
        if (ImGui::DragFloat("Play Rate", &State->PlayRate, 0.01f, 0.0f, 10.0f))
        {
            MarkDirty();
        }
        if (ImGui::DragFloat2("Graph Position", &State->GraphPosition.X, 1.0f))
        {
            MarkDirty();
        }
        if (ImGui::Button("Set Initial State"))
        {
            StateMachine->InitialState = State->Name;
            MarkDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Connect From"))
        {
            BeginConnectFromSelectedState();
        }
        return;
    }

    if (SelectionType == ESelectionType::Transition)
    {
        FAnimTransitionDef* Transition = FindMutableTransition(SelectedTransitionId);
        if (!Transition)
        {
            ImGui::TextDisabled("Transition not found.");
            return;
        }

        ImGui::Text("From: %s", Transition->FromState.ToString().c_str());
        ImGui::Text("To: %s", Transition->ToState.ToString().c_str());
        if (ImGui::DragFloat("Blend Time", &Transition->BlendTime, 0.01f, 0.0f, 5.0f))
        {
            MarkDirty();
        }
        if (ImGui::Checkbox("Reset Time", &Transition->bResetTime))
        {
            MarkDirty();
        }
        ImGui::Separator();
        DrawTransitionConditions(*Transition);
        if (ImGui::Button("Delete Transition"))
        {
            DeleteSelection();
        }
        return;
    }

    ImGui::TextDisabled("Select a state or transition.");
}

void FEditorAnimationStateMachineWidget::DrawTransitionConditions(FAnimTransitionDef& Transition)
{
    ImGui::Text("Conditions: %d", static_cast<int32>(Transition.Conditions.size()));

    ImGui::PushID("TransitionConditions");
    static int32 NewConditionType = 0;
    const EAnimConditionType NewType = static_cast<EAnimConditionType>(NewConditionType);
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("New Condition", ConditionTypeName(NewType)))
    {
        for (int32 TypeIndex = 0; TypeIndex <= static_cast<int32>(EAnimConditionType::Composite); ++TypeIndex)
        {
            const EAnimConditionType Type = static_cast<EAnimConditionType>(TypeIndex);
            if (ImGui::Selectable(ConditionTypeName(Type), NewConditionType == TypeIndex))
            {
                NewConditionType = TypeIndex;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add"))
    {
        if (UAnimTransitionCondition* Condition = CreateAnimTransitionCondition(static_cast<EAnimConditionType>(NewConditionType)))
        {
            Transition.Conditions.push_back(Condition);
            MarkDirty();
        }
    }

    for (int32 Index = 0; Index < static_cast<int32>(Transition.Conditions.size());)
    {
        UAnimTransitionCondition*& Condition = Transition.Conditions[Index];
        ImGui::PushID(Index);
        const bool bDelete = ImGui::SmallButton("Delete");
        ImGui::SameLine();
        const bool bChanged = DrawConditionEditor(Condition, 0);
        ImGui::PopID();

        if (bDelete)
        {
            DestroyEditorConditionTree(Condition);
            Transition.Conditions.erase(Transition.Conditions.begin() + Index);
            MarkDirty();
            continue;
        }

        if (bChanged)
        {
            MarkDirty();
        }
        ++Index;
    }
    ImGui::PopID();
}

bool FEditorAnimationStateMachineWidget::DrawConditionEditor(UAnimTransitionCondition*& Condition, int32 Depth)
{
    if (!Condition)
    {
        ImGui::TextDisabled("Null condition");
        return false;
    }

    bool bChanged = false;
    EAnimConditionType Type = Condition->GetConditionType();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("Type", ConditionTypeName(Type)))
    {
        for (int32 TypeIndex = 0; TypeIndex <= static_cast<int32>(EAnimConditionType::Composite); ++TypeIndex)
        {
            const EAnimConditionType Candidate = static_cast<EAnimConditionType>(TypeIndex);
            if (ImGui::Selectable(ConditionTypeName(Candidate), Type == Candidate))
            {
                if (Type != Candidate)
                {
                    DestroyEditorConditionTree(Condition);
                    Condition = CreateAnimTransitionCondition(Candidate);
                    Type = Candidate;
                    bChanged = true;
                }
            }
        }
        ImGui::EndCombo();
    }

    if (!Condition)
    {
        return bChanged;
    }

    switch (Type)
    {
    case EAnimConditionType::Bool:
    {
        UAnimBoolCondition* BoolCondition = static_cast<UAnimBoolCondition*>(Condition);
        char ParamBuffer[128];
        CopyToBuffer(ParamBuffer, sizeof(ParamBuffer), BoolCondition->ParamName.ToString());
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputText("Param", ParamBuffer, sizeof(ParamBuffer)))
        {
            BoolCondition->ParamName = FName(ParamBuffer);
            bChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Expected", &BoolCondition->ExpectedValue))
        {
            bChanged = true;
        }
        break;
    }

    case EAnimConditionType::FloatCompare:
    {
        UAnimFloatCompareCondition* FloatCondition = static_cast<UAnimFloatCompareCondition*>(Condition);
        char ParamBuffer[128];
        CopyToBuffer(ParamBuffer, sizeof(ParamBuffer), FloatCondition->ParamName.ToString());
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputText("Param", ParamBuffer, sizeof(ParamBuffer)))
        {
            FloatCondition->ParamName = FName(ParamBuffer);
            bChanged = true;
        }

        int32 OpValue = static_cast<int32>(FloatCondition->Op);
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::BeginCombo("Op", CompareOpName(FloatCondition->Op)))
        {
            for (int32 OpIndex = 0; OpIndex <= static_cast<int32>(EAnimCompareOp::GreaterEqual); ++OpIndex)
            {
                const EAnimCompareOp Candidate = static_cast<EAnimCompareOp>(OpIndex);
                if (ImGui::Selectable(CompareOpName(Candidate), OpValue == OpIndex))
                {
                    FloatCondition->Op = Candidate;
                    bChanged = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::DragFloat("Value", &FloatCondition->Threshold, 0.01f))
        {
            bChanged = true;
        }
        break;
    }

    case EAnimConditionType::StateTime:
    {
        UAnimStateTimeCondition* StateTimeCondition = static_cast<UAnimStateTimeCondition*>(Condition);
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::DragFloat("Normalized Time", &StateTimeCondition->NormalizedTime, 0.01f, 0.0f, 1.0f))
        {
            bChanged = true;
        }
        break;
    }

    case EAnimConditionType::Composite:
    {
        AnimCompositeCondition* Composite = static_cast<AnimCompositeCondition*>(Condition);
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::BeginCombo("Op", CompositeOpName(Composite->Op)))
        {
            for (int32 OpIndex = 0; OpIndex <= static_cast<int32>(EAnimConditionOp::Not); ++OpIndex)
            {
                const EAnimConditionOp Candidate = static_cast<EAnimConditionOp>(OpIndex);
                if (ImGui::Selectable(CompositeOpName(Candidate), Composite->Op == Candidate))
                {
                    Composite->Op = Candidate;
                    bChanged = true;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Indent(18.0f);
        static int32 NewChildType = 0;
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("Child Type", ConditionTypeName(static_cast<EAnimConditionType>(NewChildType))))
        {
            for (int32 TypeIndex = 0; TypeIndex <= static_cast<int32>(EAnimConditionType::Composite); ++TypeIndex)
            {
                const EAnimConditionType Candidate = static_cast<EAnimConditionType>(TypeIndex);
                if (ImGui::Selectable(ConditionTypeName(Candidate), NewChildType == TypeIndex))
                {
                    NewChildType = TypeIndex;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Child"))
        {
            if (UAnimTransitionCondition* Child = CreateAnimTransitionCondition(static_cast<EAnimConditionType>(NewChildType)))
            {
                Composite->Childrens.push_back(Child);
                bChanged = true;
            }
        }

        for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Composite->Childrens.size());)
        {
            UAnimTransitionCondition*& Child = Composite->Childrens[ChildIndex];
            ImGui::PushID(ChildIndex);
            const bool bDeleteChild = ImGui::SmallButton("Delete Child");
            ImGui::SameLine();
            bChanged |= DrawConditionEditor(Child, Depth + 1);
            ImGui::PopID();

            if (bDeleteChild)
            {
                DestroyEditorConditionTree(Child);
                Composite->Childrens.erase(Composite->Childrens.begin() + ChildIndex);
                bChanged = true;
                continue;
            }
            ++ChildIndex;
        }
        ImGui::Unindent(18.0f);
        break;
    }

    default:
        break;
    }

    (void)Depth;
    return bChanged;
}

void FEditorAnimationStateMachineWidget::AddStateAt(const FVector2& GraphPosition)
{
    if (!StateMachine)
    {
        return;
    }

    FAnimStateDef State;
    State.Name = MakeUniqueStateName();
    State.GraphPosition = GraphPosition;
    State.bLoop = true;
    State.PlayRate = 1.0f;
    StateMachine->States.push_back(State);

    if (StateMachine->InitialState == FName::None)
    {
        StateMachine->InitialState = State.Name;
    }

    SelectionType = ESelectionType::State;
    SelectedState = State.Name;
    SelectedTransitionId = FName::None;
    MarkDirty();
}

void FEditorAnimationStateMachineWidget::DeleteSelection()
{
    if (!StateMachine)
    {
        return;
    }

    if (SelectionType == ESelectionType::State)
    {
        const FName StateName = SelectedState;
        StateMachine->States.erase(
            std::remove_if(StateMachine->States.begin(), StateMachine->States.end(),
                [StateName](const FAnimStateDef& State) { return State.Name == StateName; }),
            StateMachine->States.end());
        StateMachine->Transitions.erase(
            std::remove_if(StateMachine->Transitions.begin(), StateMachine->Transitions.end(),
                [StateName](const FAnimTransitionDef& Transition)
                {
                    return Transition.FromState == StateName || Transition.ToState == StateName;
                }),
            StateMachine->Transitions.end());
        if (StateMachine->InitialState == StateName)
        {
            StateMachine->InitialState = StateMachine->States.empty() ? FName::None : StateMachine->States.front().Name;
        }
    }
    else if (SelectionType == ESelectionType::Transition)
    {
        const FName TransitionId = SelectedTransitionId;
        StateMachine->Transitions.erase(
            std::remove_if(StateMachine->Transitions.begin(), StateMachine->Transitions.end(),
                [TransitionId](const FAnimTransitionDef& Transition)
                {
                    return Transition.TransitionId == TransitionId;
                }),
            StateMachine->Transitions.end());
    }

    SelectionType = ESelectionType::None;
    SelectedState = FName::None;
    SelectedTransitionId = FName::None;
    PendingConnectFromState = FName::None;
    MarkDirty();
}

void FEditorAnimationStateMachineWidget::BeginConnectFromSelectedState()
{
    if (SelectionType == ESelectionType::State && SelectedState != FName::None)
    {
        PendingConnectFromState = SelectedState;
    }
}

void FEditorAnimationStateMachineWidget::TryCompleteConnectToState(const FName& TargetState)
{
    if (!StateMachine || PendingConnectFromState == FName::None || PendingConnectFromState == TargetState)
    {
        PendingConnectFromState = FName::None;
        return;
    }

    FAnimTransitionDef Transition;
    Transition.TransitionId = MakeUniqueTransitionId();
    Transition.FromState = PendingConnectFromState;
    Transition.ToState = TargetState;
    Transition.BlendTime = 0.2f;
    Transition.bResetTime = true;
    StateMachine->Transitions.push_back(Transition);

    SelectionType = ESelectionType::Transition;
    SelectedTransitionId = Transition.TransitionId;
    SelectedState = FName::None;
    PendingConnectFromState = FName::None;
    MarkDirty();
}

void FEditorAnimationStateMachineWidget::MarkDirty()
{
    if (!bRestoringUndoSnapshot)
    {
        CommitUndoSnapshot(false);
    }
    bDirty = true;
}

FEditorAnimationStateMachineWidget::FConditionUndoSnapshot
FEditorAnimationStateMachineWidget::MakeConditionUndoSnapshot(const UAnimTransitionCondition* Condition) const
{
    FConditionUndoSnapshot Snapshot;
    if (!Condition)
    {
        return Snapshot;
    }

    Snapshot.Type = Condition->GetConditionType();
    switch (Snapshot.Type)
    {
    case EAnimConditionType::Bool:
    {
        const UAnimBoolCondition* BoolCondition = static_cast<const UAnimBoolCondition*>(Condition);
        Snapshot.ParamName = BoolCondition->ParamName;
        Snapshot.BoolValue = BoolCondition->ExpectedValue;
        break;
    }

    case EAnimConditionType::FloatCompare:
    {
        const UAnimFloatCompareCondition* FloatCondition = static_cast<const UAnimFloatCompareCondition*>(Condition);
        Snapshot.ParamName = FloatCondition->ParamName;
        Snapshot.CompareOp = FloatCondition->Op;
        Snapshot.FloatValue = FloatCondition->Threshold;
        break;
    }

    case EAnimConditionType::StateTime:
    {
        const UAnimStateTimeCondition* StateTimeCondition = static_cast<const UAnimStateTimeCondition*>(Condition);
        Snapshot.FloatValue = StateTimeCondition->NormalizedTime;
        break;
    }

    case EAnimConditionType::Composite:
    {
        const AnimCompositeCondition* CompositeCondition = static_cast<const AnimCompositeCondition*>(Condition);
        Snapshot.CompositeOp = CompositeCondition->Op;
        Snapshot.Children.reserve(CompositeCondition->Childrens.size());
        for (const UAnimTransitionCondition* Child : CompositeCondition->Childrens)
        {
            Snapshot.Children.push_back(MakeConditionUndoSnapshot(Child));
        }
        break;
    }

    default:
        break;
    }

    return Snapshot;
}

UAnimTransitionCondition*
FEditorAnimationStateMachineWidget::CreateConditionFromUndoSnapshot(const FConditionUndoSnapshot& Snapshot) const
{
    UAnimTransitionCondition* Condition = CreateAnimTransitionCondition(Snapshot.Type);
    if (!Condition)
    {
        return nullptr;
    }

    switch (Snapshot.Type)
    {
    case EAnimConditionType::Bool:
    {
        UAnimBoolCondition* BoolCondition = static_cast<UAnimBoolCondition*>(Condition);
        BoolCondition->ParamName = Snapshot.ParamName;
        BoolCondition->ExpectedValue = Snapshot.BoolValue;
        break;
    }

    case EAnimConditionType::FloatCompare:
    {
        UAnimFloatCompareCondition* FloatCondition = static_cast<UAnimFloatCompareCondition*>(Condition);
        FloatCondition->ParamName = Snapshot.ParamName;
        FloatCondition->Op = Snapshot.CompareOp;
        FloatCondition->Threshold = Snapshot.FloatValue;
        break;
    }

    case EAnimConditionType::StateTime:
    {
        UAnimStateTimeCondition* StateTimeCondition = static_cast<UAnimStateTimeCondition*>(Condition);
        StateTimeCondition->NormalizedTime = Snapshot.FloatValue;
        break;
    }

    case EAnimConditionType::Composite:
    {
        AnimCompositeCondition* CompositeCondition = static_cast<AnimCompositeCondition*>(Condition);
        CompositeCondition->Op = Snapshot.CompositeOp;
        CompositeCondition->Childrens.reserve(Snapshot.Children.size());
        for (const FConditionUndoSnapshot& ChildSnapshot : Snapshot.Children)
        {
            if (UAnimTransitionCondition* Child = CreateConditionFromUndoSnapshot(ChildSnapshot))
            {
                CompositeCondition->Childrens.push_back(Child);
            }
        }
        break;
    }

    default:
        break;
    }

    return Condition;
}

FEditorAnimationStateMachineWidget::FStateMachineUndoSnapshot
FEditorAnimationStateMachineWidget::MakeUndoSnapshot() const
{
    FStateMachineUndoSnapshot Snapshot;
    Snapshot.SelectionType = SelectionType;
    Snapshot.SelectedState = SelectedState;
    Snapshot.SelectedTransitionId = SelectedTransitionId;

    if (!StateMachine)
    {
        return Snapshot;
    }

    Snapshot.InitialState = StateMachine->InitialState;
    Snapshot.States = StateMachine->States;
    Snapshot.Transitions.reserve(StateMachine->Transitions.size());
    for (const FAnimTransitionDef& Transition : StateMachine->Transitions)
    {
        FTransitionUndoSnapshot TransitionSnapshot;
        TransitionSnapshot.TransitionId = Transition.TransitionId;
        TransitionSnapshot.FromState = Transition.FromState;
        TransitionSnapshot.ToState = Transition.ToState;
        TransitionSnapshot.BlendTime = Transition.BlendTime;
        TransitionSnapshot.bResetTime = Transition.bResetTime;
        TransitionSnapshot.Conditions.reserve(Transition.Conditions.size());
        for (const UAnimTransitionCondition* Condition : Transition.Conditions)
        {
            TransitionSnapshot.Conditions.push_back(MakeConditionUndoSnapshot(Condition));
        }
        Snapshot.Transitions.push_back(TransitionSnapshot);
    }

    return Snapshot;
}

FString FEditorAnimationStateMachineWidget::ComputeUndoFingerprint(const FStateMachineUndoSnapshot& Snapshot) const
{
    FString Fingerprint;
    Fingerprint += ToString(Snapshot.InitialState) + "|";
    Fingerprint += std::to_string(static_cast<int32>(Snapshot.SelectionType)) + "|";
    Fingerprint += ToString(Snapshot.SelectedState) + "|";
    Fingerprint += ToString(Snapshot.SelectedTransitionId) + "|";

    for (const FAnimStateDef& State : Snapshot.States)
    {
        Fingerprint += "S:";
        Fingerprint += ToString(State.Name) + ",";
        Fingerprint += State.AnimationPath + ",";
        Fingerprint += (State.bLoop ? "1," : "0,");
        Fingerprint += std::to_string(State.PlayRate) + ",";
        Fingerprint += std::to_string(State.GraphPosition.X) + ",";
        Fingerprint += std::to_string(State.GraphPosition.Y) + "|";
    }

    std::function<void(const FConditionUndoSnapshot&)> AppendCondition =
        [&Fingerprint, &AppendCondition](const FConditionUndoSnapshot& Condition)
        {
            Fingerprint += "C:";
            Fingerprint += std::to_string(static_cast<int32>(Condition.Type)) + ",";
            Fingerprint += ToString(Condition.ParamName) + ",";
            Fingerprint += std::to_string(static_cast<int32>(Condition.CompareOp)) + ",";
            Fingerprint += std::to_string(Condition.FloatValue) + ",";
            Fingerprint += (Condition.BoolValue ? "1," : "0,");
            Fingerprint += std::to_string(static_cast<int32>(Condition.CompositeOp)) + "{";
            for (const FConditionUndoSnapshot& Child : Condition.Children)
            {
                AppendCondition(Child);
            }
            Fingerprint += "}";
        };

    for (const FTransitionUndoSnapshot& Transition : Snapshot.Transitions)
    {
        Fingerprint += "T:";
        Fingerprint += ToString(Transition.TransitionId) + ",";
        Fingerprint += ToString(Transition.FromState) + ",";
        Fingerprint += ToString(Transition.ToState) + ",";
        Fingerprint += std::to_string(Transition.BlendTime) + ",";
        Fingerprint += (Transition.bResetTime ? "1," : "0,");
        for (const FConditionUndoSnapshot& Condition : Transition.Conditions)
        {
            AppendCondition(Condition);
        }
        Fingerprint += "|";
    }

    return Fingerprint;
}

void FEditorAnimationStateMachineWidget::RestoreUndoSnapshot(const FStateMachineUndoSnapshot& Snapshot)
{
    if (!StateMachine)
    {
        return;
    }

    bRestoringUndoSnapshot = true;

    for (FAnimTransitionDef& Transition : StateMachine->Transitions)
    {
        for (UAnimTransitionCondition* Condition : Transition.Conditions)
        {
            DestroyEditorConditionTree(Condition);
        }
        Transition.Conditions.clear();
    }

    StateMachine->InitialState = Snapshot.InitialState;
    StateMachine->States = Snapshot.States;
    StateMachine->Transitions.clear();
    StateMachine->Transitions.reserve(Snapshot.Transitions.size());
    for (const FTransitionUndoSnapshot& TransitionSnapshot : Snapshot.Transitions)
    {
        FAnimTransitionDef Transition;
        Transition.TransitionId = TransitionSnapshot.TransitionId;
        Transition.FromState = TransitionSnapshot.FromState;
        Transition.ToState = TransitionSnapshot.ToState;
        Transition.BlendTime = TransitionSnapshot.BlendTime;
        Transition.bResetTime = TransitionSnapshot.bResetTime;
        Transition.Conditions.reserve(TransitionSnapshot.Conditions.size());
        for (const FConditionUndoSnapshot& ConditionSnapshot : TransitionSnapshot.Conditions)
        {
            if (UAnimTransitionCondition* Condition = CreateConditionFromUndoSnapshot(ConditionSnapshot))
            {
                Transition.Conditions.push_back(Condition);
            }
        }
        StateMachine->Transitions.push_back(Transition);
    }

    SelectionType = Snapshot.SelectionType;
    SelectedState = Snapshot.SelectedState;
    SelectedTransitionId = Snapshot.SelectedTransitionId;
    PendingConnectFromState = FName::None;
    DraggingState = FName::None;
    bDirty = true;

    bRestoringUndoSnapshot = false;
}

void FEditorAnimationStateMachineWidget::ResetUndoHistory()
{
    UndoStack.clear();
    RedoStack.clear();
    LastUndoSnapshot = MakeUndoSnapshot();
    LastUndoFingerprint = ComputeUndoFingerprint(LastUndoSnapshot);
    bUndoBaselineValid = true;
}

void FEditorAnimationStateMachineWidget::CommitUndoSnapshot(bool bForce)
{
    if (!StateMachine || bRestoringUndoSnapshot)
    {
        return;
    }

    if (!bUndoBaselineValid)
    {
        ResetUndoHistory();
        return;
    }

    const FStateMachineUndoSnapshot CurrentSnapshot = MakeUndoSnapshot();
    const FString CurrentFingerprint = ComputeUndoFingerprint(CurrentSnapshot);
    if (!bForce && CurrentFingerprint == LastUndoFingerprint)
    {
        return;
    }

    UndoStack.push_back(LastUndoSnapshot);
    constexpr size_t MaxUndoSnapshots = 64;
    if (UndoStack.size() > MaxUndoSnapshots)
    {
        UndoStack.erase(UndoStack.begin());
    }

    RedoStack.clear();
    LastUndoSnapshot = CurrentSnapshot;
    LastUndoFingerprint = CurrentFingerprint;
}

bool FEditorAnimationStateMachineWidget::CanUndoGraphEdit() const
{
    return !UndoStack.empty();
}

bool FEditorAnimationStateMachineWidget::CanRedoGraphEdit() const
{
    return !RedoStack.empty();
}

void FEditorAnimationStateMachineWidget::UndoGraphEdit()
{
    if (!CanUndoGraphEdit())
    {
        return;
    }

    const FStateMachineUndoSnapshot CurrentSnapshot = MakeUndoSnapshot();
    const FStateMachineUndoSnapshot Snapshot = UndoStack.back();
    UndoStack.pop_back();
    RedoStack.push_back(CurrentSnapshot);
    RestoreUndoSnapshot(Snapshot);
    LastUndoSnapshot = Snapshot;
    LastUndoFingerprint = ComputeUndoFingerprint(LastUndoSnapshot);

    if (EditorEngine)
    {
        EditorEngine->GetNotificationService().Info("Undo Animation State Machine edit.");
    }
}

void FEditorAnimationStateMachineWidget::RedoGraphEdit()
{
    if (!CanRedoGraphEdit())
    {
        return;
    }

    const FStateMachineUndoSnapshot CurrentSnapshot = MakeUndoSnapshot();
    const FStateMachineUndoSnapshot Snapshot = RedoStack.back();
    RedoStack.pop_back();
    UndoStack.push_back(CurrentSnapshot);
    RestoreUndoSnapshot(Snapshot);
    LastUndoSnapshot = Snapshot;
    LastUndoFingerprint = ComputeUndoFingerprint(LastUndoSnapshot);

    if (EditorEngine)
    {
        EditorEngine->GetNotificationService().Info("Redo Animation State Machine edit.");
    }
}

bool FEditorAnimationStateMachineWidget::SaveAsset()
{
    if (!StateMachine || CurrentPath.empty())
    {
        return false;
    }

    if (!FResourceManager::Get().SaveAnimationStateMachine(CurrentPath, StateMachine))
    {
        if (EditorEngine)
        {
            EditorEngine->GetNotificationService().Error("Failed to save Animation State Machine.");
        }
        return false;
    }

    bDirty = false;
    if (EditorEngine)
    {
        EditorEngine->GetNotificationService().Info("Saved Animation State Machine.");
        EditorEngine->GetMainPanel().RefreshContentBrowser();
    }
    return true;
}

FAnimStateDef* FEditorAnimationStateMachineWidget::FindMutableState(FName Name)
{
    if (!StateMachine)
    {
        return nullptr;
    }
    for (FAnimStateDef& State : StateMachine->States)
    {
        if (State.Name == Name)
        {
            return &State;
        }
    }
    return nullptr;
}

FAnimTransitionDef* FEditorAnimationStateMachineWidget::FindMutableTransition(FName TransitionId)
{
    if (!StateMachine)
    {
        return nullptr;
    }
    for (FAnimTransitionDef& Transition : StateMachine->Transitions)
    {
        if (Transition.TransitionId == TransitionId)
        {
            return &Transition;
        }
    }
    return nullptr;
}

int32 FEditorAnimationStateMachineWidget::FindStateIndex(FName Name) const
{
    if (!StateMachine)
    {
        return -1;
    }
    for (int32 Index = 0; Index < static_cast<int32>(StateMachine->States.size()); ++Index)
    {
        if (StateMachine->States[Index].Name == Name)
        {
            return Index;
        }
    }
    return -1;
}

int32 FEditorAnimationStateMachineWidget::FindTransitionIndex(FName TransitionId) const
{
    if (!StateMachine)
    {
        return -1;
    }
    for (int32 Index = 0; Index < static_cast<int32>(StateMachine->Transitions.size()); ++Index)
    {
        if (StateMachine->Transitions[Index].TransitionId == TransitionId)
        {
            return Index;
        }
    }
    return -1;
}

FName FEditorAnimationStateMachineWidget::MakeUniqueStateName() const
{
    int32 Index = static_cast<int32>(StateMachine ? StateMachine->States.size() : 0);
    while (true)
    {
        const FString Candidate = FString("State_") + std::to_string(Index++);
        if (!StateMachine || !StateMachine->FindState(FName(Candidate)))
        {
            return FName(Candidate);
        }
    }
}

FName FEditorAnimationStateMachineWidget::MakeUniqueTransitionId() const
{
    int32 Index = static_cast<int32>(StateMachine ? StateMachine->Transitions.size() : 0);
    while (true)
    {
        const FString Candidate = FString("Transition_") + std::to_string(Index++);
        bool bExists = false;
        if (StateMachine)
        {
            for (const FAnimTransitionDef& Transition : StateMachine->Transitions)
            {
                if (Transition.TransitionId == FName(Candidate))
                {
                    bExists = true;
                    break;
                }
            }
        }
        if (!bExists)
        {
            return FName(Candidate);
        }
    }
}

ImVec2 FEditorAnimationStateMachineWidget::GraphToScreen(const FVector2& GraphPosition, const ImVec2& CanvasOrigin) const
{
    return ImVec2(
        CanvasOrigin.x + CanvasPan.x + GraphPosition.X * CanvasZoom,
        CanvasOrigin.y + CanvasPan.y + GraphPosition.Y * CanvasZoom);
}

FVector2 FEditorAnimationStateMachineWidget::ScreenToGraph(const ImVec2& ScreenPosition, const ImVec2& CanvasOrigin) const
{
    return FVector2(
        (ScreenPosition.x - CanvasOrigin.x - CanvasPan.x) / CanvasZoom,
        (ScreenPosition.y - CanvasOrigin.y - CanvasPan.y) / CanvasZoom);
}
