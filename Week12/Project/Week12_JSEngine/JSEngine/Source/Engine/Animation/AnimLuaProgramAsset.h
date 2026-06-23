#pragma once

#include "Animation/LuaAnimGraph.h"
#include "Engine/Object/Object.h"

struct FAnimLuaProgramAssetPayload
{
    FLuaAnimGraph Graph;
    FString GeneratedLuaSource;

    void Serialize(FArchive& Ar, int32 PayloadVersion);
};

UCLASS()
class UAnimLuaProgramAsset : public UObject
{
public:
    GENERATED_BODY(UAnimLuaProgramAsset, UObject)

public:
    void SetAssetPathFileName(const FString& InPathFileName) { AssetPathFileName = InPathFileName; }
    const FString& GetAssetPathFileName() const { return AssetPathFileName; }

    void SetGraph(const FLuaAnimGraph& InGraph) { Graph = InGraph; }
    FLuaAnimGraph& GetGraph() { return Graph; }
    const FLuaAnimGraph& GetGraph() const { return Graph; }

    void SetGeneratedLuaSource(const FString& InSource) { GeneratedLuaSource = InSource; }
    const FString& GetGeneratedLuaSource() const { return GeneratedLuaSource; }

    void SetEntryStateId(int32 InEntryStateId) { Graph.InitialStateId = InEntryStateId; }
    int32 GetEntryStateId() const { return Graph.InitialStateId; }

private:
    FString AssetPathFileName;
    FLuaAnimGraph Graph;
    FString GeneratedLuaSource;
};
