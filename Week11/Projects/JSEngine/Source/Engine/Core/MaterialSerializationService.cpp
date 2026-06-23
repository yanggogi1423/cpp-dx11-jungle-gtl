#include "Core/MaterialSerializationService.h"

#include "Asset/AssetFile.h"
#include "Asset/AssetMetaData.h"
#include "Core/Guid.h"
#include "Core/AssetPathPolicy.h"
#include "Core/Logging/Log.h"
#include "Core/MaterialResourceCache.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/Texture.h"
#include "SimpleJSON/json.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
	using json::JSON;

	JSON SerializeMaterialParam(const FString& ParamName, const FMaterialParamValue& ParamValue)
	{
		JSON Param = JSON::Make(JSON::Class::Object);
		Param["Name"] = ParamName;
		if (std::holds_alternative<bool>(ParamValue.Value))
		{
			Param["Type"] = "Bool";
			Param["Value"] = std::get<bool>(ParamValue.Value);
		}
		else if (std::holds_alternative<int>(ParamValue.Value))
		{
			Param["Type"] = "Int";
			Param["Value"] = std::get<int>(ParamValue.Value);
		}
		else if (std::holds_alternative<uint32>(ParamValue.Value))
		{
			Param["Type"] = "UInt";
			Param["Value"] = std::get<uint32>(ParamValue.Value);
		}
		else if (std::holds_alternative<float>(ParamValue.Value))
		{
			Param["Type"] = "Float";
			Param["Value"] = std::get<float>(ParamValue.Value);
		}
		else if (std::holds_alternative<FVector2>(ParamValue.Value))
		{
			const FVector2& Vec = std::get<FVector2>(ParamValue.Value);
			Param["Type"] = "Vector2";
			Param["Value"] = JSON::Make(JSON::Class::Array);
			Param["Value"].append(Vec.X);
			Param["Value"].append(Vec.Y);
		}
		else if (std::holds_alternative<FVector>(ParamValue.Value))
		{
			const FVector& Vec = std::get<FVector>(ParamValue.Value);
			Param["Type"] = "Vector3";
			Param["Value"] = JSON::Make(JSON::Class::Array);
			Param["Value"].append(Vec.X);
			Param["Value"].append(Vec.Y);
			Param["Value"].append(Vec.Z);
		}
		else if (std::holds_alternative<FVector4>(ParamValue.Value))
		{
			const FVector4& Vec = std::get<FVector4>(ParamValue.Value);
			Param["Type"] = "Vector4";
			Param["Value"] = JSON::Make(JSON::Class::Array);
			Param["Value"].append(Vec.X);
			Param["Value"].append(Vec.Y);
			Param["Value"].append(Vec.Z);
			Param["Value"].append(Vec.W);
		}
		else if (std::holds_alternative<FMatrix>(ParamValue.Value))
		{
			const FMatrix& Mat = std::get<FMatrix>(ParamValue.Value);
			Param["Type"] = "Matrix4";
			JSON MatArray = JSON::Make(JSON::Class::Array);
			for (int Row = 0; Row < 4; ++Row)
			{
				JSON RowArray = JSON::Make(JSON::Class::Array);
				for (int Col = 0; Col < 4; ++Col)
				{
					RowArray.append(Mat.M[Row][Col]);
				}
				MatArray.append(RowArray);
			}
			Param["Value"] = MatArray;
		}
		else if (std::holds_alternative<UTexture*>(ParamValue.Value))
		{
			UTexture* Texture = std::get<UTexture*>(ParamValue.Value);
			Param["Type"] = "Texture";
			Param["Value"] = Texture ? Texture->GetFilePath() : "";
		}
		return Param;
	}

	void ApplyTypedParam(UMaterialInstance* MaterialInstance, const FString& ParamName, const FString& Type, JSON& Param, FResourceManager& ResourceManager)
	{
		if (Type == "Bool")
		{
			MaterialInstance->SetParam(ParamName, FMaterialParamValue(Param["Value"].ToBool()));
		}
		else if (Type == "Int")
		{
			MaterialInstance->SetParam(ParamName, FMaterialParamValue(static_cast<int32>(Param["Value"].ToInt())));
		}
		else if (Type == "UInt")
		{
			MaterialInstance->SetParam(ParamName, FMaterialParamValue(static_cast<uint32>(Param["Value"].ToInt())));
		}
		else if (Type == "Float")
		{
			MaterialInstance->SetParam(ParamName, FMaterialParamValue(static_cast<float>(Param["Value"].ToFloat())));
		}
		else if (Type == "Vector2" || Type == "FVector2")
		{
			MaterialInstance->SetParam(ParamName, FMaterialParamValue(FVector2(
				static_cast<float>(Param["Value"][0].ToFloat()),
				static_cast<float>(Param["Value"][1].ToFloat()))));
		}
		else if (Type == "Vector3" || Type == "FVector3")
		{
			MaterialInstance->SetParam(ParamName, FMaterialParamValue(FVector(
				static_cast<float>(Param["Value"][0].ToFloat()),
				static_cast<float>(Param["Value"][1].ToFloat()),
				static_cast<float>(Param["Value"][2].ToFloat()))));
		}
		else if (Type == "Vector4" || Type == "FVector4")
		{
			MaterialInstance->SetParam(ParamName, FMaterialParamValue(FVector4(
				static_cast<float>(Param["Value"][0].ToFloat()),
				static_cast<float>(Param["Value"][1].ToFloat()),
				static_cast<float>(Param["Value"][2].ToFloat()),
				static_cast<float>(Param["Value"][3].ToFloat()))));
		}
		else if (Type == "Matrix4")
		{
			FMatrix Value;
			for (int Row = 0; Row < 4; ++Row)
			{
				for (int Col = 0; Col < 4; ++Col)
				{
					Value.M[Row][Col] = static_cast<float>(Param["Value"][Row][Col].ToFloat());
				}
			}
			MaterialInstance->SetParam(ParamName, FMaterialParamValue(Value));
		}
		else if (Type == "Texture")
		{
			const FString TexPath = Param["Value"].ToString();
			UTexture* Texture = ResourceManager.LoadTexture(TexPath);
			if (Texture)
			{
				MaterialInstance->SetParam(ParamName, FMaterialParamValue(Texture));
			}
		}
	}

	void ApplyMaterialDataVector(UMaterial* Material, const FString& ParamName, const FVector& Value)
	{
		if (ParamName == "AmbientColor")
		{
			Material->MaterialData.AmbientColor = Value;
		}
		else if (ParamName == "DiffuseColor")
		{
			Material->MaterialData.DiffuseColor = Value;
		}
		else if (ParamName == "SpecularColor")
		{
			Material->MaterialData.SpecularColor = Value;
		}
		else if (ParamName == "EmissiveColor")
		{
			Material->MaterialData.EmissiveColor = Value;
		}
	}

	void ApplyMaterialDataTexture(UMaterial* Material, const FString& ParamName, const FString& TexPath)
	{
		const FString NormalizedTexPath = FPaths::Normalize(TexPath);
		if (ParamName == "DiffuseMap")
		{
			Material->MaterialData.DiffuseTexPath = NormalizedTexPath;
			Material->MaterialData.bHasDiffuseTexture = true;
			Material->SetParam("bHasDiffuseMap", FMaterialParamValue(true));
		}
		else if (ParamName == "SpecularMap")
		{
			Material->MaterialData.SpecularTexPath = NormalizedTexPath;
			Material->MaterialData.bHasSpecularTexture = true;
			Material->SetParam("bHasSpecularMap", FMaterialParamValue(true));
		}
		else if (ParamName == "EmissiveMap")
		{
			Material->MaterialData.EmissiveTexPath = NormalizedTexPath;
			Material->MaterialData.bHasEmissiveTexture = true;
			Material->SetParam("bHasEmissiveMap", FMaterialParamValue(true));
		}
		else if (ParamName == "AmbientMap")
		{
			Material->MaterialData.AmbientTexPath = NormalizedTexPath;
			Material->MaterialData.bHasAmbientTexture = true;
			Material->SetParam("bHasAmbientMap", FMaterialParamValue(true));
		}
		else if (ParamName == "BumpMap")
		{
			Material->MaterialData.BumpTexPath = NormalizedTexPath;
			Material->MaterialData.bHasBumpTexture = true;
			Material->SetParam("bHasBumpMap", FMaterialParamValue(true));
		}
	}

	void ApplyTypedParam(UMaterial* Material, const FString& ParamName, const FString& Type, JSON& Param, FResourceManager& ResourceManager)
	{
		if (Type == "Bool")
		{
			Material->SetParam(ParamName, FMaterialParamValue(Param["Value"].ToBool()));
		}
		else if (Type == "Int")
		{
			Material->SetParam(ParamName, FMaterialParamValue(static_cast<int32>(Param["Value"].ToInt())));
		}
		else if (Type == "UInt")
		{
			Material->SetParam(ParamName, FMaterialParamValue(static_cast<uint32>(Param["Value"].ToInt())));
		}
		else if (Type == "Float")
		{
			Material->SetParam(ParamName, FMaterialParamValue(static_cast<float>(Param["Value"].ToFloat())));
		}
		else if (Type == "Vector2" || Type == "FVector2")
		{
			Material->SetParam(ParamName, FMaterialParamValue(FVector2(
				static_cast<float>(Param["Value"][0].ToFloat()),
				static_cast<float>(Param["Value"][1].ToFloat()))));
		}
		else if (Type == "Vector3" || Type == "FVector3")
		{
			FVector Value(
				static_cast<float>(Param["Value"][0].ToFloat()),
				static_cast<float>(Param["Value"][1].ToFloat()),
				static_cast<float>(Param["Value"][2].ToFloat()));
			Material->SetParam(ParamName, FMaterialParamValue(Value));
			ApplyMaterialDataVector(Material, ParamName, Value);
		}
		else if (Type == "Vector4" || Type == "FVector4")
		{
			Material->SetParam(ParamName, FMaterialParamValue(FVector4(
				static_cast<float>(Param["Value"][0].ToFloat()),
				static_cast<float>(Param["Value"][1].ToFloat()),
				static_cast<float>(Param["Value"][2].ToFloat()),
				static_cast<float>(Param["Value"][3].ToFloat()))));
		}
		else if (Type == "Matrix4")
		{
			FMatrix Value;
			for (int Row = 0; Row < 4; ++Row)
			{
				for (int Col = 0; Col < 4; ++Col)
				{
					Value.M[Row][Col] = static_cast<float>(Param["Value"][Row][Col].ToFloat());
				}
			}
			Material->SetParam(ParamName, FMaterialParamValue(Value));
		}
		else if (Type == "Texture")
		{
			const FString TexPath = Param["Value"].ToString();
			UTexture* Texture = ResourceManager.LoadTexture(TexPath);
			if (Texture)
			{
				Material->SetParam(ParamName, FMaterialParamValue(Texture));
				ApplyMaterialDataTexture(Material, ParamName, TexPath);
			}
		}
	}

	FSerializedMaterialParam MakeSerializedParam(const FString& Name, const FMaterialParamValue& Value)
	{
		FSerializedMaterialParam Out;
		Out.Name = Name;
		Out.Type = Value.Type;

		switch (Value.Type)
		{
		case EMaterialParamType::Bool:
			Out.BoolValue = std::get<bool>(Value.Value);
			break;
		case EMaterialParamType::Int:
			Out.IntValue = std::get<int32>(Value.Value);
			break;
		case EMaterialParamType::UInt:
			Out.UIntValue = std::get<uint32>(Value.Value);
			break;
		case EMaterialParamType::Float:
			Out.FloatValue = std::get<float>(Value.Value);
			break;
		case EMaterialParamType::Vector2:
			Out.Vector2Value = std::get<FVector2>(Value.Value);
			break;
		case EMaterialParamType::Vector3:
			Out.Vector3Value = std::get<FVector>(Value.Value);
			break;
		case EMaterialParamType::Vector4:
			Out.Vector4Value = std::get<FVector4>(Value.Value);
			break;
		case EMaterialParamType::Matrix4:
			Out.Matrix4Value = std::get<FMatrix>(Value.Value);
			break;
		case EMaterialParamType::Texture:
		{
			UTexture* Texture = std::get<UTexture*>(Value.Value);
			Out.TexturePath = Texture ? FPaths::Normalize(Texture->GetFilePath()) : "";
			break;
		}
		}

		return Out;
	}

	FMaterialParamValue MakeRuntimeParam(const FSerializedMaterialParam& SerializedParam, FResourceManager& ResourceManager)
	{
		switch (SerializedParam.Type)
		{
		case EMaterialParamType::Bool:
			return FMaterialParamValue(SerializedParam.BoolValue);
		case EMaterialParamType::Int:
			return FMaterialParamValue(SerializedParam.IntValue);
		case EMaterialParamType::UInt:
			return FMaterialParamValue(SerializedParam.UIntValue);
		case EMaterialParamType::Float:
			return FMaterialParamValue(SerializedParam.FloatValue);
		case EMaterialParamType::Vector2:
			return FMaterialParamValue(SerializedParam.Vector2Value);
		case EMaterialParamType::Vector3:
			return FMaterialParamValue(SerializedParam.Vector3Value);
		case EMaterialParamType::Vector4:
			return FMaterialParamValue(SerializedParam.Vector4Value);
		case EMaterialParamType::Matrix4:
			return FMaterialParamValue(SerializedParam.Matrix4Value);
		case EMaterialParamType::Texture:
		{
			UTexture* Texture = ResourceManager.LoadTexture(SerializedParam.TexturePath);
			return FMaterialParamValue(Texture);
		}
		default:
			return FMaterialParamValue();
		}
	}

	void SerializeMaterialParamArray(FArchive& Ar, TArray<FSerializedMaterialParam>& Params, int32 PayloadVersion)
	{
		int32 Count = static_cast<int32>(Params.size());
		Ar.BeginArray("Params", Count);

		if (Ar.IsLoading())
		{
			Params.resize(Count);
		}

		for (FSerializedMaterialParam& Param : Params)
		{
			Param.Serialize(Ar, PayloadVersion);
		}

		Ar.EndArray();
	}
}

FMaterialSerializationService::FMaterialSerializationService(FResourceManager& InResourceManager)
	: ResourceManager(InResourceManager)
{
}

bool FMaterialSerializationService::SerializeMaterial(const FString& MatFilePath, const UMaterial* Material)
{
	const FString NormalizedPath = FPaths::Normalize(MatFilePath);

	FMaterialAssetPayload Payload;
	Payload.Name = Material->Name;
	Payload.ImportedName = Material->ImportedName;
	Payload.ShaderType = Material->GetShaderType();

	for (const auto& [ParamName, ParamValue] : Material->MaterialParams)
	{
		Payload.Params.push_back(MakeSerializedParam(ParamName, ParamValue));
	}

	FAssetMetaData MetaData;
	MetaData.Version = 1;
	MetaData.PayloadVersion = 1;
	MetaData.AssetGuid = FGuid::NewGuid().ToString();
	MetaData.ClassName = UMaterial::StaticClass()->ClassName;
	MetaData.DisplayName = Material->Name.empty()
		? FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(NormalizedPath)).stem().wstring())
		: Material->Name;
	MetaData.SourceFile = "";

	return FAssetFile::Save(NormalizedPath, MetaData, [&](FArchive& Ar)
	{
		Payload.Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});
}

bool FMaterialSerializationService::SerializeMaterialInstance(const FString& MatInstFilePath, const UMaterialInstance* MaterialInstance)
{
	const FString NormalizedPath = FPaths::Normalize(MatInstFilePath);

	if (!MaterialInstance->Parent || MaterialInstance->Parent->GetFilePath().empty())
	{
		UE_LOG_WARNING("Cannot save material instance without parent: %s", NormalizedPath.c_str());
		return false;
	}

	FMaterialInstanceAssetPayload Payload;
	Payload.Name = MaterialInstance->Name;
	Payload.Parent = FPaths::Normalize(MaterialInstance->Parent->GetFilePath());

	for (const auto& [ParamName, ParamValue] : MaterialInstance->OverridedParams)
	{
		Payload.OverridedParams.push_back(MakeSerializedParam(ParamName, ParamValue));
	}

	FAssetMetaData MetaData;
	MetaData.Version = 1;
	MetaData.PayloadVersion = 1;
	MetaData.AssetGuid = FGuid::NewGuid().ToString();
	MetaData.ClassName = UMaterialInstance::StaticClass()->ClassName;
	MetaData.DisplayName = MaterialInstance->Name.empty()
		? FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(NormalizedPath)).stem().wstring())
		: MaterialInstance->Name;
	MetaData.SourceFile = "";

	return FAssetFile::Save(NormalizedPath, MetaData, [&](FArchive& Ar)
	{
		Payload.Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});
}

bool FMaterialSerializationService::DeserializeMaterial(const FString& MatFilePath)
{
	const FString NormalizedPath = FPaths::Normalize(MatFilePath);

	FAssetMetaData MetaData;
	if (!FAssetFile::LoadMetadataOnly(NormalizedPath, MetaData)) return false;

	if (MetaData.ClassName == UMaterial::StaticClass()->ClassName)
	{
		FMaterialAssetPayload Payload;
		if (!FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
		{
			Payload.Serialize(Ar, MetaData.PayloadVersion);
			return true;
		}))
		{
			return false;
		}

		const FString MaterialName = Payload.Name.empty()
			? MetaData.DisplayName
			: Payload.Name;

		UMaterial* Material = ResourceManager.GetOrCreateMaterial(
			MaterialName.empty() ? NormalizedPath : MaterialName,
			NormalizedPath,
			Payload.ShaderType);

		Material->Name = MaterialName.empty() ? NormalizedPath : MaterialName;
		Material->FilePath = NormalizedPath;
		Material->ImportedName = Payload.ImportedName;
		Material->SetShaderType(Payload.ShaderType);
		Material->MaterialData = FMaterial();
		Material->MaterialData.Name = Material->Name;
		Material->MaterialParams.clear();

		for (const FSerializedMaterialParam& Param : Payload.Params)
		{
			FMaterialParamValue RuntimeValue = MakeRuntimeParam(Param, ResourceManager);
			Material->SetParam(Param.Name, RuntimeValue);

			if (Param.Type == EMaterialParamType::Vector3)
			{
				ApplyMaterialDataVector(Material, Param.Name, Param.Vector3Value);
			}
			else if (Param.Type == EMaterialParamType::Texture)
			{
				ApplyMaterialDataTexture(Material, Param.Name, Param.TexturePath);
			}
		}

		ResourceManager.MaterialCache.RegisterMaterial(NormalizedPath, Material);
		ResourceManager.MaterialCache.RegisterMaterial(Material->Name, Material);

		if (!Material->ImportedName.empty())
		{
			ResourceManager.MaterialCache.RegisterMaterial(Material->ImportedName, Material);
		}
	}
	else if (MetaData.ClassName == UMaterialInstance::StaticClass()->ClassName)
	{
		FMaterialInstanceAssetPayload Payload;
		if (!FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
		{
			Payload.Serialize(Ar, MetaData.PayloadVersion);
			return true;
		}))
		{
			return false;
		}

		const FString ParentPath = FPaths::Normalize(Payload.Parent);
		if (ParentPath.empty())
		{
			UE_LOG_WARNING("MaterialInstance parent is empty: %s", NormalizedPath.c_str());
			return false;
		}

		UMaterial* ParentMat = ResourceManager.GetMaterial(ParentPath);
		if (!ParentMat && FAssetPathPolicy::IsSerializedMaterialAssetPath(ParentPath) && FAssetPathPolicy::FileExists(ParentPath))
		{
			DeserializeMaterial(ParentPath);
			ParentMat = ResourceManager.GetMaterial(ParentPath);
		}

		if (!ParentMat)
		{
			UE_LOG_WARNING("Parent material not found: %s", ParentPath.c_str());
			return false;
		}

		UMaterialInstance* MatInstance = ResourceManager.CreateMaterialInstance(NormalizedPath, ParentMat);
		MatInstance->Name = Payload.Name.empty() ? NormalizedPath : Payload.Name;
		MatInstance->FilePath = NormalizedPath;
		MatInstance->Parent = ParentMat;
		MatInstance->OverridedParams.clear();

		for (const FSerializedMaterialParam& Param : Payload.OverridedParams)
		{
			MatInstance->SetParam(Param.Name, MakeRuntimeParam(Param, ResourceManager));
		}

		ResourceManager.MaterialCache.RegisterMaterialInstance(NormalizedPath, MatInstance);
		return true;
	}
	else
	{
		UE_LOG_WARNING("Unsupported material asset class: %s", MetaData.ClassName.c_str());
		return false;
	}

	return true;
}

void FSerializedMaterialParam::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	Ar << Name;

	int32 TypeValue = static_cast<int32>(Type);
	Ar << TypeValue;
	if (Ar.IsLoading())
	{
		Type = static_cast<EMaterialParamType>(TypeValue);
	}

	switch (Type)
	{
	case EMaterialParamType::Bool:
		Ar << BoolValue;
		break;
	case EMaterialParamType::Int:
		Ar << IntValue;
		break;
	case EMaterialParamType::UInt:
		Ar << UIntValue;
		break;
	case EMaterialParamType::Float:
		Ar << FloatValue;
		break;
	case EMaterialParamType::Vector2:
		Ar << Vector2Value;
		break;
	case EMaterialParamType::Vector3:
		Ar << Vector3Value;
		break;
	case EMaterialParamType::Vector4:
		Ar << Vector4Value;
		break;
	case EMaterialParamType::Matrix4:
		Ar << Matrix4Value;
		break;
	case EMaterialParamType::Texture:
		Ar << TexturePath;
		break;
	}
}

void FMaterialAssetPayload::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	Ar << Name;
	Ar << ImportedName;

	int32 ShaderTypeValue = static_cast<int32>(ShaderType);
	Ar << ShaderTypeValue;
	if (Ar.IsLoading())
	{
		ShaderType = static_cast<EMaterialShaderType>(ShaderTypeValue);
	}

	SerializeMaterialParamArray(Ar, Params, PayloadVersion);
}

void FMaterialInstanceAssetPayload::Serialize(FArchive& Ar, int32 PayloadVersion)
{
	Ar << Name;
	Ar << Parent;
	SerializeMaterialParamArray(Ar, OverridedParams, PayloadVersion);
}
