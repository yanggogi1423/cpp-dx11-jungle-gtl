#pragma once
#include "Object/Object.h"

UCLASS()
class UAnimationAsset : public UObject
{
	GENERATED_BODY(UAnimationAsset, UObject)
public:    
    UAnimationAsset() = default;
    ~UAnimationAsset() override = default;
    
	void SetAssetPathFileName(const FString& InPathFileName) { AssetPathFileName = InPathFileName; }
	const FString& GetAssetPathFileName() const { return AssetPathFileName; }

private:
	FString AssetPathFileName;
};
