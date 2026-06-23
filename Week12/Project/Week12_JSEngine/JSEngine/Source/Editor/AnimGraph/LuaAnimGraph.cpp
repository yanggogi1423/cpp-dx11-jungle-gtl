#include "Animation/LuaAnimGraph.h"

#include "ImGui/imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace ax
{
namespace NodeEditor
{
    struct NodeId
    {
        explicit NodeId(int InValue = 0) : Value(InValue) {}
        int Value = 0;
    };

    struct PinId
    {
        explicit PinId(int InValue = 0) : Value(InValue) {}
        int Value = 0;
    };

    struct LinkId
    {
        explicit LinkId(int InValue = 0) : Value(InValue) {}
        int Value = 0;
    };

    enum class PinKind
    {
        Input,
        Output,
    };

    enum class FlowDirection
    {
        Forward,
    };

    inline void BeginPin(PinId, PinKind) {}
    inline void EndPin() {}
    inline ImVec2 GetNodePosition(NodeId) { return ImVec2(0.0f, 0.0f); }
    inline ImVec2 GetNodeSize(NodeId) { return ImVec2(0.0f, 0.0f); }
    inline void Link(LinkId, PinId, PinId) {}
    inline void Flow(LinkId, FlowDirection) {}
}
}

namespace ed = ax::NodeEditor;

namespace
{
FString TrimStateName(const FString& Name)
{
    const size_t First = Name.find_first_not_of(" \t\r\n");
    if (First == FString::npos)
    {
        return "";
    }

    const size_t Last = Name.find_last_not_of(" \t\r\n");
    return Name.substr(First, Last - First + 1);
}

bool DrawFStringInput(const char* Label, FString& Value)
{
    std::array<char, 512> Buffer{};
    const size_t CopySize = std::min(Value.size(), Buffer.size() - 1);
    if (CopySize > 0)
    {
        std::memcpy(Buffer.data(), Value.data(), CopySize);
    }

    if (ImGui::InputText(Label, Buffer.data(), Buffer.size()))
    {
        Value = Buffer.data();
        return true;
    }

    return false;
}

void DrawNodeSeparator(float Width)
{
    ImGui::Spacing();

    ImVec2 P = ImGui::GetCursorScreenPos();
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const float Y = P.y + 0.5f;
    const ImU32 Color = ImGui::GetColorU32(ImGuiCol_Separator);

    DrawList->AddLine(
        ImVec2(P.x, Y),
        ImVec2(P.x + Width, Y),
        Color,
        1.0f);

    ImGui::Dummy(ImVec2(Width, 1.0f));
    ImGui::Spacing();
}

const char* ToBlendModeLabel(EAnimBlendMode Mode)
{
    switch (Mode)
    {
    case EAnimBlendMode::Linear:
        return "Linear";
    case EAnimBlendMode::EaseIn:
        return "EaseIn";
    case EAnimBlendMode::EaseOut:
        return "EaseOut";
    case EAnimBlendMode::EaseInOut:
        return "EaseInOut";
    default:
        return "Linear";
    }
}

const char* ToCompareOpLabel(EAnimCompareOp Op)
{
    switch (Op)
    {
    case EAnimCompareOp::Equal:
        return "==";
    case EAnimCompareOp::NotEqual:
        return "~=";
    case EAnimCompareOp::Greater:
        return ">";
    case EAnimCompareOp::GreaterEqual:
        return ">=";
    case EAnimCompareOp::Less:
        return "<";
    case EAnimCompareOp::LessEqual:
        return "<=";
    default:
        return "==";
    }
}

const char* ToJoinLabel(EAnimConditionJoin Join)
{
    switch (Join)
    {
    case EAnimConditionJoin::And:
        return "And";
    case EAnimConditionJoin::Or:
        return "Or";
    default:
        return "And";
    }
}

bool DrawBlendModeCombo(const char* Label, EAnimBlendMode& Mode)
{
    bool bChanged = false;
    const EAnimBlendMode Values[] = {
        EAnimBlendMode::Linear,
        EAnimBlendMode::EaseIn,
        EAnimBlendMode::EaseOut,
        EAnimBlendMode::EaseInOut,
    };

    if (ImGui::BeginCombo(Label, ToBlendModeLabel(Mode)))
    {
        for (EAnimBlendMode Value : Values)
        {
            const bool bSelected = Mode == Value;
            if (ImGui::Selectable(ToBlendModeLabel(Value), bSelected))
            {
                if (Mode != Value)
                {
                    Mode = Value;
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

    return bChanged;
}

bool DrawCompareOpCombo(const char* Label, EAnimCompareOp& Op)
{
    bool bChanged = false;
    const EAnimCompareOp Values[] = {
        EAnimCompareOp::Equal,
        EAnimCompareOp::NotEqual,
        EAnimCompareOp::Greater,
        EAnimCompareOp::GreaterEqual,
        EAnimCompareOp::Less,
        EAnimCompareOp::LessEqual,
    };

    if (ImGui::BeginCombo(Label, ToCompareOpLabel(Op)))
    {
        for (EAnimCompareOp Value : Values)
        {
            const bool bSelected = Op == Value;
            if (ImGui::Selectable(ToCompareOpLabel(Value), bSelected))
            {
                if (Op != Value)
                {
                    Op = Value;
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

    return bChanged;
}

bool DrawJoinCombo(const char* Label, EAnimConditionJoin& Join)
{
    bool bChanged = false;
    const EAnimConditionJoin Values[] = {
        EAnimConditionJoin::And,
        EAnimConditionJoin::Or,
    };

    if (ImGui::BeginCombo(Label, ToJoinLabel(Join)))
    {
        for (EAnimConditionJoin Value : Values)
        {
            const bool bSelected = Join == Value;
            if (ImGui::Selectable(ToJoinLabel(Value), bSelected))
            {
                if (Join != Value)
                {
                    Join = Value;
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

    return bChanged;
}

FString MakeBinaryConditionText(EAnimCompareOp Op)
{
    switch (Op)
    {
    case EAnimCompareOp::Equal:
        return "==";
    case EAnimCompareOp::NotEqual:
        return "~=";
    case EAnimCompareOp::Greater:
        return ">";
    case EAnimCompareOp::GreaterEqual:
        return ">=";
    case EAnimCompareOp::Less:
        return "<";
    case EAnimCompareOp::LessEqual:
        return "<=";
    default:
        return "==";
    }
}

void BumpNextIdForState(FLuaAnimGraph& Graph, const FLuaAnimStateNode& State)
{
    Graph.NextId = std::max(Graph.NextId, State.StateId + 1);
    for (int32 SideIndex = 0; SideIndex < static_cast<int32>(ELuaAnimGraphNodeSide::Count); ++SideIndex)
    {
        Graph.NextId = std::max(Graph.NextId, State.InputPinIds[SideIndex] + 1);
        Graph.NextId = std::max(Graph.NextId, State.OutputPinIds[SideIndex] + 1);
    }
}

void BumpNextIdForTransition(FLuaAnimGraph& Graph, const FLuaAnimTransitionLink& Transition)
{
    Graph.NextId = std::max(Graph.NextId, Transition.TransitionId + 1);
}

int32 GetSideIndex(ELuaAnimGraphNodeSide Side)
{
    const int32 Index = static_cast<int32>(Side);
    if (Index < 0 || Index >= static_cast<int32>(ELuaAnimGraphNodeSide::Count))
    {
        return 0;
    }
    return Index;
}

ELuaAnimGraphNodeSide GetOppositeSide(ELuaAnimGraphNodeSide Side)
{
    switch (Side)
    {
    case ELuaAnimGraphNodeSide::Left:
        return ELuaAnimGraphNodeSide::Right;
    case ELuaAnimGraphNodeSide::Right:
        return ELuaAnimGraphNodeSide::Left;
    case ELuaAnimGraphNodeSide::Top:
        return ELuaAnimGraphNodeSide::Bottom;
    case ELuaAnimGraphNodeSide::Bottom:
        return ELuaAnimGraphNodeSide::Top;
    default:
        return ELuaAnimGraphNodeSide::Left;
    }
}

ELuaAnimGraphNodeSide ChooseOutputSide(const ImVec2& FromCenter, const ImVec2& ToCenter)
{
    const float DX = ToCenter.x - FromCenter.x;
    const float DY = ToCenter.y - FromCenter.y;

    if (std::abs(DX) > std::abs(DY))
    {
        return DX >= 0.0f ? ELuaAnimGraphNodeSide::Right : ELuaAnimGraphNodeSide::Left;
    }

    return DY >= 0.0f ? ELuaAnimGraphNodeSide::Bottom : ELuaAnimGraphNodeSide::Top;
}

void DrawPinDirectionArrow(
    ImDrawList* DrawList,
    const ImVec2& RectMin,
    const ImVec2& RectMax,
    ed::PinKind PinKind,
    ELuaAnimGraphNodeSide Side)
{
    if (!DrawList)
    {
        return;
    }

    const ImVec2 Center(
        (RectMin.x + RectMax.x) * 0.5f,
        (RectMin.y + RectMax.y) * 0.5f);
    const float Length = 8.0f;
    const float Width = 5.0f;
    const ImU32 Color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const bool bOutput = PinKind == ed::PinKind::Output;

    switch (Side)
    {
    case ELuaAnimGraphNodeSide::Left:
        DrawList->AddTriangleFilled(
            ImVec2(Center.x + (bOutput ? -Length : Length), Center.y),
            ImVec2(Center.x + (bOutput ? Width : -Width), Center.y - Width),
            ImVec2(Center.x + (bOutput ? Width : -Width), Center.y + Width),
            Color);
        break;
    case ELuaAnimGraphNodeSide::Right:
        DrawList->AddTriangleFilled(
            ImVec2(Center.x + (bOutput ? Length : -Length), Center.y),
            ImVec2(Center.x + (bOutput ? -Width : Width), Center.y + Width),
            ImVec2(Center.x + (bOutput ? -Width : Width), Center.y - Width),
            Color);
        break;
    case ELuaAnimGraphNodeSide::Top:
        DrawList->AddTriangleFilled(
            ImVec2(Center.x, Center.y + (bOutput ? -Length : Length)),
            ImVec2(Center.x + Width, Center.y + (bOutput ? Width : -Width)),
            ImVec2(Center.x - Width, Center.y + (bOutput ? Width : -Width)),
            Color);
        break;
    case ELuaAnimGraphNodeSide::Bottom:
        DrawList->AddTriangleFilled(
            ImVec2(Center.x, Center.y + (bOutput ? Length : -Length)),
            ImVec2(Center.x - Width, Center.y + (bOutput ? -Width : Width)),
            ImVec2(Center.x + Width, Center.y + (bOutput ? -Width : Width)),
            Color);
        break;
    default:
        break;
    }
}

void DrawPinHitArea(int32 PinId, ed::PinKind PinKind, const ImVec2& Size, ELuaAnimGraphNodeSide Side)
{
    ed::BeginPin(ed::PinId(PinId), PinKind);
    const ImVec2 RectMin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(Size);
    const ImVec2 RectMax(RectMin.x + Size.x, RectMin.y + Size.y);
    DrawPinDirectionArrow(ImGui::GetWindowDrawList(), RectMin, RectMax, PinKind, Side);
    ed::EndPin();
}

void DrawHorizontalSidePins(
    int32 LeftPinId,
    ed::PinKind LeftKind,
    int32 RightPinId,
    ed::PinKind RightKind,
    ELuaAnimGraphNodeSide Side,
    float NodeWidth)
{
    constexpr float PinHeight = 16.0f;
    const float HalfWidth = std::max(1.0f, NodeWidth * 0.5f);

    ImGui::BeginGroup();

    DrawPinHitArea(LeftPinId, LeftKind, ImVec2(HalfWidth, PinHeight), Side);
    ImGui::SameLine(0.0f, 0.0f);
    DrawPinHitArea(RightPinId, RightKind, ImVec2(HalfWidth, PinHeight), Side);

    ImGui::EndGroup();
}

void DrawVerticalSidePins(
    int32 TopPinId,
    ed::PinKind TopKind,
    int32 BottomPinId,
    ed::PinKind BottomKind,
    ELuaAnimGraphNodeSide Side,
    float SideStripWidth,
    float BodyHeight)
{
    const float HalfHeight = std::max(1.0f, BodyHeight * 0.5f);

    ImGui::BeginGroup();
    DrawPinHitArea(TopPinId, TopKind, ImVec2(SideStripWidth, HalfHeight), Side);
    DrawPinHitArea(BottomPinId, BottomKind, ImVec2(SideStripWidth, HalfHeight), Side);
    ImGui::EndGroup();
}

ImVec2 GetNodeCenter(int32 StateId)
{
    const ImVec2 Pos = ed::GetNodePosition(ed::NodeId(StateId));
    const ImVec2 Size = ed::GetNodeSize(ed::NodeId(StateId));
    return ImVec2(Pos.x + Size.x * 0.5f, Pos.y + Size.y * 0.5f);
}

void AssignStatePinIds(FLuaAnimGraph& Graph, FLuaAnimStateNode& State)
{
    for (int32 SideIndex = 0; SideIndex < static_cast<int32>(ELuaAnimGraphNodeSide::Count); ++SideIndex)
    {
        if (State.InputPinIds[SideIndex] <= 0)
        {
            State.InputPinIds[SideIndex] = Graph.AllocId();
        }

        if (State.OutputPinIds[SideIndex] <= 0)
        {
            State.OutputPinIds[SideIndex] = Graph.AllocId();
        }
    }
}
} // namespace

void FLuaAnimStateNode::SetEditorPosition(float X, float Y)
{
    EditorPosX = X;
    EditorPosY = Y;
}

int32 FLuaAnimStateNode::GetInputPinId(ELuaAnimGraphNodeSide Side) const
{
    return InputPinIds[GetSideIndex(Side)];
}

int32 FLuaAnimStateNode::GetOutputPinId(ELuaAnimGraphNodeSide Side) const
{
    return OutputPinIds[GetSideIndex(Side)];
}

bool FLuaAnimStateNode::DrawNode(float NodeWidth)
{
    bool bChanged = false;

    constexpr float SideStripWidth = 28.0f;
    constexpr float ColumnGap = 8.0f;
    constexpr float BodyHeight = 150.0f;

    const float BodyWidth =
        std::max(80.0f, NodeWidth - SideStripWidth * 2.0f - ColumnGap * 2.0f);

    ImGui::BeginGroup();

    DrawHorizontalSidePins(
        GetInputPinId(ELuaAnimGraphNodeSide::Top),
        ed::PinKind::Input,
        GetOutputPinId(ELuaAnimGraphNodeSide::Top),
        ed::PinKind::Output,
        ELuaAnimGraphNodeSide::Top,
        NodeWidth);

    ImGui::Spacing();

    ImGui::BeginGroup();
    DrawVerticalSidePins(
        GetOutputPinId(ELuaAnimGraphNodeSide::Left),
        ed::PinKind::Output,
        GetInputPinId(ELuaAnimGraphNodeSide::Left),
        ed::PinKind::Input,
        ELuaAnimGraphNodeSide::Left,
        SideStripWidth,
        BodyHeight);
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, ColumnGap);

    ImGui::BeginGroup();
    ImGui::TextUnformatted(Name.c_str());
    DrawNodeSeparator(BodyWidth);

    ImGui::PushItemWidth(BodyWidth);

    FString EditedName = Name;
    ImGui::TextUnformatted("State Name");
    if (DrawFStringInput("##StateName", EditedName))
    {
        Name = TrimStateName(EditedName);
        if (Name.empty())
        {
            Name = "State";
        }
        bChanged = true;
    }

    ImGui::TextUnformatted("Animation Path");
    if (DrawFStringInput("##AnimationPath", AnimationPath))
    {
        bChanged = true;
    }

    if (ImGui::Checkbox("Loop", &bLoop))
    {
        bChanged = true;
    }

	ImGui::Text("Play Rate");
    if (ImGui::DragFloat("##PlayRate", &PlayRate, 0.01f, 0.01f, 10.0f))
    {
        PlayRate = std::max(0.01f, std::min(10.0f, PlayRate));
        bChanged = true;
    }

    ImGui::PopItemWidth();
    ImGui::EndGroup();

    ImGui::SameLine(0.0f, ColumnGap);

    ImGui::BeginGroup();
    DrawVerticalSidePins(
        GetInputPinId(ELuaAnimGraphNodeSide::Right),
        ed::PinKind::Input,
        GetOutputPinId(ELuaAnimGraphNodeSide::Right),
        ed::PinKind::Output,
        ELuaAnimGraphNodeSide::Right,
        SideStripWidth,
        BodyHeight);
    ImGui::EndGroup();

    ImGui::Spacing();

    DrawHorizontalSidePins(
        GetOutputPinId(ELuaAnimGraphNodeSide::Bottom),
        ed::PinKind::Output,
        GetInputPinId(ELuaAnimGraphNodeSide::Bottom),
        ed::PinKind::Input,
        ELuaAnimGraphNodeSide::Bottom,
        NodeWidth);

    ImGui::EndGroup();

    return bChanged;
}

void FLuaAnimStateNode::Serialize(FArchive& Ar, int32 PayloadVersion)
{
    Ar << StateId;
    Ar << Name;
    Ar << AnimationPath;
    Ar << bLoop;
    Ar << PlayRate;
    if (PayloadVersion >= 4)
    {
        for (int32 SideIndex = 0; SideIndex < static_cast<int32>(ELuaAnimGraphNodeSide::Count); ++SideIndex)
        {
            Ar << InputPinIds[SideIndex];
        }

        for (int32 SideIndex = 0; SideIndex < static_cast<int32>(ELuaAnimGraphNodeSide::Count); ++SideIndex)
        {
            Ar << OutputPinIds[SideIndex];
        }
    }
    else
    {
        int32 LegacyInputPinId = 0;
        int32 LegacyOutputPinId = 0;
        Ar << LegacyInputPinId;
        Ar << LegacyOutputPinId;
    }
    Ar << EditorPosX;
    Ar << EditorPosY;
}

void FLuaAnimTransitionLink::DrawLink(
    const FLuaAnimStateNode& FromState,
    const FLuaAnimStateNode& ToState,
    bool bSelected) const
{
    const ELuaAnimGraphNodeSide OutputSide =
        ChooseOutputSide(GetNodeCenter(FromState.GetStateId()), GetNodeCenter(ToState.GetStateId()));
    const ELuaAnimGraphNodeSide InputSide = GetOppositeSide(OutputSide);

    ed::Link(
        ed::LinkId(TransitionId),
        ed::PinId(FromState.GetOutputPinId(OutputSide)),
        ed::PinId(ToState.GetInputPinId(InputSide)));

    if (bSelected)
    {
        ed::Flow(ed::LinkId(TransitionId), ed::FlowDirection::Forward);
    }
}

void FLuaAnimTransitionLink::Serialize(FArchive& Ar)
{
    Ar << TransitionId;
    Ar << FromStateId;
    Ar << ToStateId;
    Ar << BlendTime;
    Ar << bResetTime;

    int32 BlendModeValue = static_cast<int32>(BlendMode);
    Ar << BlendModeValue;
    if (Ar.IsLoading())
    {
        BlendMode = static_cast<EAnimBlendMode>(BlendModeValue);
    }

    int32 JoinValue = static_cast<int32>(Join);
    Ar << JoinValue;
    if (Ar.IsLoading())
    {
        Join = static_cast<EAnimConditionJoin>(JoinValue);
    }

    Ar << Conditions;
}

int32 FLuaAnimGraph::AllocId()
{
    if (NextId <= 0)
    {
        NextId = 1;
    }
    return NextId++;
}

FLuaAnimStateNode* FLuaAnimGraph::FindState(int32 StateId)
{
    auto It = States.find(StateId);
    return It != States.end() ? &It->second : nullptr;
}

FLuaAnimStateNode& FLuaAnimGraph::AddState(
    const FString& InStateName,
    const FString& InAnimationPath,
    float InEditorPosX,
    float InEditorPosY)
{
    FLuaAnimStateNode State;
    State.StateId = AllocId();
    State.Name = MakeUniqueStateName(InStateName);
    State.AnimationPath = InAnimationPath;
    State.bLoop = true;
    State.PlayRate = 1.0f;

    for (int32 SideIndex = 0; SideIndex < static_cast<int32>(ELuaAnimGraphNodeSide::Count); ++SideIndex)
    {
        State.InputPinIds[SideIndex] = AllocId();
        State.OutputPinIds[SideIndex] = AllocId();
    }

    State.EditorPosX = InEditorPosX;
    State.EditorPosY = InEditorPosY;

    const int32 StateId = State.StateId;
    States[StateId] = State;

    if (InitialStateId == 0)
    {
        InitialStateId = StateId;
    }

    return States[StateId];
}

const FLuaAnimStateNode* FLuaAnimGraph::FindState(int32 StateId) const
{
    auto It = States.find(StateId);
    return It != States.end() ? &It->second : nullptr;
}

FLuaAnimTransitionLink* FLuaAnimGraph::FindTransition(int32 TransitionId)
{
    auto It = Transitions.find(TransitionId);
    return It != Transitions.end() ? &It->second : nullptr;
}

const FLuaAnimTransitionLink* FLuaAnimGraph::FindTransition(int32 TransitionId) const
{
    auto It = Transitions.find(TransitionId);
    return It != Transitions.end() ? &It->second : nullptr;
}

bool FLuaAnimGraph::ResolvePin(int32 PinId, FLuaAnimResolvedPin& OutPin)
{
    for (auto& Pair : States)
    {
        FLuaAnimStateNode& State = Pair.second;
        for (int32 SideIndex = 0; SideIndex < static_cast<int32>(ELuaAnimGraphNodeSide::Count); ++SideIndex)
        {
            const ELuaAnimGraphNodeSide Side = static_cast<ELuaAnimGraphNodeSide>(SideIndex);
            if (State.GetInputPinId(Side) == PinId)
            {
                OutPin.State = &State;
                OutPin.Side = Side;
                OutPin.Role = ELuaAnimGraphPinRole::Input;
                return true;
            }

            if (State.GetOutputPinId(Side) == PinId)
            {
                OutPin.State = &State;
                OutPin.Side = Side;
                OutPin.Role = ELuaAnimGraphPinRole::Output;
                return true;
            }
        }
    }

    OutPin = FLuaAnimResolvedPin();
    return false;
}

FLuaAnimStateNode& FLuaAnimGraph::AddState()
{
    const float Offset = static_cast<float>(States.size());

    return AddState(
        "NewState",
        "",
        120.0f + Offset * 40.0f,
        120.0f + Offset * 30.0f);
}

bool FLuaAnimGraph::DeleteState(int32 StateId)
{
    if (States.erase(StateId) == 0)
    {
        return false;
    }

    for (auto It = Transitions.begin(); It != Transitions.end();)
    {
        const FLuaAnimTransitionLink& Transition = It->second;
        if (Transition.FromStateId == StateId || Transition.ToStateId == StateId)
        {
            It = Transitions.erase(It);
        }
        else
        {
            ++It;
        }
    }

    if (InitialStateId == StateId)
    {
        InitialStateId = States.empty() ? 0 : States.begin()->first;
    }

    return true;
}

bool FLuaAnimGraph::CanCreateTransition(int32 FromStateId, int32 ToStateId) const
{
    if (FromStateId == 0 || ToStateId == 0 || FromStateId == ToStateId)
    {
        return false;
    }

    if (States.find(FromStateId) == States.end() ||
        States.find(ToStateId) == States.end())
    {
        return false;
    }

    for (const auto& Pair : Transitions)
    {
        const FLuaAnimTransitionLink& Transition = Pair.second;
        if (Transition.FromStateId == FromStateId && Transition.ToStateId == ToStateId)
        {
            return false;
        }
    }

    return true;
}

FLuaAnimTransitionLink* FLuaAnimGraph::AddTransition(int32 FromStateId, int32 ToStateId)
{
    if (!CanCreateTransition(FromStateId, ToStateId))
    {
        return nullptr;
    }

    FLuaAnimTransitionLink Transition;
    Transition.TransitionId = AllocId();
    Transition.FromStateId = FromStateId;
    Transition.ToStateId = ToStateId;

    const int32 TransitionId = Transition.TransitionId;
    Transitions[TransitionId] = Transition;
    return &Transitions[TransitionId];
}

bool FLuaAnimGraph::DeleteTransition(int32 TransitionId)
{
    return Transitions.erase(TransitionId) > 0;
}

void FLuaAnimGraph::SetInitialState(int32 StateId)
{
    if (StateId == 0 || States.find(StateId) != States.end())
    {
        InitialStateId = StateId;
    }
}

int32 FLuaAnimGraph::CountTransitionsFromState(int32 StateId) const
{
    int32 Count = 0;
    for (const auto& Pair : Transitions)
    {
        if (Pair.second.FromStateId == StateId)
        {
            ++Count;
        }
    }
    return Count;
}

FString FLuaAnimGraph::GenerateLua() const
{
    return FLuaAnimGraphCodeGenerator().Generate(*this);
}

void FLuaAnimGraph::Serialize(FArchive& Ar, int32 PayloadVersion)
{
    Ar << MachineName;
    Ar << NextId;
    Ar << InitialStateId;
    if (PayloadVersion >= 3)
    {
        Ar << PreviewSkeletalMeshPath;
    }
    else if (Ar.IsLoading())
    {
        PreviewSkeletalMeshPath.clear();
    }

    TArray<FLuaAnimStateNode> StateArray;
    TArray<FLuaAnimTransitionLink> TransitionArray;

    if (Ar.IsLoading() && NextId <= 0)
    {
        NextId = 1;
    }

    int32 StateCount = Ar.IsSaving() ? static_cast<int32>(States.size()) : 0;
    Ar.BeginArray("States", StateCount);

    if (Ar.IsSaving())
    {
        for (auto& Pair : States)
        {
            FLuaAnimStateNode State = Pair.second;
            State.Serialize(Ar, PayloadVersion);
        }
    }
    else
    {
        StateArray.resize(std::max(0, StateCount));
        for (FLuaAnimStateNode& State : StateArray)
        {
            State.Serialize(Ar, PayloadVersion);
            if (PayloadVersion < 4)
            {
                // Payload v3 and older stored one input/output pin per state.
                // Four-sided editor pins are regenerated from Graph.NextId.
                AssignStatePinIds(*this, State);
            }
        }
    }

    Ar.EndArray();

    if (Ar.IsSaving())
    {
        TransitionArray.reserve(Transitions.size());
        for (auto& Pair : Transitions)
        {
            TransitionArray.push_back(Pair.second);
        }
    }

    Ar << TransitionArray;

    if (Ar.IsLoading())
    {
        States.clear();
        Transitions.clear();

        for (FLuaAnimStateNode& State : StateArray)
        {
            if (State.StateId > 0)
            {
                AssignStatePinIds(*this, State);
                States[State.StateId] = State;
                BumpNextIdForState(*this, State);
            }
        }

        for (const FLuaAnimTransitionLink& Transition : TransitionArray)
        {
            if (Transition.TransitionId > 0)
            {
                Transitions[Transition.TransitionId] = Transition;
                BumpNextIdForTransition(*this, Transition);
            }
        }

        if (States.find(InitialStateId) == States.end())
        {
            InitialStateId = States.empty() ? 0 : States.begin()->first;
        }
    }
}

FString FLuaAnimGraph::MakeUniqueStateName(const FString& BaseName) const
{
    const FString TrimmedBaseName = TrimStateName(BaseName);
    const FString Base = TrimmedBaseName.empty() ? "NewState" : TrimmedBaseName;
    if (IsStateNameAvailable(Base))
    {
        return Base;
    }

    for (int32 Suffix = 1;; ++Suffix)
    {
        FString Candidate = Base;
        Candidate += std::to_string(Suffix);
        if (IsStateNameAvailable(Candidate))
        {
            return Candidate;
        }
    }
}

bool FLuaAnimGraph::IsStateNameAvailable(const FString& Name, const FLuaAnimStateNode* IgnoredState) const
{
    const FString TrimmedName = TrimStateName(Name);
    if (TrimmedName.empty())
    {
        return false;
    }

    for (const auto& Pair : States)
    {
        const FLuaAnimStateNode& State = Pair.second;
        if (&State != IgnoredState && State.Name == TrimmedName)
        {
            return false;
        }
    }

    return true;
}

bool FLuaAnimTransitionDetailsWidget::Draw(
    FLuaAnimTransitionLink& Transition,
    const FLuaAnimStateNode* FromState,
    const FLuaAnimStateNode* ToState)
{
    bool bChanged = false;

    ImGui::TextUnformatted("Transition");
    ImGui::Text("From: %s", FromState ? FromState->GetName().c_str() : "<unknown>");
    ImGui::Text("To: %s", ToState ? ToState->GetName().c_str() : "<unknown>");

    float BlendTime = Transition.GetBlendTime();
    if (ImGui::DragFloat("Blend Time", &BlendTime, 0.01f, 0.0f, 5.0f))
    {
        Transition.SetBlendTime(BlendTime);
        bChanged = true;
    }

    bool bResetTime = Transition.ShouldResetTime();
    if (ImGui::Checkbox("Reset Time", &bResetTime))
    {
        Transition.SetResetTime(bResetTime);
        bChanged = true;
    }

    EAnimBlendMode BlendMode = Transition.GetBlendMode();
    if (DrawBlendModeCombo("Blend Mode", BlendMode))
    {
        Transition.SetBlendMode(BlendMode);
        bChanged = true;
    }

    EAnimConditionJoin Join = Transition.GetJoin();
    if (DrawJoinCombo("Join", Join))
    {
        Transition.SetJoin(Join);
        bChanged = true;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Conditions");

    TArray<FAnimCondition>& Conditions = Transition.GetConditions();
    if (ImGui::Button("Add Condition"))
    {
        FAnimCondition Condition;
        Condition.ContextName = "Speed";
        Condition.Operator = EAnimCompareOp::Greater;
        Condition.Value = "0.0";
        Condition.bUseDefaultValue = true;
        Condition.DefaultValue = "0.0";
        Conditions.push_back(Condition);
        bChanged = true;
    }

    if (Conditions.empty())
    {
        ImGui::TextDisabled("No conditions. Generated Lua returns false.");
        return bChanged;
    }

    for (size_t Index = 0; Index < Conditions.size(); ++Index)
    {
        FAnimCondition& Condition = Conditions[Index];

        ImGui::PushID(static_cast<int>(Index));
        ImGui::Separator();
        ImGui::Text("Condition %d", static_cast<int>(Index + 1));
        ImGui::SameLine();
        if (ImGui::Button("Delete"))
        {
            Conditions.erase(Conditions.begin() + static_cast<ptrdiff_t>(Index));
            ImGui::PopID();
            bChanged = true;
            break;
        }

        if (DrawFStringInput("Context Name", Condition.ContextName))
        {
            bChanged = true;
        }

        if (DrawCompareOpCombo("Operator", Condition.Operator))
        {
            bChanged = true;
        }

        if (DrawFStringInput("Value", Condition.Value))
        {
            bChanged = true;
        }

        if (ImGui::Checkbox("Use Default Value", &Condition.bUseDefaultValue))
        {
            bChanged = true;
        }

        if (DrawFStringInput("Default Value", Condition.DefaultValue))
        {
            bChanged = true;
        }

        ImGui::PopID();
    }

	ImGui::Spacing();
    ImGui::Spacing();
	ImGui::TextDisabled("DeltaTime");
    ImGui::TextDisabled("IsInTransition");
    ImGui::TextDisabled("TransitionFrom");
    ImGui::TextDisabled("TransitionTo");
    ImGui::TextDisabled("TransitionAlpha");
    ImGui::TextDisabled("StateTime");
    ImGui::TextDisabled("StateNormalizedTime");

    return bChanged;
}

FArchive& operator<<(FArchive& Ar, FAnimCondition& Condition)
{
    Ar << Condition.ContextName;

    int32 OperatorValue = static_cast<int32>(Condition.Operator);
    Ar << OperatorValue;
    if (Ar.IsLoading())
    {
        Condition.Operator = static_cast<EAnimCompareOp>(OperatorValue);
    }

    Ar << Condition.Value;
    Ar << Condition.bUseDefaultValue;
    Ar << Condition.DefaultValue;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FLuaAnimStateNode& State)
{
    State.Serialize(Ar);
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FLuaAnimTransitionLink& Transition)
{
    Transition.Serialize(Ar);
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FLuaAnimGraph& Graph)
{
    Graph.Serialize(Ar);
    return Ar;
}

FString FLuaAnimGraphCodeGenerator::Generate(const FLuaAnimGraph& Graph) const
{
    FString Out;
    const FString MachineName = Graph.MachineName.empty() ? "Machine" : Graph.MachineName;
    const FLuaAnimStateNode* InitialState = ResolveInitialState(Graph);

    Out += "local ";
    Out += MachineName;
    Out += " = {}\n\n";

    Out += "function ";
    Out += MachineName;
    Out += ".new()\n";
    Out += "    local self = {}\n\n";

    Out += "    self.currentState = \"";
    Out += EscapeLuaString(InitialState ? InitialState->Name : "");
    Out += "\"\n";
    Out += "    self.stateTime = 0.0\n\n";

    Out += "    self.states = {\n";
    for (const auto& Pair : Graph.States)
    {
        Out += EmitState(Graph, Pair.second, 2);
    }
    Out += "    }\n\n";

    Out += R"(    function self:getCurrentState()
        return self.currentState
    end

    function self:getStates()
        return self.states
    end

    function self:onEnterState(stateName)
        self.currentState = stateName
        self.stateTime = 0.0
    end

    function self:onExitState(stateName)
    end

    function self:update(ctx)
        self.stateTime = self.stateTime + (ctx.DeltaTime or 0.0)

        if ctx.IsInTransition then
            return {
                type = "none"
            }
        end

        local state = self.states[self.currentState]
        if state == nil then
            return {
                type = "none"
            }
        end

        for _, transition in ipairs(state.transitions or {}) do
            if transition.condition(ctx) then
                self:onExitState(self.currentState)

                return {
                    type = "transition",
                    from = self.currentState,
                    to = transition.to,
                    blendTime = transition.blendTime or 0.2,
                    resetTime = transition.resetTime ~= false,
                    blendMode = transition.blendMode or "Linear"
                }
            end
        end

        return {
            type = "state",
            state = self.currentState
        }
    end

    function self:completeTransition(toState)
        self:onEnterState(toState)
    end

    return self
end

return )";
    Out += MachineName;
    Out += "\n";

    return Out;
}

FString FLuaAnimGraphCodeGenerator::EmitState(
    const FLuaAnimGraph& Graph,
    const FLuaAnimStateNode& State,
    int32 IndentLevel) const
{
    FString Out;
    const FString I = Indent(IndentLevel);

    Out += I + "[\"" + EscapeLuaString(State.Name) + "\"] = {\n";
    Out += I + "    animation = \"" + EscapeLuaString(State.AnimationPath) + "\",\n";
    Out += I + "    loop = " + ToLuaBool(State.bLoop) + ",\n";
    Out += I + "    playRate = " + FormatFloat(State.PlayRate) + ",\n\n";
    Out += I + "    transitions = {\n";

    for (const auto& Pair : Graph.Transitions)
    {
        const FLuaAnimTransitionLink& Transition = Pair.second;
        if (Transition.FromStateId == State.StateId)
        {
            Out += EmitTransition(Graph, Transition, IndentLevel + 2);
        }
    }

    Out += I + "    }\n";
    Out += I + "},\n\n";
    return Out;
}

FString FLuaAnimGraphCodeGenerator::EmitTransition(
    const FLuaAnimGraph& Graph,
    const FLuaAnimTransitionLink& Transition,
    int32 IndentLevel) const
{
    const FLuaAnimStateNode* TargetState = Graph.FindState(Transition.ToStateId);
    if (!TargetState)
    {
        return "";
    }

    FString Out;
    const FString I = Indent(IndentLevel);

    Out += I + "{\n";
    Out += I + "    to = \"" + EscapeLuaString(TargetState->Name) + "\",\n";
    Out += I + "    blendTime = " + FormatFloat(Transition.BlendTime) + ",\n";
    Out += I + "    resetTime = " + ToLuaBool(Transition.bResetTime) + ",\n";
    Out += I + "    blendMode = \"" + ToLuaBlendMode(Transition.BlendMode) + "\",\n\n";
    Out += EmitConditionFunction(Transition, IndentLevel + 1);
    Out += I + "},\n\n";
    return Out;
}

FString FLuaAnimGraphCodeGenerator::EmitConditionFunction(const FLuaAnimTransitionLink& Transition, int32 IndentLevel) const
{
    FString Out;
    const FString I = Indent(IndentLevel);

    Out += I + "condition = function(ctx)\n";
    if (Transition.Conditions.empty())
    {
        Out += I + "    return false\n";
    }
    else
    {
        const FString JoinText = Transition.Join == EAnimConditionJoin::And ? " and " : " or ";
        FString Expr;
        for (size_t Index = 0; Index < Transition.Conditions.size(); ++Index)
        {
            if (Index > 0)
            {
                Expr += JoinText;
            }
            Expr += EmitCondition(Transition.Conditions[Index]);
        }
        Out += I + "    return " + Expr + "\n";
    }
    Out += I + "end\n";
    return Out;
}

FString FLuaAnimGraphCodeGenerator::EmitCondition(const FAnimCondition& Condition) const
{
    FString Left;
    if (Condition.bUseDefaultValue)
    {
        Left = "(ctx." + Condition.ContextName + " or " + Condition.DefaultValue + ")";
    }
    else
    {
        Left = "ctx." + Condition.ContextName;
    }

    return Left + " " + ToLuaCompareOp(Condition.Operator) + " " + Condition.Value;
}

const FLuaAnimStateNode* FLuaAnimGraphCodeGenerator::ResolveInitialState(const FLuaAnimGraph& Graph) const
{
    if (const FLuaAnimStateNode* State = Graph.FindState(Graph.InitialStateId))
    {
        return State;
    }

    return Graph.States.empty() ? nullptr : &Graph.States.begin()->second;
}

FString FLuaAnimGraphCodeGenerator::EscapeLuaString(const FString& Value) const
{
    FString Result;
    Result.reserve(Value.size());
    for (char C : Value)
    {
        switch (C)
        {
        case '\\':
            Result += "\\\\";
            break;
        case '"':
            Result += "\\\"";
            break;
        case '\n':
            Result += "\\n";
            break;
        case '\r':
            Result += "\\r";
            break;
        case '\t':
            Result += "\\t";
            break;
        default:
            Result += C;
            break;
        }
    }
    return Result;
}

FString FLuaAnimGraphCodeGenerator::ToLuaBool(bool bValue) const
{
    return bValue ? "true" : "false";
}

FString FLuaAnimGraphCodeGenerator::ToLuaBlendMode(EAnimBlendMode Mode) const
{
    return ToBlendModeLabel(Mode);
}

FString FLuaAnimGraphCodeGenerator::ToLuaCompareOp(EAnimCompareOp Op) const
{
    return MakeBinaryConditionText(Op);
}

FString FLuaAnimGraphCodeGenerator::Indent(int32 Level) const
{
    return FString(static_cast<size_t>(std::max(0, Level)) * 4, ' ');
}

FString FLuaAnimGraphCodeGenerator::FormatFloat(float Value) const
{
    std::ostringstream Stream;
    Stream << std::fixed << std::setprecision(3) << Value;
    return Stream.str();
}

FLuaAnimGraph MakeDefaultLuaAnimGraph()
{
    FLuaAnimGraph Graph;
    Graph.MachineName = "Machine";
    Graph.NextId = 1;
    Graph.InitialStateId = 0;
    Graph.PreviewSkeletalMeshPath.clear();
    Graph.States.clear();
    Graph.Transitions.clear();
    return Graph;
}
