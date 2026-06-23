#include "Editor/UI/EditorLuaAnimGraphWidget.h"

#include "Animation/AnimLuaProgramAsset.h"
#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Core/Paths.h"
#include "Editor/EditorEngine.h"
#include "Editor/Notification/EditorNotificationService.h"
#include "Editor/UI/EditorDetachedDocumentChrome.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_node_editor.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace ed = ax::NodeEditor;

namespace
{
FString GetFileNameFromPath(const FString& Path)
{
    const size_t SlashIndex = Path.find_last_of("/\\");
    return SlashIndex == FString::npos ? Path : Path.substr(SlashIndex + 1);
}

FString GetFileStemFromPath(const FString& Path)
{
    FString FileName = GetFileNameFromPath(Path);

    const size_t DotIndex = FileName.find_last_of('.');
    if (DotIndex != FString::npos)
    {
        FileName = FileName.substr(0, DotIndex);
    }

    return FileName.empty() ? "NewState" : FileName;
}

bool IsScreenPointInsideRect(const ImVec2& Point, const ImVec2& RectMin, const ImVec2& RectSize)
{
    return Point.x >= RectMin.x &&
           Point.y >= RectMin.y &&
           Point.x < RectMin.x + RectSize.x &&
           Point.y < RectMin.y + RectSize.y;
}
} // namespace

void FEditorLuaAnimGraphWidget::Initialize(UEditorEngine* InEditorEngine)
{
    FEditorWidget::Initialize(InEditorEngine);

    if (!NodeEditorContext)
    {
        ed::Config Config;
        Config.SettingsFile = "LuaAnimGraphNodeEditor.json";
        NodeEditorContext = ed::CreateEditor(&Config);
    }

    PreviewOverlayWidget.Initialize(InEditorEngine);
}

void FEditorLuaAnimGraphWidget::Shutdown()
{
    PreviewOverlayWidget.Shutdown();

    if (NodeEditorContext)
    {
        ed::DestroyEditor(static_cast<ed::EditorContext*>(NodeEditorContext));
        NodeEditorContext = nullptr;
    }
}

void FEditorLuaAnimGraphWidget::Render(float DeltaTime)
{
    if (!bOpen)
    {
        return;
    }

    bool bWindowOpen = bOpen;
    bool bCloseRequested = false;

    FEditorDetachedDocumentChrome::PushDetachedWindowStyle();
    FEditorDetachedDocumentChrome::ApplyWindowClass();
    FEditorDetachedDocumentChrome::SetFirstUseWindowPlacement(ImVec2(1280.0f, 780.0f), ImVec2(160.0f, 120.0f));
    constexpr ImGuiWindowFlags WindowFlags =
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin(GetWindowName().c_str(), &bWindowOpen, WindowFlags))
    {
        ImGui::End();
        FEditorDetachedDocumentChrome::PopDetachedWindowStyle();
        bOpen = bWindowOpen;
        return;
    }

    FEditorDetachedDocumentChrome::RenderChrome(
        GetWindowName(),
        [this, &bWindowOpen]()
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save Asset", "Ctrl+S", false, bLoaded))
                {
                    SaveAsset();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Close"))
                {
                    bWindowOpen = false;
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
        bWindowOpen = false;
    }

    bOpen = bWindowOpen;
}

void FEditorLuaAnimGraphWidget::RenderEmbedded(float DeltaTime)
{
    DrawContent(DeltaTime, false);
}

void FEditorLuaAnimGraphWidget::Close()
{
    bOpen = false;
    PreviewOverlayWidget.Shutdown();
}

bool FEditorLuaAnimGraphWidget::OpenAsset(const FString& InAssetPath)
{
    AssetPath = FPaths::Normalize(InAssetPath);

    bLoaded = LoadAssetPayload();
    bOpen = bLoaded;

    AssignDefaultGraphIfEmpty();
    RegenerateLuaSource();

    bDirty = false;
    bInitializedNodePositions = false;
    SelectedStateId = 0;
    SelectedTransitionId = 0;
    ResetUndoHistory();

    return bLoaded;
}

FString FEditorLuaAnimGraphWidget::GetWindowName() const
{
    FString WindowName = "Lua Anim Graph";
    if (!AssetPath.empty())
    {
        WindowName += " - ";
        WindowName += GetFileNameFromPath(AssetPath);
    }
    WindowName += "###LuaAnimGraphEditor";
    return WindowName;
}

bool FEditorLuaAnimGraphWidget::ConsumeDockRequest()
{
    const bool bRequested = bDockRequested;
    bDockRequested = false;
    return bRequested;
}

bool FEditorLuaAnimGraphWidget::LoadAssetPayload()
{
    LastError.clear();

    FAssetMetaData MetaData;
    FAnimLuaProgramAssetPayload Payload;

    const bool bLoaded = FAssetFile::Load(AssetPath, MetaData, [&](FArchive& Ar)
                                          {
        Payload.Serialize(Ar, MetaData.PayloadVersion);
        return true; });

    if (!bLoaded)
    {
        LastError = "Failed to load Lua Anim Graph asset.";
        return false;
    }

    if (MetaData.ClassName != UAnimLuaProgramAsset::StaticClass()->ClassName)
    {
        LastError = "Selected asset is not UAnimLuaProgramAsset.";
        return false;
    }

    Graph = Payload.Graph;
    GeneratedLuaSource = Payload.GeneratedLuaSource;
    return true;
}

void FEditorLuaAnimGraphWidget::AssignDefaultGraphIfEmpty()
{
    // 빈 그래프는 정상 상태입니다.
    // 더 이상 Idle / Walk / Attack 기본 State를 자동 생성하지 않습니다.

    if (Graph.MachineName.empty())
    {
        Graph.MachineName = "Machine";
    }

    if (Graph.NextId <= 0)
    {
        Graph.NextId = 1;
    }

    if (Graph.InitialStateId != 0 &&
        Graph.States.find(Graph.InitialStateId) == Graph.States.end())
    {
        Graph.InitialStateId = 0;
    }
}

bool FEditorLuaAnimGraphWidget::SaveAsset()
{
    if (AssetPath.empty())
    {
        return false;
    }

    RegenerateLuaSource();

    FAssetMetaData MetaData;
    MetaData.PayloadVersion = 4;
    MetaData.ClassName = UAnimLuaProgramAsset::StaticClass()->ClassName;
    MetaData.DisplayName = GetFileNameFromPath(AssetPath);

    FAnimLuaProgramAssetPayload Payload;
    Payload.Graph = Graph;
    Payload.GeneratedLuaSource = GeneratedLuaSource;

    const bool bSaved = FAssetFile::Save(AssetPath, MetaData, [&](FArchive& Ar)
                                         {
        Payload.Serialize(Ar, MetaData.PayloadVersion);
        return true; });

    if (bSaved)
    {
        bDirty = false;

        if (EditorEngine)
        {
            EditorEngine->GetNotificationService().Info("Saved Lua Anim Graph asset.");
        }
    }
    else if (EditorEngine)
    {
        EditorEngine->GetNotificationService().Error("Failed to save Lua Anim Graph asset.");
    }

    return bSaved;
}

void FEditorLuaAnimGraphWidget::HandleShortcuts()
{
    const ImGuiIO& IO = ImGui::GetIO();
    const bool bInGraphEditorContext =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    if (!bInGraphEditorContext || !IO.KeyCtrl)
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

void FEditorLuaAnimGraphWidget::DrawContent(float DeltaTime, bool bDetachedWindow)
{
    HandleShortcuts();

    DrawToolbar(bDetachedWindow);
    ImGui::Separator();

    if (!LastError.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", LastError.c_str());
        ImGui::Separator();
    }

    const ImVec2 LayoutSize = ImGui::GetContentRegionAvail();
    if (ImGui::BeginTable(
            "##LuaAnimGraphEditorLayout",
            2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp,
            LayoutSize))
    {
        const float AvailableWidth = ImGui::GetContentRegionAvail().x;
        const float LuaPanelWidth = std::clamp(AvailableWidth * 0.34f, 300.0f, 430.0f);
        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Lua", ImGuiTableColumnFlags_WidthFixed, LuaPanelWidth);

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        DrawGraphCanvas(DeltaTime);

        ImGui::TableSetColumnIndex(1);
        DrawLuaSourcePanel();

        ImGui::EndTable();
    }
}

void FEditorLuaAnimGraphWidget::DrawToolbar(bool bDetachedWindow)
{
    const float AvailableWidth = ImGui::GetContentRegionAvail().x;
    const float ActionWidth =
        ImGui::CalcTextSize("Regenerate Lua").x +
        ImGui::CalcTextSize("Add State").x +
        ImGui::CalcTextSize("Save").x +
        ImGui::GetStyle().FramePadding.x * 6.0f +
        ImGui::GetStyle().ItemSpacing.x * 2.0f;

    if (ImGui::BeginTable(
            "##LuaAnimGraphToolbarTop",
            2,
            ImGuiTableFlags_SizingStretchProp,
            ImVec2(AvailableWidth, 0.0f)))
    {
        ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, ActionWidth);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (bDetachedWindow)
        {
            if (ImGui::Button("Dock"))
            {
                bDockRequested = true;
            }
            ImGui::SameLine(0.0f, 10.0f);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Lua Anim Graph");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", AssetPath.empty() ? "<none>" : AssetPath.c_str());
        if (bDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f), "*");
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("Regenerate Lua"))
        {
            RegenerateLuaSource();
            bDirty = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Add State"))
        {
            FLuaAnimStateNode& State = Graph.AddState();
            if (NodeEditorContext)
            {
                ed::SetCurrentEditor(NodeEditorContext);
                ed::ClearSelection();
                ed::SetCurrentEditor(nullptr);
            }
            SelectedStateId = State.GetStateId();
            SelectedTransitionId = 0;
            bInitializedNodePositions = false;
            MarkGraphEdited();
        }

        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            SaveAsset();
        }

        ImGui::EndTable();
    }

    char PreviewMeshBuffer[512];
    std::strncpy(PreviewMeshBuffer, Graph.PreviewSkeletalMeshPath.c_str(), sizeof(PreviewMeshBuffer) - 1);
    PreviewMeshBuffer[sizeof(PreviewMeshBuffer) - 1] = '\0';
    if (ImGui::BeginTable(
            "##LuaAnimGraphToolbarPreview",
            2,
            ImGuiTableFlags_SizingStretchProp,
            ImVec2(AvailableWidth, 0.0f)))
    {
        ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed, 320.0f);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        DrawInitialStateCombo();

        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("Preview Mesh", PreviewMeshBuffer, sizeof(PreviewMeshBuffer)))
        {
            Graph.PreviewSkeletalMeshPath = PreviewMeshBuffer;
            MarkGraphEdited();
        }

        ImGui::EndTable();
    }
}

void FEditorLuaAnimGraphWidget::DrawGraphCanvas(float DeltaTime)
{
    if (!NodeEditorContext)
    {
        return;
    }

    ed::SetCurrentEditor(NodeEditorContext);

    const ImVec2 CanvasSize = ImGui::GetContentRegionAvail();
    const ImVec2 CanvasScreenPos = ImGui::GetCursorScreenPos();
    const ImGuiID OwnerViewportId = ImGui::GetWindowViewport() ? ImGui::GetWindowViewport()->ID : 0;
    ed::Begin("LuaAnimGraphCanvas", CanvasSize);

    if (!bInitializedNodePositions)
    {
        for (const auto& Pair : Graph.States)
        {
            const FLuaAnimStateNode& State = Pair.second;
            ed::SetNodePosition(
                ed::NodeId(State.GetStateId()),
                ImVec2(State.GetEditorPosX(), State.GetEditorPosY()));
        }

        bInitializedNodePositions = true;
    }

    for (auto& Pair : Graph.States)
    {
        FLuaAnimStateNode& State = Pair.second;
        ed::BeginNode(ed::NodeId(State.GetStateId()));
        ImGui::PushID(State.GetStateId());

        if (State.DrawNode(220.0f))
        {
            MarkGraphEdited();
        }

        ImGui::PopID();
        ed::EndNode();
    }

    for (auto& Pair : Graph.States)
    {
        FLuaAnimStateNode& State = Pair.second;
        const ImVec2 Pos = ed::GetNodePosition(ed::NodeId(State.GetStateId()));

        if (State.GetEditorPosX() != Pos.x || State.GetEditorPosY() != Pos.y)
        {
            State.SetEditorPosition(Pos.x, Pos.y);
            MarkGraphEdited();
        }
    }

    for (const auto& Pair : Graph.Transitions)
    {
        const FLuaAnimTransitionLink& Transition = Pair.second;
        const FLuaAnimStateNode* FromState = Graph.FindState(Transition.GetFromStateId());
        const FLuaAnimStateNode* ToState = Graph.FindState(Transition.GetToStateId());
        if (!FromState || !ToState)
        {
            continue;
        }

        Transition.DrawLink(*FromState, *ToState, Transition.GetTransitionId() == SelectedTransitionId);
    }

    const bool bDeletedItemThisFrame = HandleGraphCreateDelete();

    const int SelectedObjectCount = bDeletedItemThisFrame ? 0 : ed::GetSelectedObjectCount();
    if (SelectedObjectCount > 0)
    {
        int32 NewSelectedStateId = 0;
        int32 NewSelectedTransitionId = 0;

        ed::NodeId SelectedNodes[1];
        if (ed::GetSelectedNodes(SelectedNodes, 1) > 0)
        {
            const int32 StateId = static_cast<int32>(SelectedNodes[0].Get());
            if (Graph.FindState(StateId))
            {
                NewSelectedStateId = StateId;
            }
        }

        ed::LinkId SelectedLinks[1];
        if (ed::GetSelectedLinks(SelectedLinks, 1) > 0)
        {
            const int32 TransitionId = static_cast<int32>(SelectedLinks[0].Get());
            if (Graph.FindTransition(TransitionId))
            {
                NewSelectedTransitionId = TransitionId;
                NewSelectedStateId = 0;
            }
        }

        SelectedStateId = NewSelectedStateId;
        SelectedTransitionId = NewSelectedTransitionId;
    }
    else
    {
        SelectedStateId = 0;
        SelectedTransitionId = 0;
    }

    FlowSelectedNodeTransitions();

    ed::End();

    HandleCanvasAssetDrop(CanvasScreenPos, CanvasSize);

    ed::SetCurrentEditor(nullptr);

    DrawTransitionDetailsOverlay(CanvasScreenPos, CanvasSize, OwnerViewportId);
    PreviewOverlayWidget.DrawOverlay(
        DeltaTime,
        Graph,
        SelectedStateId,
        SelectedTransitionId,
        CanvasScreenPos,
        CanvasSize,
        OwnerViewportId);
}

bool FEditorLuaAnimGraphWidget::HandleGraphCreateDelete()
{
    bool bDeletedItem = false;

    if (ed::BeginCreate())
    {
        ed::PinId StartPinId;
        ed::PinId EndPinId;

        if (ed::QueryNewLink(&StartPinId, &EndPinId))
        {
            FLuaAnimResolvedPin StartPin;
            FLuaAnimResolvedPin EndPin;
            const bool bResolvedStart =
                Graph.ResolvePin(static_cast<int32>(StartPinId.Get()), StartPin);
            const bool bResolvedEnd =
                Graph.ResolvePin(static_cast<int32>(EndPinId.Get()), EndPin);

            FLuaAnimStateNode* FromState = nullptr;
            FLuaAnimStateNode* ToState = nullptr;

            if (bResolvedStart && bResolvedEnd)
            {
                if (StartPin.Role == ELuaAnimGraphPinRole::Output &&
                    EndPin.Role == ELuaAnimGraphPinRole::Input)
                {
                    FromState = StartPin.State;
                    ToState = EndPin.State;
                }
                else if (StartPin.Role == ELuaAnimGraphPinRole::Input &&
                         EndPin.Role == ELuaAnimGraphPinRole::Output)
                {
                    FromState = EndPin.State;
                    ToState = StartPin.State;
                }
            }

            const int32 FromStateId = FromState ? FromState->GetStateId() : 0;
            const int32 ToStateId = ToState ? ToState->GetStateId() : 0;

            if (Graph.CanCreateTransition(FromStateId, ToStateId))
            {
                if (ed::AcceptNewItem())
                {
                    FLuaAnimTransitionLink* Transition =
                        Graph.AddTransition(FromStateId, ToStateId);

                    SelectedStateId = 0;
                    SelectedTransitionId = Transition ? Transition->GetTransitionId() : 0;

                    MarkGraphEdited();
                }
            }
            else
            {
                ed::RejectNewItem();
            }
        }
    }

    ed::EndCreate();

    TArray<int32> DeletedStateIds;
    TArray<int32> DeletedTransitionIds;

    if (ed::BeginDelete())
    {
        ed::NodeId DeletedNodeId;

        while (ed::QueryDeletedNode(&DeletedNodeId))
        {
            if (ed::AcceptDeletedItem())
            {
                DeletedStateIds.push_back(static_cast<int32>(DeletedNodeId.Get()));
            }
        }

        ed::LinkId DeletedLinkId;

        while (ed::QueryDeletedLink(&DeletedLinkId))
        {
            if (ed::AcceptDeletedItem())
            {
                DeletedTransitionIds.push_back(static_cast<int32>(DeletedLinkId.Get()));
            }
        }
    }

    ed::EndDelete();

    const ImGuiIO& IO = ImGui::GetIO();
    if (DeletedStateIds.empty() &&
        DeletedTransitionIds.empty() &&
        !IO.WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        ed::NodeId SelectedNodes[1];
        if (ed::GetSelectedNodes(SelectedNodes, 1) > 0)
        {
            DeletedStateIds.push_back(static_cast<int32>(SelectedNodes[0].Get()));
        }

        ed::LinkId SelectedLinks[1];
        if (ed::GetSelectedLinks(SelectedLinks, 1) > 0)
        {
            DeletedTransitionIds.push_back(static_cast<int32>(SelectedLinks[0].Get()));
        }
    }

    bool bGraphChanged = false;

    for (int32 TransitionId : DeletedTransitionIds)
    {
        if (Graph.DeleteTransition(TransitionId))
        {
            bGraphChanged = true;
        }
    }

    for (int32 StateId : DeletedStateIds)
    {
        if (Graph.DeleteState(StateId))
        {
            bGraphChanged = true;
            bInitializedNodePositions = false;
        }
    }

    if (bGraphChanged)
    {
        SelectedStateId = 0;
        SelectedTransitionId = 0;

        MarkGraphEdited();
        bDeletedItem = true;
    }

    return bDeletedItem;
}

void FEditorLuaAnimGraphWidget::DrawTransitionDetailsOverlay(
    const ImVec2& CanvasScreenPos,
    const ImVec2& CanvasSize,
    ImGuiID OwnerViewportId)
{
    FLuaAnimTransitionLink* Transition = Graph.FindTransition(SelectedTransitionId);
    if (SelectedTransitionId == 0 || !Transition)
    {
        return;
    }

    const float OverlayMargin = 12.0f;

    const ImVec2 OverlaySize(
        std::min(336.0f, std::max(0.0f, CanvasSize.x - OverlayMargin * 2.0f)),
        std::min(560.0f, std::max(0.0f, CanvasSize.y - OverlayMargin * 2.0f)));

    const ImVec2 OverlayPos(
        CanvasScreenPos.x + CanvasSize.x - OverlaySize.x - OverlayMargin,
        CanvasScreenPos.y + OverlayMargin);

    if (OwnerViewportId != 0)
    {
        ImGui::SetNextWindowViewport(OwnerViewportId);
    }
    else if (ImGuiViewport* Viewport = ImGui::GetWindowViewport())
    {
        ImGui::SetNextWindowViewport(Viewport->ID);
    }
    ImGui::SetNextWindowPos(OverlayPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(OverlaySize, ImGuiCond_Always);

    const ImGuiWindowFlags Flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_AlwaysVerticalScrollbar;

    if (ImGui::Begin("##LuaAnimGraphTransitionDetailsOverlay", nullptr, Flags))
    {
        ImGui::TextUnformatted("Details");
        ImGui::Separator();

        const FLuaAnimStateNode* FromState =
            Graph.FindState(Transition->GetFromStateId());

        const FLuaAnimStateNode* ToState =
            Graph.FindState(Transition->GetToStateId());

        if (TransitionDetailsWidget.Draw(*Transition, FromState, ToState))
        {
            MarkGraphEdited();
        }
    }

    ImGui::End();
}
void FEditorLuaAnimGraphWidget::DrawLuaSourcePanel()
{
    ImGui::TextUnformatted("Generated Lua");
    ImGui::Separator();

    ImGui::BeginChild(
        "##GeneratedLuaSource",
        ImGui::GetContentRegionAvail(),
        true,
        ImGuiWindowFlags_HorizontalScrollbar);

    ImGui::TextUnformatted(GeneratedLuaSource.c_str());

    ImGui::EndChild();
}

void FEditorLuaAnimGraphWidget::RegenerateLuaSource()
{
    GeneratedLuaSource = Graph.GenerateLua();
}

void FEditorLuaAnimGraphWidget::MarkGraphEdited()
{
    RegenerateLuaSource();
    CommitUndoSnapshot(false);
    bDirty = true;
}

FEditorLuaAnimGraphWidget::FLuaAnimGraphUndoSnapshot FEditorLuaAnimGraphWidget::MakeUndoSnapshot() const
{
    FLuaAnimGraphUndoSnapshot Snapshot;
    Snapshot.Graph = Graph;
    Snapshot.GeneratedLuaSource = GeneratedLuaSource;
    Snapshot.SelectedStateId = SelectedStateId;
    Snapshot.SelectedTransitionId = SelectedTransitionId;
    return Snapshot;
}

FString FEditorLuaAnimGraphWidget::ComputeUndoFingerprint(const FLuaAnimGraphUndoSnapshot& Snapshot) const
{
    FString Fingerprint;
    const FLuaAnimGraph& SnapshotGraph = Snapshot.Graph;
    Fingerprint += SnapshotGraph.MachineName + "|";
    Fingerprint += std::to_string(SnapshotGraph.NextId) + "|";
    Fingerprint += std::to_string(SnapshotGraph.InitialStateId) + "|";
    Fingerprint += SnapshotGraph.PreviewSkeletalMeshPath + "|";
    Fingerprint += std::to_string(Snapshot.SelectedStateId) + "|";
    Fingerprint += std::to_string(Snapshot.SelectedTransitionId) + "|";

    TArray<int32> StateIds;
    StateIds.reserve(SnapshotGraph.States.size());
    for (const auto& Pair : SnapshotGraph.States)
    {
        StateIds.push_back(Pair.first);
    }
    std::sort(StateIds.begin(), StateIds.end());
    for (int32 StateId : StateIds)
    {
        const auto Found = SnapshotGraph.States.find(StateId);
        if (Found == SnapshotGraph.States.end())
        {
            continue;
        }
        const FLuaAnimStateNode& State = Found->second;
        Fingerprint += "S:";
        Fingerprint += std::to_string(State.StateId) + ",";
        Fingerprint += State.Name + ",";
        Fingerprint += State.AnimationPath + ",";
        Fingerprint += (State.bLoop ? "1," : "0,");
        Fingerprint += std::to_string(State.PlayRate) + ",";
        Fingerprint += std::to_string(State.EditorPosX) + ",";
        Fingerprint += std::to_string(State.EditorPosY) + "|";
    }

    TArray<int32> TransitionIds;
    TransitionIds.reserve(SnapshotGraph.Transitions.size());
    for (const auto& Pair : SnapshotGraph.Transitions)
    {
        TransitionIds.push_back(Pair.first);
    }
    std::sort(TransitionIds.begin(), TransitionIds.end());
    for (int32 TransitionId : TransitionIds)
    {
        const auto Found = SnapshotGraph.Transitions.find(TransitionId);
        if (Found == SnapshotGraph.Transitions.end())
        {
            continue;
        }
        const FLuaAnimTransitionLink& Transition = Found->second;
        Fingerprint += "T:";
        Fingerprint += std::to_string(Transition.TransitionId) + ",";
        Fingerprint += std::to_string(Transition.FromStateId) + ",";
        Fingerprint += std::to_string(Transition.ToStateId) + ",";
        Fingerprint += std::to_string(Transition.BlendTime) + ",";
        Fingerprint += (Transition.bResetTime ? "1," : "0,");
        Fingerprint += std::to_string(static_cast<int32>(Transition.BlendMode)) + ",";
        Fingerprint += std::to_string(static_cast<int32>(Transition.Join)) + ",";
        for (const FAnimCondition& Condition : Transition.Conditions)
        {
            Fingerprint += Condition.ContextName + ",";
            Fingerprint += std::to_string(static_cast<int32>(Condition.Operator)) + ",";
            Fingerprint += Condition.Value + ",";
            Fingerprint += (Condition.bUseDefaultValue ? "1," : "0,");
            Fingerprint += Condition.DefaultValue + ";";
        }
        Fingerprint += "|";
    }
    return Fingerprint;
}

void FEditorLuaAnimGraphWidget::RestoreUndoSnapshot(const FLuaAnimGraphUndoSnapshot& Snapshot)
{
    bRestoringUndoSnapshot = true;
    Graph = Snapshot.Graph;
    GeneratedLuaSource = Snapshot.GeneratedLuaSource;
    SelectedStateId = Snapshot.SelectedStateId;
    SelectedTransitionId = Snapshot.SelectedTransitionId;
    bInitializedNodePositions = false;
    bDirty = true;
    bRestoringUndoSnapshot = false;
}

void FEditorLuaAnimGraphWidget::ResetUndoHistory()
{
    UndoStack.clear();
    RedoStack.clear();
    LastUndoSnapshot = MakeUndoSnapshot();
    LastUndoFingerprint = ComputeUndoFingerprint(LastUndoSnapshot);
    bUndoBaselineValid = true;
}

void FEditorLuaAnimGraphWidget::CommitUndoSnapshot(bool bForce)
{
    if (bRestoringUndoSnapshot)
    {
        return;
    }
    if (!bUndoBaselineValid)
    {
        ResetUndoHistory();
        return;
    }

    const FLuaAnimGraphUndoSnapshot CurrentSnapshot = MakeUndoSnapshot();
    const FString CurrentFingerprint = ComputeUndoFingerprint(CurrentSnapshot);
    if (!bForce && CurrentFingerprint == LastUndoFingerprint)
    {
        return;
    }

    constexpr size_t MaxUndoSnapshots = 64;
    UndoStack.push_back(LastUndoSnapshot);
    while (UndoStack.size() > MaxUndoSnapshots)
    {
        UndoStack.erase(UndoStack.begin());
    }
    RedoStack.clear();
    LastUndoSnapshot = CurrentSnapshot;
    LastUndoFingerprint = CurrentFingerprint;
}

bool FEditorLuaAnimGraphWidget::CanUndoGraphEdit() const
{
    return bUndoBaselineValid && !UndoStack.empty();
}

bool FEditorLuaAnimGraphWidget::CanRedoGraphEdit() const
{
    return !RedoStack.empty();
}

void FEditorLuaAnimGraphWidget::UndoGraphEdit()
{
    if (!CanUndoGraphEdit())
    {
        return;
    }

    const FLuaAnimGraphUndoSnapshot CurrentSnapshot = MakeUndoSnapshot();
    const FLuaAnimGraphUndoSnapshot PreviousSnapshot = UndoStack.back();
    UndoStack.pop_back();
    RedoStack.push_back(CurrentSnapshot);
    RestoreUndoSnapshot(PreviousSnapshot);
    LastUndoSnapshot = PreviousSnapshot;
    LastUndoFingerprint = ComputeUndoFingerprint(LastUndoSnapshot);
    if (EditorEngine)
    {
        EditorEngine->GetNotificationService().Info("Undo Lua Anim Graph edit.");
    }
}

void FEditorLuaAnimGraphWidget::RedoGraphEdit()
{
    if (!CanRedoGraphEdit())
    {
        return;
    }

    const FLuaAnimGraphUndoSnapshot CurrentSnapshot = MakeUndoSnapshot();
    const FLuaAnimGraphUndoSnapshot NextSnapshot = RedoStack.back();
    RedoStack.pop_back();
    UndoStack.push_back(CurrentSnapshot);
    RestoreUndoSnapshot(NextSnapshot);
    LastUndoSnapshot = NextSnapshot;
    LastUndoFingerprint = ComputeUndoFingerprint(LastUndoSnapshot);
    if (EditorEngine)
    {
        EditorEngine->GetNotificationService().Info("Redo Lua Anim Graph edit.");
    }
}

void FEditorLuaAnimGraphWidget::DrawInitialStateCombo()
{
    const FLuaAnimStateNode* InitialState = Graph.FindState(Graph.GetInitialStateId());
    const char* Preview = InitialState ? InitialState->GetName().c_str() : "<none>";
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Entry");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##EntryState", Preview))
    {
        for (const auto& Pair : Graph.States)
        {
            const FLuaAnimStateNode& State = Pair.second;
            const bool bSelected = Graph.GetInitialStateId() == State.GetStateId();
            if (ImGui::Selectable(State.GetName().c_str(), bSelected))
            {
                if (Graph.GetInitialStateId() != State.GetStateId())
                {
                    Graph.SetInitialState(State.GetStateId());
                    MarkGraphEdited();
                }
            }
            if (bSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void FEditorLuaAnimGraphWidget::FlowSelectedNodeTransitions()
{
    if (SelectedStateId == 0 || SelectedTransitionId != 0)
    {
        return;
    }

    for (const auto& Pair : Graph.Transitions)
    {
        const FLuaAnimTransitionLink& Transition = Pair.second;
        if (Transition.GetTransitionId() != 0 &&
            (Transition.GetFromStateId() == SelectedStateId || Transition.GetToStateId() == SelectedStateId))
        {
            ed::Flow(ed::LinkId(Transition.GetTransitionId()), ed::FlowDirection::Forward);
        }
    }
}

void FEditorLuaAnimGraphWidget::HandleCanvasAssetDrop(
    const ImVec2& CanvasScreenPos,
    const ImVec2& CanvasSize)
{
    const ImGuiPayload* ActivePayload = ImGui::GetDragDropPayload();
    if (!ActivePayload)
    {
        return;
    }

    const ImVec2 MousePos = ImGui::GetIO().MousePos;
    if (!IsScreenPointInsideRect(MousePos, CanvasScreenPos, CanvasSize))
    {
        return;
    }

    const ImGuiWindowFlags Flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::SetNextWindowPos(CanvasScreenPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(CanvasSize, ImGuiCond_Always);

    if (ImGui::Begin("##LuaAnimGraphCanvasDropTarget", nullptr, Flags))
    {
        ImGui::InvisibleButton("##LuaAnimGraphCanvasDropTargetButton", CanvasSize);

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("AnimSequenceContentItem"))
            {
                const char* PayloadPath = static_cast<const char*>(Payload->Data);
                if (PayloadPath && Payload->DataSize > 0)
                {
                    TryCreateStateFromDroppedAnimSequence(PayloadPath, MousePos);
                }
            }

            ImGui::EndDragDropTarget();
        }
    }

    ImGui::End();
}

bool FEditorLuaAnimGraphWidget::TryCreateStateFromDroppedAnimSequence(
    const FString& AssetPath,
    const ImVec2& DropScreenPos)
{
    FString NormalizedAssetPath = FPaths::Normalize(AssetPath);

    FAssetMetaData MetaData;
    if (!FAssetFile::LoadMetadataOnly(NormalizedAssetPath, MetaData))
    {
        if (EditorEngine)
        {
            EditorEngine->GetNotificationService().Error("Failed to read dropped animation asset metadata.");
        }
        return false;
    }

    if (MetaData.ClassName != "UAnimSequence")
    {
        if (EditorEngine)
        {
            EditorEngine->GetNotificationService().Error("Dropped asset is not UAnimSequence.");
        }
        return false;
    }

    ImVec2 CanvasPos = DropScreenPos;

    if (NodeEditorContext)
    {
        ed::SetCurrentEditor(NodeEditorContext);
        CanvasPos = ed::ScreenToCanvas(DropScreenPos);
    }

    const FString StateName = GetFileStemFromPath(NormalizedAssetPath);

    FLuaAnimStateNode& State = Graph.AddState(
        StateName,
        NormalizedAssetPath,
        CanvasPos.x,
        CanvasPos.y);

    if (NodeEditorContext)
    {
        ed::SetNodePosition(
            ed::NodeId(State.GetStateId()),
            CanvasPos);

        ed::ClearSelection();
        ed::SelectNode(ed::NodeId(State.GetStateId()));
    }

    SelectedStateId = State.GetStateId();
    SelectedTransitionId = 0;

    bInitializedNodePositions = false;

    MarkGraphEdited();

    return true;
}
