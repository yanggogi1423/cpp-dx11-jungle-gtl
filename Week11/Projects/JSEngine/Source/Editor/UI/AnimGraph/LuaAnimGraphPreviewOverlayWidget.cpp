#include "Editor/UI/AnimGraph/LuaAnimGraphPreviewOverlayWidget.h"

#include "Asset/SkeletalMesh.h"
#include "Core/ResourceManager.h"
#include "Editor/Animation/AnimGraphPreviewInstance.h"
#include "Editor/Animation/DebugSkelMeshComponent.h"
#include "Editor/EditorEngine.h"
#include "Editor/Viewer/EditorViewer.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "GameFramework/PrimitiveActors.h"

#include "ImGui/imgui.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

namespace
{
void SetOpaqueBlendStateCallback(const ImDrawList*, const ImDrawCmd* Cmd)
{
    ID3D11DeviceContext* Context = static_cast<ID3D11DeviceContext*>(Cmd->UserCallbackData);
    if (!Context)
    {
        return;
    }

    float BlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    UINT SampleMask = 0xffffffff;
    Context->OMSetBlendState(nullptr, BlendFactor, SampleMask);
}

POINT ImGuiScreenToClientPoint(FWindowsWindow* Window, const ImVec2& Point)
{
    POINT Result{
        static_cast<LONG>(std::lround(Point.x)),
        static_cast<LONG>(std::lround(Point.y))
    };

    if (Window && Window->GetHWND())
    {
        ::ScreenToClient(Window->GetHWND(), &Result);
    }

    return Result;
}

const char* ToPreviewModeLabel(ELuaAnimGraphPreviewMode Mode)
{
    switch (Mode)
    {
    case ELuaAnimGraphPreviewMode::State:
        return "State";
    case ELuaAnimGraphPreviewMode::Transition:
        return "Transition";
    case ELuaAnimGraphPreviewMode::None:
    default:
        return "None";
    }
}

const char* ToBlendModeLabel(EAnimBlendMode Mode)
{
    switch (Mode)
    {
    case EAnimBlendMode::EaseIn:
        return "EaseIn";
    case EAnimBlendMode::EaseOut:
        return "EaseOut";
    case EAnimBlendMode::EaseInOut:
        return "EaseInOut";
    case EAnimBlendMode::Linear:
    default:
        return "Linear";
    }
}

EAnimLuaBlendMode ToPreviewBlendMode(EAnimBlendMode Mode)
{
    // Keep this mapping aligned with ULuaAnimInstance::ParseLuaBlendMode until
    // transition blending is extracted into a shared animation utility.
    return Mode == EAnimBlendMode::EaseInOut
               ? EAnimLuaBlendMode::EaseInOut
               : EAnimLuaBlendMode::Linear;
}

FString BoolText(bool bValue)
{
    return bValue ? "true" : "false";
}

FString MakeClipSignature(const FLuaAnimStateNode& State)
{
    FString Signature;
    Signature += std::to_string(State.GetStateId());
    Signature += "|";
    Signature += State.Name;
    Signature += "|";
    Signature += State.AnimationPath;
    Signature += "|";
    Signature += BoolText(State.bLoop);
    Signature += "|";
    Signature += std::to_string(State.PlayRate);
    return Signature;
}

FString MakeTransitionSignature(
    const FLuaAnimTransitionLink& Transition,
    const FLuaAnimStateNode& FromState,
    const FLuaAnimStateNode& ToState)
{
    FString Signature;
    Signature += std::to_string(Transition.GetTransitionId());
    Signature += "|";
    Signature += MakeClipSignature(FromState);
    Signature += "|";
    Signature += MakeClipSignature(ToState);
    Signature += "|";
    Signature += std::to_string(Transition.GetBlendTime());
    Signature += "|";
    Signature += BoolText(Transition.ShouldResetTime());
    Signature += "|";
    Signature += std::to_string(static_cast<int32>(Transition.GetBlendMode()));
    return Signature;
}

FLuaAnimGraphPreviewClipDesc MakeClipDesc(const FLuaAnimStateNode& State)
{
    FLuaAnimGraphPreviewClipDesc Desc;
    Desc.DebugName = State.GetName();
    Desc.AnimationPath = State.AnimationPath;
    Desc.bLoop = State.bLoop;
    Desc.PlayRate = State.PlayRate;
    return Desc;
}
} // namespace

void FLuaAnimGraphPreviewOverlayWidget::Initialize(UEditorEngine* InEditorEngine)
{
    EditorEngine = InEditorEngine;
    PreviewViewerDocumentPath =
        "LuaAnimGraphPreviewOverlay_" +
        std::to_string(reinterpret_cast<std::uintptr_t>(this));
}

void FLuaAnimGraphPreviewOverlayWidget::Shutdown()
{
    if (EditorEngine && Viewer)
    {
        EditorEngine->RemoveViewer(Viewer);
    }

    Viewer = nullptr;
    PreviewComponent = nullptr;
    LastPreviewStateId = 0;
    LastPreviewTransitionId = 0;
    LastPreviewSignature.clear();
    LastPreviewMeshPath.clear();
    CurrentStateName.clear();
    CurrentFromStateName.clear();
    CurrentToStateName.clear();
    CurrentBlendModeLabel.clear();
    CurrentBlendTime = 0.0f;
}

void FLuaAnimGraphPreviewOverlayWidget::DrawOverlay(
    float DeltaTime,
    FLuaAnimGraph& Graph,
    int32 SelectedStateId,
    int32 SelectedTransitionId,
    ImVec2 CanvasScreenPos,
    ImVec2 CanvasSize,
    ImGuiID OwnerViewportId)
{
    (void)DeltaTime;

    if (CanvasSize.x < 220.0f || CanvasSize.y < 180.0f)
    {
        return;
    }

    LastError.clear();

    if (SelectedTransitionId != 0)
    {
        FLuaAnimTransitionLink* Transition = Graph.FindTransition(SelectedTransitionId);
        const FLuaAnimStateNode* FromState =
            Transition ? Graph.FindState(Transition->GetFromStateId()) : nullptr;
        const FLuaAnimStateNode* ToState =
            Transition ? Graph.FindState(Transition->GetToStateId()) : nullptr;

        if (!Transition || !FromState || !ToState)
        {
            LastError = "Transition preview target is missing.";
            ClearPreviewSelection();
        }
        else
        {
            SyncTransitionPreview(Graph, *Transition, *FromState, *ToState);
        }
    }
    else if (SelectedStateId != 0)
    {
        FLuaAnimStateNode* State = Graph.FindState(SelectedStateId);
        if (!State)
        {
            LastError = "State preview target is missing.";
            ClearPreviewSelection();
        }
        else
        {
            SyncStatePreview(Graph, *State);
        }
    }
    else
    {
        ClearPreviewSelection();
    }

    const float Margin = 12.0f;
    const ImVec2 OverlaySize(
        std::min(360.0f, std::max(180.0f, CanvasSize.x - Margin * 2.0f)),
        std::min(360.0f, std::max(160.0f, CanvasSize.y - Margin * 2.0f)));
    const ImVec2 OverlayPos(CanvasScreenPos.x + Margin, CanvasScreenPos.y + Margin);

    (void)OwnerViewportId;

    const ImGuiWindowFlags Flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const ImVec2 OverlayMax(OverlayPos.x + OverlaySize.x, OverlayPos.y + OverlaySize.y);
    DrawList->AddRectFilled(
        OverlayPos,
        OverlayMax,
        ImGui::GetColorU32(ImVec4(0.055f, 0.060f, 0.072f, 0.96f)),
        6.0f);
    DrawList->AddRect(
        OverlayPos,
        OverlayMax,
        ImGui::GetColorU32(ImVec4(0.20f, 0.23f, 0.28f, 1.0f)),
        6.0f);

    ImGui::SetCursorScreenPos(ImVec2(OverlayPos.x + 8.0f, OverlayPos.y + 8.0f));
    ImGui::BeginChild(
        "##LuaAnimGraphPreviewOverlay",
        ImVec2(OverlaySize.x - 16.0f, OverlaySize.y - 16.0f),
        false,
        Flags);

    const float ViewportHeight = std::max(96.0f, OverlaySize.y - 158.0f);
    DrawViewport(ImVec2(OverlaySize.x - 36.0f, ViewportHeight));
    DrawPreviewControls();

    if (!LastError.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", LastError.c_str());
    }

    ImGui::EndChild();
    ImGui::SetCursorScreenPos(ImVec2(CanvasScreenPos.x, CanvasScreenPos.y + std::max(0.0f, CanvasSize.y - 1.0f)));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
}

bool FLuaAnimGraphPreviewOverlayWidget::EnsurePreviewViewer(const FString& PreviewSkeletalMeshPath)
{
    if (PreviewSkeletalMeshPath.empty())
    {
        LastError = "No preview skeletal mesh assigned.";
        return false;
    }

    USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(PreviewSkeletalMeshPath);
    if (!Mesh)
    {
        LastError = "Failed to load preview skeletal mesh.";
        return false;
    }

    if (!EditorEngine)
    {
        LastError = "Editor engine is not available.";
        return false;
    }

    if (!Viewer)
    {
        Viewer = EditorEngine->CreateAnimationSequencePreviewViewer(
            PreviewViewerDocumentPath,
            PreviewSkeletalMeshPath);
    }

    if (!Viewer)
    {
        LastError = "Failed to create preview viewer.";
        return false;
    }

    Viewer->SetRenderWithoutEditorTab(true);

    if (LastPreviewMeshPath != PreviewSkeletalMeshPath)
    {
        Viewer->ChangeTarget(PreviewSkeletalMeshPath, PreviewViewerDocumentPath);
        LastPreviewMeshPath = PreviewSkeletalMeshPath;
    }

    PreviewComponent = ResolvePreviewComponent();
    if (!PreviewComponent)
    {
        LastError = "Failed to create preview component.";
        return false;
    }

    return true;
}

UDebugSkelMeshComponent* FLuaAnimGraphPreviewOverlayWidget::ResolvePreviewComponent() const
{
    ASkeletalMeshActor* ViewTarget = Viewer ? Viewer->GetViewTarget() : nullptr;
    return ViewTarget
               ? Cast<UDebugSkelMeshComponent>(ViewTarget->GetSkeletalMeshComponent())
               : nullptr;
}

bool FLuaAnimGraphPreviewOverlayWidget::SyncStatePreview(
    FLuaAnimGraph& Graph,
    const FLuaAnimStateNode& State)
{
    if (State.AnimationPath.empty())
    {
        LastError = "State animation path is empty.";
        ClearPreviewSelection();
        return false;
    }

    if (!EnsurePreviewViewer(Graph.PreviewSkeletalMeshPath))
    {
        ClearPreviewSelection();
        return false;
    }

    const FString Signature =
        "State|" + Graph.PreviewSkeletalMeshPath + "|" + MakeClipSignature(State);

    if (LastPreviewStateId == State.GetStateId() &&
        LastPreviewTransitionId == 0 &&
        LastPreviewSignature == Signature)
    {
        if (ULuaAnimGraphPreviewInstance* Instance = PreviewComponent->GetLuaAnimGraphPreviewInstance())
        {
            Instance->SetPlaying(bPlaying);
        }
        return true;
    }

    if (!PreviewComponent->SetLuaAnimGraphPreviewState(MakeClipDesc(State)))
    {
        LastError = "Failed to load state animation sequence.";
        ClearPreviewSelection();
        return false;
    }

    LastPreviewStateId = State.GetStateId();
    LastPreviewTransitionId = 0;
    LastPreviewSignature = Signature;
    CurrentStateName = State.GetName();
    CurrentFromStateName.clear();
    CurrentToStateName.clear();
    CurrentBlendModeLabel.clear();
    CurrentBlendTime = 0.0f;

    if (ULuaAnimGraphPreviewInstance* Instance = PreviewComponent->GetLuaAnimGraphPreviewInstance())
    {
        Instance->SetPlaying(bPlaying);
    }

    return true;
}

bool FLuaAnimGraphPreviewOverlayWidget::SyncTransitionPreview(
    FLuaAnimGraph& Graph,
    const FLuaAnimTransitionLink& Transition,
    const FLuaAnimStateNode& FromState,
    const FLuaAnimStateNode& ToState)
{
    if (FromState.AnimationPath.empty() || ToState.AnimationPath.empty())
    {
        LastError = "Transition state animation path is empty.";
        ClearPreviewSelection();
        return false;
    }

    if (!EnsurePreviewViewer(Graph.PreviewSkeletalMeshPath))
    {
        ClearPreviewSelection();
        return false;
    }

    const FString Signature =
        "Transition|" +
        Graph.PreviewSkeletalMeshPath +
        "|" +
        MakeTransitionSignature(Transition, FromState, ToState);

    if (LastPreviewTransitionId == Transition.GetTransitionId() &&
        LastPreviewStateId == 0 &&
        LastPreviewSignature == Signature)
    {
        if (ULuaAnimGraphPreviewInstance* Instance = PreviewComponent->GetLuaAnimGraphPreviewInstance())
        {
            Instance->SetPlaying(bPlaying);
        }
        return true;
    }

    FLuaAnimGraphPreviewTransitionDesc Desc;
    Desc.From = MakeClipDesc(FromState);
    Desc.To = MakeClipDesc(ToState);
    Desc.BlendTime = Transition.GetBlendTime();
    Desc.bResetTime = Transition.ShouldResetTime();
    Desc.BlendMode = ToPreviewBlendMode(Transition.GetBlendMode());

    if (!PreviewComponent->SetLuaAnimGraphPreviewTransition(Desc))
    {
        LastError = "Failed to load transition animation sequence.";
        ClearPreviewSelection();
        return false;
    }

    LastPreviewStateId = 0;
    LastPreviewTransitionId = Transition.GetTransitionId();
    LastPreviewSignature = Signature;
    CurrentStateName.clear();
    CurrentFromStateName = FromState.GetName();
    CurrentToStateName = ToState.GetName();
    CurrentBlendModeLabel = ToBlendModeLabel(Transition.GetBlendMode());
    CurrentBlendTime = Transition.GetBlendTime();

    if (ULuaAnimGraphPreviewInstance* Instance = PreviewComponent->GetLuaAnimGraphPreviewInstance())
    {
        Instance->SetPlaying(bPlaying);
    }

    return true;
}

void FLuaAnimGraphPreviewOverlayWidget::ClearPreviewSelection()
{
    if (PreviewComponent)
    {
        PreviewComponent->ClearLuaAnimGraphPreview();
    }

    LastPreviewStateId = 0;
    LastPreviewTransitionId = 0;
    LastPreviewSignature.clear();
    CurrentStateName.clear();
    CurrentFromStateName.clear();
    CurrentToStateName.clear();
    CurrentBlendModeLabel.clear();
    CurrentBlendTime = 0.0f;
}

void FLuaAnimGraphPreviewOverlayWidget::DrawViewport(const ImVec2& Size)
{
    ImGui::BeginChild(
        "##LuaAnimGraphPreviewViewport",
        Size,
        true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    FSceneViewport* SceneViewport = Viewer ? &Viewer->GetViewport() : nullptr;
    ID3D11ShaderResourceView* SRV = SceneViewport ? SceneViewport->GetOutSRV() : nullptr;

    ImGui::Dummy(Size);
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    if (SceneViewport)
    {
        const POINT ClientMin =
            ImGuiScreenToClientPoint(EditorEngine ? EditorEngine->GetWindow() : nullptr, Min);

        FViewportRect NewRect;
        NewRect.X = static_cast<int32>(ClientMin.x);
        NewRect.Y = static_cast<int32>(ClientMin.y);
        NewRect.Width = static_cast<int32>(Max.x - Min.x);
        NewRect.Height = static_cast<int32>(Max.y - Min.y);

        Viewer->SetRect(NewRect);

        if (ImGui::IsItemHovered() &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) &&
            EditorEngine)
        {
            EditorEngine->FocusViewportInput(SceneViewport);
        }
    }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    if (SRV)
    {
        ID3D11DeviceContext* Context = EditorEngine
                                           ? EditorEngine->GetRenderer().GetFD3DDevice().GetDeviceContext()
                                           : nullptr;

        DrawList->AddCallback(SetOpaqueBlendStateCallback, Context);
        DrawList->AddImage(reinterpret_cast<ImTextureID>(SRV), Min, Max);
        DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
    else
    {
        DrawList->AddRectFilled(
            Min,
            Max,
            ImGui::GetColorU32(ImVec4(0.04f, 0.045f, 0.055f, 1.0f)));
        DrawList->AddText(
            ImVec2(Min.x + 10.0f, Min.y + 10.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            "Preview viewport");
    }

    ImGui::EndChild();
}

void FLuaAnimGraphPreviewOverlayWidget::DrawPreviewControls()
{
    ULuaAnimGraphPreviewInstance* Instance =
        PreviewComponent ? PreviewComponent->GetLuaAnimGraphPreviewInstance() : nullptr;

    if (Instance)
    {
        bPlaying = Instance->IsPlaying();
    }

    const ELuaAnimGraphPreviewMode Mode =
        Instance ? Instance->GetPreviewMode() : ELuaAnimGraphPreviewMode::None;

    ImGui::Text("Mode: %s", ToPreviewModeLabel(Mode));

    if (ImGui::Button(bPlaying ? "Pause" : "Play"))
    {
        bPlaying = !bPlaying;
        if (Instance)
        {
            Instance->SetPlaying(bPlaying);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset") && Instance)
    {
        Instance->ResetPreview();
        bPlaying = Instance->IsPlaying();
    }

    if (!Instance)
    {
        return;
    }

    if (Mode == ELuaAnimGraphPreviewMode::State)
    {
        ImGui::Text("State: %s", CurrentStateName.empty() ? "<none>" : CurrentStateName.c_str());
        ImGui::Text("Time: %.3f", Instance->GetCurrentTime());
        ImGui::Text("Normalized: %.3f", Instance->GetCurrentNormalizedTime());
    }
    else if (Mode == ELuaAnimGraphPreviewMode::Transition)
    {
        ImGui::Text("From: %s", CurrentFromStateName.empty() ? "<none>" : CurrentFromStateName.c_str());
        ImGui::Text("To: %s", CurrentToStateName.empty() ? "<none>" : CurrentToStateName.c_str());
        ImGui::Text("BlendTime: %.3f", CurrentBlendTime);
        ImGui::Text("BlendMode: %s", CurrentBlendModeLabel.empty() ? "Linear" : CurrentBlendModeLabel.c_str());
        ImGui::Text("Raw Alpha: %.3f", Instance->GetTransitionRawAlpha());
        ImGui::Text("Effective Alpha: %.3f", Instance->GetTransitionBlendAlpha());
    }
}
