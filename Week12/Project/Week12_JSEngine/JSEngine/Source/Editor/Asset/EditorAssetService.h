#pragma once

#include "Core/CoreMinimal.h"

class UEditorEngine;
class UMaterial;
class UMaterialInstance;
class UMaterialInterface;
class UParticleSystem;
class UStaticMesh;
class USkeletalMesh;
class UAnimSequence;
class UTexture;

enum class EEditorAssetType : uint8
{
	StaticMesh,
	SkeletalMesh,
	AnimSequence,
	Texture,
	Material,
	AnimGraph,
	LuaAnimGraph,
	ParticleSystem,
	Font,
	SubUV,
	Scene,
	Script,
};

struct FEditorAssetItem
{
	FString Path;
	FString DisplayName;
	EEditorAssetType Type = EEditorAssetType::StaticMesh;
};

class FEditorAssetService
{
public:
	void Initialize(UEditorEngine* InEditorEngine);
	void RefreshAssetDatabase();

	const TArray<FEditorAssetItem>& GetAssets(EEditorAssetType Type) const;

	const TArray<FString>& GetStaticMeshAssetPaths() const { return StaticMeshPaths; }
	const TArray<FString>& GetSkeletalMeshAssetPaths() const { return SkeletalMeshPaths; }
	const TArray<FString>& GetAnimSequenceAssetPaths() const { return AnimSequencePaths; }
	const TArray<FString>& GetTextureAssetPaths() const { return TexturePaths; }
	const TArray<FString>& GetMaterialInterfaceNames() const { return MaterialInterfaceNames; }
	const TArray<FString>& GetAnimGraphAssetPaths() const { return AnimGraphPaths; }
	const TArray<FString>& GetLuaAnimGraphAssetPaths() const { return LuaAnimGraphPaths; }
	const TArray<FString>& GetParticleSystemAssetPaths() const { return ParticleSystemPaths; }
	const TArray<FString>& GetFontNames() const { return FontNames; }
	const TArray<FString>& GetSubUVNames() const { return SubUVNames; }

	UStaticMesh* LoadStaticMesh(const FString& Path) const;
	USkeletalMesh* LoadSkeletalMesh(const FString& Path) const;
	UAnimSequence* LoadAnimSequence(const FString& Path) const;
	UTexture* LoadTexture(const FString& Path) const;
	UParticleSystem* LoadParticleSystem(const FString& Path) const;
	UMaterialInterface* GetMaterialInterface(const FString& NameOrPath) const;
	UMaterialInterface* ResolveMaterialInterface(const FString& NameOrPath);
	UMaterialInterface* ResolveMaterialInterfaceByIndex(int32 MaterialIndex);
	UTexture* GetMaterialPreviewTexture(UMaterialInterface* Material) const;
	UMaterialInstance* CreateMaterialInstance(const FString& InstancePath, UMaterial* Parent) const;
	bool SaveMaterial(const FString& MaterialPath, UMaterial* Material);
	bool SaveMaterialInstance(const FString& InstancePath, UMaterialInstance* Instance);

private:
	static void AddUniquePath(TArray<FString>& Paths, const FString& Path);
	static void BuildItems(const TArray<FString>& Paths, EEditorAssetType Type, TArray<FEditorAssetItem>& OutItems);

private:
	UEditorEngine* EditorEngine = nullptr;

	TArray<FString> StaticMeshPaths;
	TArray<FString> SkeletalMeshPaths;
	TArray<FString> AnimSequencePaths;
	TArray<FString> TexturePaths;
	TArray<FString> MaterialInterfaceNames;
	TArray<FString> AnimGraphPaths;
	TArray<FString> LuaAnimGraphPaths;
	TArray<FString> ParticleSystemPaths;
	TArray<FString> FontNames;
	TArray<FString> SubUVNames;
	TArray<UMaterialInterface*> CachedMaterialInterfaces;
	TArray<bool> CachedMaterialInterfaceResolved;

	TArray<FEditorAssetItem> StaticMeshItems;
	TArray<FEditorAssetItem> SkeletalMeshItems;
	TArray<FEditorAssetItem> AnimSequenceItems;
	TArray<FEditorAssetItem> TextureItems;
	TArray<FEditorAssetItem> MaterialItems;
	TArray<FEditorAssetItem> AnimGraphItems;
	TArray<FEditorAssetItem> LuaAnimGraphItems;
	TArray<FEditorAssetItem> ParticleSystemItems;
	TArray<FEditorAssetItem> FontItems;
	TArray<FEditorAssetItem> SubUVItems;
	TArray<FEditorAssetItem> EmptyItems;
};
