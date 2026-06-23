#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Math/Vector.h"

struct ImVec2;
struct FCombatCoverLink;
namespace ax { namespace NodeEditor { struct EditorContext; } }

class AActor;
class UCombatCoverAgentComponent;
class UCombatCoverNodeComponent;
class UCombatFlowManagerComponent;
class UWorld;

class FCombatMapEditorWidget : public FEditorWidget
{
public:
    FCombatMapEditorWidget() = default;
    ~FCombatMapEditorWidget() override;

    void Initialize(UEditorEngine* InEditorEngine) override;
    void Render(float DeltaTime) override;

private:
    void Refresh();
	void RefreshIfWorldOrPIEStateChanged();
	void ClearCachedRuntimePointers();
	void PruneInvalidCachedReferences();
    UWorld* GetEditorWorld() const;
    AActor* GetSelectedActor() const;
    UCombatFlowManagerComponent* FindOrUseManager() const;
    void RenderToolbar();
    void RenderMainLayout();
    void RenderMiddleLayout();
    void RenderLeftColumn();
    void RenderRightColumn();
    void RenderNodeList();
    void RenderSelectedNodePanel();
    void RenderSlotPanel(UCombatCoverNodeComponent* Node);
    void RenderLinkPanel(UCombatCoverNodeComponent* Node);
    void RenderGraphEditor();
    void RenderAgentPanel();
    void RenderRoleStatsPopup();
    void RenderAutoLinkPopup();
    void RenderGenerateBezierPathPointsPopup();
    void RenderProceduralObstacleMapPopup();
    void RenderValidationPopup();
    int32 GenerateProceduralObstacleMap();
    int32 ClearProceduralObstacleActors(UWorld* World) const;
    AActor* SpawnProceduralObstaclePrefab(UWorld* World, const FString& PrefabPath, const FVector& Location, float YawDegrees) const;
    template<typename TComponent>
    TComponent* AddComponentToSelectedActor();
    void SelectNode(UCombatCoverNodeComponent* Node, bool bNavigateGraphToNode = false);
    void QueueGraphNavigationToNode(UCombatCoverNodeComponent* Node);
    void ProcessPendingGraphNavigationToNode();

    void InitializeGraphEditor();
    void DestroyGraphEditor();
    void ResetGraphLayoutFromScene();
    void EnsureGraphNodePositionFromScene(UCombatCoverNodeComponent* Node, int32 NodeIndex);
    void ApplyGraphPositionToScene(UCombatCoverNodeComponent* Node, int32 NodeIndex);
    FVector GraphToWorld(const ImVec2& Position) const;
    ImVec2 WorldToGraph(const FVector& Position) const;
    UCombatCoverNodeComponent* FindLinkTargetNode(const FCombatCoverLink& Link) const;
    bool GenerateBezierPathPointsForLink(UCombatCoverNodeComponent* SourceNode, FCombatCoverLink& Link, int32 SampleCount, float Strength);
    int32 GenerateBezierPathPointsForAllLinks(int32 SampleCount, float Strength);

    UCombatCoverNodeComponent* CreateCoverNodeActorFromEditor(const ImVec2* GraphPosition = nullptr);
    UCombatCoverNodeComponent* DuplicateCoverNodeActor(UCombatCoverNodeComponent* SourceNode, const ImVec2* GraphPosition = nullptr);
    UCombatCoverNodeComponent* FindNodeByGraphNodeId(uint32 GraphNodeId) const;
    void GenerateNodeIdsAndRenameActors();
    int32 AutoLinkNearbyFromCachedNodes(float MaxDistance, int32 MaxLinksPerNode, bool bDirectedByX);
    void RenameActorToNodeId(UCombatCoverNodeComponent* Node);

private:
    TArray<UCombatCoverNodeComponent*> CachedNodes;
    TArray<UCombatCoverAgentComponent*> CachedAgents;
    UCombatFlowManagerComponent* CachedManager = nullptr;
	UWorld* CachedWorld = nullptr;
    UCombatCoverNodeComponent* SelectedNode = nullptr;
    int32 SelectedSlotIndex = -1;
    int32 LinkTargetIndex = -1;
    float AutoLinkMaxDistance = 1500.0f;
    int32 AutoLinkMaxLinksPerNode = 2;
    bool bAutoLinkDirectedByX = true;
    TArray<FString> LastValidationMessages;

    ax::NodeEditor::EditorContext* GraphEditorContext = nullptr;
    TSet<uint32> InitializedGraphItemIds;
    bool bGraphApplyToScene = true;
	bool bWasPlayingInEditor = false;
	bool bPendingGraphNavigateToContent = false;
	bool bPendingGraphNavigateToNode = false;
	uint32 PendingGraphNavigateNodeId = 0;
    bool bPendingOpenAutoLinkPopup = false;
    bool bPendingOpenGenerateBezierPathPointsPopup = false;
    bool bPendingOpenProceduralObstacleMapPopup = false;
    bool bPendingOpenValidationPopup = false;
    bool bPendingOpenRoleStatsPopup = false;
    int32 BezierPathSampleCount = 6;
    float BezierPathStrength = 120.0f;

    int32 ProceduralObstacleColumns = 7;
    int32 ProceduralObstacleRows = 5;
    float ProceduralObstacleMinX = -75.0f;
    float ProceduralObstacleMaxX = 75.0f;
    float ProceduralObstacleMinY = -40.0f;
    float ProceduralObstacleMaxY = 40.0f;
    float ProceduralObstacleLayoutRandomness = 0.20f;
    float ProceduralObstacleJitterX = 8.0f;
    float ProceduralObstacleJitterY = 6.0f;
    float ProceduralObstacleYawJitterDegrees = 8.0f;
    bool bProceduralObstacleClearPrevious = true;
    bool bProceduralObstacleClearExistingLinks = true;
    bool bProceduralObstacleRunAutoLink = true;
    bool bProceduralObstacleGenerateBezier = true;

    float GraphSceneUnitsPerGraphUnit = 1.0f / 15.0f;
};
