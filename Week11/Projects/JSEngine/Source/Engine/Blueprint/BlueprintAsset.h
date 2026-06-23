#pragma once

#include "Object/Object.h"

#include "Blueprint/BlueprintGraph.h"

struct FArchive;

UCLASS()
class UBlueprintAsset : public UObject
{
	GENERATED_BODY(UBlueprintAsset, UObject)
public:
	static constexpr int32 CurrentPayloadVersion = 2;

	void Serialize(FArchive& Ar) override;
	void Serialize(FArchive& Ar, int32 PayloadVersion);

	bool SaveToFile(const FString& Path);
	bool LoadFromFile(const FString& Path);

	FBlueprintGraph& GetGraph() { return Graph; }
	const FBlueprintGraph& GetGraph() const { return Graph; }

	void SetAssetPath(const FString& Path) { AssetPath = Path; }
	const FString& GetAssetPath() const { return AssetPath; }

private:
	FString AssetPath;
	FBlueprintGraph Graph;
};
