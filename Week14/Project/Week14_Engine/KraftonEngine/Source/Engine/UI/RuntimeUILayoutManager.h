#pragma once

#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"
#include "Object/GarbageCollection.h"

class URuntimeUILayoutAsset;

class FRuntimeUILayoutManager : public TSingleton<FRuntimeUILayoutManager>, public FGCObject
{
	friend class TSingleton<FRuntimeUILayoutManager>;

public:
	URuntimeUILayoutAsset* Load(const FString& Path);
	URuntimeUILayoutAsset* Find(const FString& Path) const;
	bool Save(URuntimeUILayoutAsset* Asset);

	const char* GetReferencerName() const override { return "FRuntimeUILayoutManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;
	void ClearCache();

private:
	TMap<FString, URuntimeUILayoutAsset*> LoadedLayouts;
};
