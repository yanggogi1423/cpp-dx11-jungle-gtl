#include "Animation/AnimLuaProgramAsset.h"

void FAnimLuaProgramAssetPayload::Serialize(FArchive& Ar, int32 PayloadVersion)
{
    if (Ar.IsLoading() && PayloadVersion < 2)
    {
        // Old graph assets used name-based nested transitions and must be recreated.
        Graph = MakeDefaultLuaAnimGraph();
        GeneratedLuaSource = FLuaAnimGraphCodeGenerator().Generate(Graph);
        return;
    }

    Graph.Serialize(Ar, PayloadVersion);
    Ar << GeneratedLuaSource;
}
