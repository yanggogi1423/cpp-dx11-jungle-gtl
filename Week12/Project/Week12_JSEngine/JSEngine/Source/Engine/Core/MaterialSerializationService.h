#pragma once

#include "Core/CoreMinimal.h"
#include "Render/Resource/Material.h"
#include "Serialization/Archive.h"

class FResourceManager;
class UMaterial;
class UMaterialInstance;

struct FSerializedMaterialParam
{
	FString Name;
	EMaterialParamType Type = EMaterialParamType::Float;
	bool BoolValue = false;
	int32 IntValue = 0;
	uint32 UIntValue = 0;
	float FloatValue = 0.0f;
	FVector2 Vector2Value;
	FVector Vector3Value;
	FVector4 Vector4Value;
	FMatrix Matrix4Value;
	FString TexturePath;

	void Serialize(FArchive& Ar, int32 PayloadVersion);
};

struct FMaterialAssetPayload
{
	FString Name;
	FString ImportedName;
	EMaterialShaderType ShaderType = EMaterialShaderType::SurfaceLit;
	ESamplerType SamplerType = ESamplerType::EST_Linear;
	EDepthStencilType DepthStencilType = EDepthStencilType::Default;
	EBlendType BlendType = EBlendType::Opaque;
	ERasterizerType RasterizerType = ERasterizerType::SolidBackCull;
	TArray<FSerializedMaterialParam> Params;

	void Serialize(FArchive& Ar, int32 PayloadVersion);
};

struct FMaterialInstanceAssetPayload
{
	FString Name;
	FString Parent;
	TArray<FSerializedMaterialParam> OverridedParams;

	void Serialize(FArchive& Ar, int32 PayloadVersion);
};

class FMaterialSerializationService
{
public:
	explicit FMaterialSerializationService(FResourceManager& InResourceManager);

	bool SerializeMaterial(const FString& MatFilePath, const UMaterial* Material);
	bool SerializeMaterialInstance(const FString& MatInstFilePath, const UMaterialInstance* MaterialInstance);
	bool DeserializeMaterial(const FString& MatFilePath);

private:
	FResourceManager& ResourceManager;
};
