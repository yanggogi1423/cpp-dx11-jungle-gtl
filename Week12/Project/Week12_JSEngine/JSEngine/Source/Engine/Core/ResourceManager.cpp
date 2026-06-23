#include "Core/ResourceManager.h"

#include "Core/Paths.h"
#include "Core/AssetPathPolicy.h"
#include "Core/ImportedMaterialPolicy.h"
#include "Core/MaterialLoadService.h"
#include "Core/MaterialSerializationService.h"
#include "Core/Reflection/ReflectionRegistry.h"
#include "Core/ResourceMemoryReporter.h"
#include "Core/SkeletalMeshLoadService.h"
#include "Core/StaticMeshLoadService.h"
#include "Core/Guid.h"
#include "Object/Object.h"
#include "Object/Property.h"
#include "Particle/ParticleSystem.h"

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cwctype>
#include <cstdio>
#include <fstream>
#include <unordered_set>
#include "Asset/FileUtils.h"
#include "Animation/AnimSequence.h"

#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "Core/Logging/Log.h"

#if WITH_EDITOR
#include "Settings/EditorSettings.h"
#endif

#include "Asset/BinarySerializer.h"
#include "Asset/AssetFile.h"
#include "Asset/StaticMeshTypes.h"
#include "Asset/StaticMeshSimplifier.h"
#include "Render/Scene/RenderCommand.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

namespace
{
	bool ShouldBuildStaticMeshLODs()
	{
#if WITH_EDITOR
		return FEditorSettings::Get().ShowFlags.bEnableLOD;
#else
		return true;
#endif
	}

	bool IsFbxSourcePath(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
		std::wstring Extension = FsPath.extension().wstring();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
		return Extension == L".fbx";
	}

	bool IsStaticMeshSourcePath(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
		std::wstring Extension = FsPath.extension().wstring();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
		return Extension == L".obj" || Extension == L".fbx";
	}

	bool IsParticleSystemAssetPath(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
		std::wstring Extension = FsPath.extension().wstring();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
		return Extension == L".uasset";
	}

	bool IsParticleSystemGraphObject(UObject* Object)
	{
		return Object &&
			(Object->IsA(UParticleSystem::StaticClass()) ||
			 Object->IsA(UParticleEmitter::StaticClass()) ||
			 Object->IsA(UParticleLODLevel::StaticClass()) ||
			 Object->IsA(UParticleModule::StaticClass()) ||
			 Object->IsA(UParticleRendererProperties::StaticClass()));
	}

	bool IsParticleSystemGraphClass(UClass* Class)
	{
		return Class &&
			(Class->IsChildOf(UParticleSystem::StaticClass()) ||
			 Class->IsChildOf(UParticleEmitter::StaticClass()) ||
			 Class->IsChildOf(UParticleLODLevel::StaticClass()) ||
			 Class->IsChildOf(UParticleModule::StaticClass()) ||
			 Class->IsChildOf(UParticleRendererProperties::StaticClass()));
	}

	bool ContainsParticleRuntimeSerializationToken(const std::string& SerializedText, FString& OutToken)
	{
		static constexpr const char* RuntimeTokens[] =
		{
			"FCompiledParticleLODData",
			"FParticleEmitterInstance",
			"FBaseParticle",
			"FParticleDataContainer",
			"FParticleEmitterRuntimeView",
			"UParticleSystemComponent",
			"ParticleData",
			"ParticleIndices",
			"ActiveParticles",
			"EmitterInstances",
			"PendingCollisionEvents",
			"CurrentCompiledLOD"
		};

		for (const char* Token : RuntimeTokens)
		{
			if (Token && SerializedText.find(Token) != std::string::npos)
			{
				OutToken = Token;
				return true;
			}
		}
		return false;
	}

	bool ValidateParticleSystemSerializedClasses(json::JSON& JsonData, FString& OutClassName)
	{
		if (!JsonData.hasKey("Objects") || JsonData["Objects"].JSONType() != json::JSON::Class::Array)
		{
			return true;
		}

		json::JSON& ObjectsJson = JsonData["Objects"];
		for (int32 Index = 0; Index < static_cast<int32>(ObjectsJson.length()); ++Index)
		{
			json::JSON& ObjectNode = ObjectsJson.at(Index);
			if (ObjectNode.JSONType() != json::JSON::Class::Object)
			{
				continue;
			}

			const FString ClassName = ObjectNode.hasKey("Class")
				? ObjectNode["Class"].ToString()
				: (ObjectNode.hasKey("Data") && ObjectNode["Data"].hasKey("Type") ? ObjectNode["Data"]["Type"].ToString() : FString());
			UClass* Class = FReflectionRegistry::Get().FindClass(ClassName);
			if (!IsParticleSystemGraphClass(Class))
			{
				OutClassName = ClassName;
				return false;
			}
		}
		return true;
	}

	class FParticleSystemObjectGraphResolver final : public IObjectReferenceResolver
	{
	public:
		uint32 AddObject(UObject* Object)
		{
			if (!Object)
			{
				return 0;
			}

			auto It = ObjectToId.find(Object);
			if (It != ObjectToId.end())
			{
				return It->second;
			}

			Objects.push_back(Object);
			const uint32 Id = static_cast<uint32>(Objects.size());
			ObjectToId[Object] = Id;
			return Id;
		}

		void SetObject(uint32 Id, UObject* Object)
		{
			if (Id == 0 || !Object)
			{
				return;
			}

			if (Objects.size() < Id)
			{
				Objects.resize(Id, nullptr);
			}

			Objects[Id - 1] = Object;
			ObjectToId[Object] = Id;
		}

		uint32 GetObjectId(UObject* Object) const override
		{
			auto It = ObjectToId.find(Object);
			return It != ObjectToId.end() ? It->second : 0;
		}

		UObject* ResolveObjectId(uint32 Id, UClass* ExpectedClass) const override
		{
			if (Id == 0 || Id > Objects.size())
			{
				return nullptr;
			}

			UObject* Object = Objects[Id - 1];
			if (!Object || (ExpectedClass && !Object->IsA(ExpectedClass)))
			{
				return nullptr;
			}
			return Object;
		}

		const TArray<UObject*>& GetObjects() const { return Objects; }

	private:
		TArray<UObject*> Objects;
		TMap<UObject*, uint32> ObjectToId;
	};

	void CollectParticleSystemObjectGraph(UObject* RootObject, FParticleSystemObjectGraphResolver& Resolver)
	{
		if (!IsParticleSystemGraphObject(RootObject))
		{
			return;
		}

		Resolver.AddObject(RootObject);
		for (int32 ObjectIndex = 0; ObjectIndex < static_cast<int32>(Resolver.GetObjects().size()); ++ObjectIndex)
		{
			UObject* Object = Resolver.GetObjects()[ObjectIndex];
			if (!IsParticleSystemGraphObject(Object) || !Object->GetClass())
			{
				continue;
			}

			TArray<const FProperty*> Properties;
			Object->GetClass()->GetAllProperties(Properties);
			for (const FProperty* Property : Properties)
			{
				if (!Property || Property->IsTransient())
				{
					continue;
				}

				FReferenceCollector Collector;
				Property->VisitReferences(Collector, Property->GetValuePtr(Object));
				for (UObject* ReferencedObject : Collector.GetReferencedObjects())
				{
					if (IsParticleSystemGraphObject(ReferencedObject))
					{
						Resolver.AddObject(ReferencedObject);
					}
				}
			}
		}
	}

	void RebuildParticleSystemCaches(UParticleSystem* Asset)
	{
		if (!Asset)
		{
			return;
		}

		for (UParticleEmitter* Emitter : Asset->Emitters)
		{
			if (Emitter)
			{
				Emitter->CacheEmitterModuleInfo();
			}
		}
	}

	json::JSON BuildParticleSystemAssetJson(UParticleSystem* Asset, const FString& AssetPath)
	{
		json::JSON JsonData = json::JSON::Make(json::JSON::Class::Object);
		if (!Asset)
		{
			return JsonData;
		}

		if (!AssetPath.empty())
		{
			Asset->SetAssetPath(AssetPath);
		}

		FParticleSystemObjectGraphResolver Resolver;
		CollectParticleSystemObjectGraph(Asset, Resolver);

		JsonData["Format"] = "ParticleSystemAsset";
		JsonData["Version"] = 1;
		JsonData["AssetPath"] = AssetPath;
		JsonData["RootObjectId"] = static_cast<int32>(Resolver.GetObjectId(Asset));

		json::JSON ObjectsJson = json::JSON::Make(json::JSON::Class::Array);
		const TArray<UObject*>& Objects = Resolver.GetObjects();
		for (int32 Index = 0; Index < static_cast<int32>(Objects.size()); ++Index)
		{
			UObject* Object = Objects[Index];
			if (!IsParticleSystemGraphObject(Object))
			{
				continue;
			}

			json::JSON ObjectNode = json::JSON::Make(json::JSON::Class::Object);
			ObjectNode["Id"] = Index + 1;
			ObjectNode["Class"] = Object->GetClassName();

			json::JSON ObjectData = json::JSON::Make(json::JSON::Class::Object);
			FJsonWriter Writer(ObjectData);
			Writer.SetObjectResolver(&Resolver);
			Object->Serialize(Writer);
			ObjectNode["Data"] = ObjectData;
			ObjectsJson.append(ObjectNode);
		}
		JsonData["Objects"] = ObjectsJson;
		return JsonData;
	}

	bool SerializeParticleSystemObjectGraph(FArchive& Ar, UParticleSystem* Asset, const FString& AssetPath)
	{
		if (!Asset)
		{
			return false;
		}

		if (!AssetPath.empty())
		{
			Asset->SetAssetPath(AssetPath);
		}

		FParticleSystemObjectGraphResolver Resolver;
		CollectParticleSystemObjectGraph(Asset, Resolver);
		Ar.SetObjectResolver(&Resolver);

		int32 Version = 1;
		int32 RootObjectId = static_cast<int32>(Resolver.GetObjectId(Asset));
		TArray<int32> ObjectIds;
		TArray<FString> ClassNames;

		const TArray<UObject*>& Objects = Resolver.GetObjects();
		ObjectIds.reserve(Objects.size());
		ClassNames.reserve(Objects.size());
		for (int32 Index = 0; Index < static_cast<int32>(Objects.size()); ++Index)
		{
			UObject* Object = Objects[Index];
			if (!IsParticleSystemGraphObject(Object))
			{
				return false;
			}

			ObjectIds.push_back(Index + 1);
			ClassNames.push_back(Object->GetClassName());
		}

		FString StoredAssetPath = AssetPath;
		Ar << "Version" << Version;
		Ar << "AssetPath" << StoredAssetPath;
		Ar << "RootObjectId" << RootObjectId;
		Ar << "ObjectIds" << ObjectIds;
		Ar << "ClassNames" << ClassNames;

		for (UObject* Object : Objects)
		{
			if (!IsParticleSystemGraphObject(Object))
			{
				return false;
			}

			Ar.BeginObject("ObjectData");
			Object->Serialize(Ar);
			Ar.EndObject();
		}

		Ar.SetObjectResolver(nullptr);
		return true;
	}

	UParticleSystem* LoadParticleSystemObjectGraph(FArchive& Ar, const FString& SourceLabel)
	{
		FParticleSystemObjectGraphResolver Resolver;
		int32 Version = 0;
		FString EmbeddedAssetPath;
		int32 RootObjectId = 1;
		TArray<int32> ObjectIds;
		TArray<FString> ClassNames;

		Ar << "Version" << Version;
		Ar << "AssetPath" << EmbeddedAssetPath;
		Ar << "RootObjectId" << RootObjectId;
		Ar << "ObjectIds" << ObjectIds;
		Ar << "ClassNames" << ClassNames;

		if (Version <= 0 || ObjectIds.size() != ClassNames.size() || ObjectIds.empty())
		{
			UE_LOG_ERROR("[ParticleSystemAsset] Invalid object graph header: %s", SourceLabel.c_str());
			return nullptr;
		}

		for (int32 Index = 0; Index < static_cast<int32>(ObjectIds.size()); ++Index)
		{
			const uint32 Id = static_cast<uint32>(ObjectIds[Index]);
			UClass* Class = FReflectionRegistry::Get().FindClass(ClassNames[Index]);
			if (Id == 0 || !IsParticleSystemGraphClass(Class))
			{
				UE_LOG_ERROR("[ParticleSystemAsset] Invalid object class '%s': %s", ClassNames[Index].c_str(), SourceLabel.c_str());
				return nullptr;
			}

			UObject* Object = NewObject(Class);
			if (!Object)
			{
				UE_LOG_ERROR("[ParticleSystemAsset] Failed to create object '%s': %s", ClassNames[Index].c_str(), SourceLabel.c_str());
				return nullptr;
			}

			Resolver.SetObject(Id, Object);
		}

		Ar.SetObjectResolver(&Resolver);
		for (int32 Index = 0; Index < static_cast<int32>(ObjectIds.size()); ++Index)
		{
			UObject* Object = Resolver.ResolveObjectId(static_cast<uint32>(ObjectIds[Index]), nullptr);
			if (!Object)
			{
				continue;
			}

			Ar.BeginObject("ObjectData");
			Object->Serialize(Ar);
			Ar.EndObject();
		}
		Ar.SetObjectResolver(nullptr);

		UParticleSystem* Asset = Cast<UParticleSystem>(Resolver.ResolveObjectId(static_cast<uint32>(RootObjectId), UParticleSystem::StaticClass()));
		if (!Asset)
		{
			UE_LOG_ERROR("[ParticleSystemAsset] Missing root particle system: %s", SourceLabel.c_str());
			return nullptr;
		}

		RebuildParticleSystemCaches(Asset);
		if (!EmbeddedAssetPath.empty() && IsParticleSystemAssetPath(EmbeddedAssetPath))
		{
			Asset->SetAssetPath(EmbeddedAssetPath);
		}
		return Asset;
	}

	UParticleSystem* LoadParticleSystemFromJson(json::JSON& JsonData, const FString& SourceLabel)
	{
		if (JsonData.JSONType() != json::JSON::Class::Object)
		{
			UE_LOG_ERROR("[ParticleSystemAsset] Invalid json: %s", SourceLabel.c_str());
			return nullptr;
		}

		const FString EmbeddedAssetPath = JsonData.hasKey("AssetPath")
			? FPaths::Normalize(JsonData["AssetPath"].ToString())
			: FString();

		if (JsonData.hasKey("Objects") && JsonData["Objects"].JSONType() == json::JSON::Class::Array)
		{
			FParticleSystemObjectGraphResolver Resolver;
			json::JSON& ObjectsJson = JsonData["Objects"];

			for (int32 Index = 0; Index < static_cast<int32>(ObjectsJson.length()); ++Index)
			{
				json::JSON& ObjectNode = ObjectsJson.at(Index);
				if (ObjectNode.JSONType() != json::JSON::Class::Object)
				{
					continue;
				}

				const uint32 Id = ObjectNode.hasKey("Id") ? static_cast<uint32>(ObjectNode["Id"].ToInt()) : static_cast<uint32>(Index + 1);
				const FString ClassName = ObjectNode.hasKey("Class")
					? ObjectNode["Class"].ToString()
					: (ObjectNode.hasKey("Data") && ObjectNode["Data"].hasKey("Type") ? ObjectNode["Data"]["Type"].ToString() : FString());
				UClass* Class = FReflectionRegistry::Get().FindClass(ClassName);
				if (!IsParticleSystemGraphClass(Class))
				{
					UE_LOG_ERROR("[ParticleSystemAsset] Invalid object class '%s': %s", ClassName.c_str(), SourceLabel.c_str());
					return nullptr;
				}

				UObject* Object = NewObject(Class);
				if (!Object)
				{
					UE_LOG_ERROR("[ParticleSystemAsset] Failed to create object '%s': %s", ClassName.c_str(), SourceLabel.c_str());
					return nullptr;
				}

				Resolver.SetObject(Id, Object);
			}

			for (int32 Index = 0; Index < static_cast<int32>(ObjectsJson.length()); ++Index)
			{
				json::JSON& ObjectNode = ObjectsJson.at(Index);
				if (ObjectNode.JSONType() != json::JSON::Class::Object)
				{
					continue;
				}

				const uint32 Id = ObjectNode.hasKey("Id") ? static_cast<uint32>(ObjectNode["Id"].ToInt()) : static_cast<uint32>(Index + 1);
				UObject* Object = Resolver.ResolveObjectId(Id, nullptr);
				if (!Object)
				{
					continue;
				}

				json::JSON& ObjectData = ObjectNode.hasKey("Data") ? ObjectNode["Data"] : ObjectNode;
				FJsonReader Reader(ObjectData);
				Reader.SetObjectResolver(&Resolver);
				Object->Serialize(Reader);
			}

			const uint32 RootObjectId = JsonData.hasKey("RootObjectId") ? static_cast<uint32>(JsonData["RootObjectId"].ToInt()) : 1;
			UParticleSystem* Asset = Cast<UParticleSystem>(Resolver.ResolveObjectId(RootObjectId, UParticleSystem::StaticClass()));
			if (!Asset)
			{
				UE_LOG_ERROR("[ParticleSystemAsset] Missing root particle system: %s", SourceLabel.c_str());
				return nullptr;
			}

			RebuildParticleSystemCaches(Asset);
			if (!EmbeddedAssetPath.empty() && IsParticleSystemAssetPath(EmbeddedAssetPath))
			{
				Asset->SetAssetPath(EmbeddedAssetPath);
			}
			return Asset;
		}

		UParticleSystem* Asset = UObjectManager::Get().CreateObject<UParticleSystem>();
		if (!Asset)
		{
			UE_LOG_ERROR("[ParticleSystemAsset] Failed to create asset object: %s", SourceLabel.c_str());
			return nullptr;
		}

		FJsonReader Reader(JsonData);
		Asset->Serialize(Reader);
		RebuildParticleSystemCaches(Asset);
		if (!EmbeddedAssetPath.empty() && IsParticleSystemAssetPath(EmbeddedAssetPath))
		{
			Asset->SetAssetPath(EmbeddedAssetPath);
		}
		return Asset;
	}

	FString MakeProjectRelativePath(const FString& Path)
	{
		const FString NormalizedPath = FPaths::Normalize(Path);
		if (NormalizedPath.empty())
		{
			return {};
		}

		std::filesystem::path FsPath(FPaths::ToWide(NormalizedPath));
		if (!FsPath.is_absolute())
		{
			return NormalizedPath;
		}

		std::error_code ErrorCode;
		const std::filesystem::path RelativePath = std::filesystem::relative(FsPath, std::filesystem::path(FPaths::RootDir()), ErrorCode);
		if (ErrorCode || RelativePath.empty())
		{
			return NormalizedPath;
		}

		const std::wstring RelativeText = RelativePath.generic_wstring();
		if (RelativeText == L".." || RelativeText.rfind(L"../", 0) == 0)
		{
			return NormalizedPath;
		}

		return FPaths::Normalize(FPaths::ToUtf8(RelativeText));
	}

	bool LoadAnimSequenceUAssetPayload(const FString& Path, FAssetMetaData& OutMetaData, FAnimSequenceAssetPayload& OutPayload)
	{
		if (!FAssetFile::IsAssetPath(Path))
		{
			return false;
		}

		const bool bLoaded = FAssetFile::Load(Path, OutMetaData, [&](FArchive& Ar)
		{
			OutPayload.Serialize(Ar, OutMetaData.PayloadVersion);
			return true;
		});

		return bLoaded && OutMetaData.ClassName == UAnimSequence::StaticClass()->GetName();
	}

	UAnimSequence* CreateAnimSequenceFromUAssetPayload(
		const FString& Path,
		const FAssetMetaData& MetaData,
		FAnimSequenceAssetPayload& Payload)
	{
		UAnimSequence* Sequence = UObjectManager::Get().CreateObject<UAnimSequence>();
		if (!Sequence)
		{
			return nullptr;
		}

		Sequence->SetAssetPath(Path);
		Sequence->SetSourceFilePath(MetaData.SourceFile);
		Sequence->SetSourceStackName(Payload.SourceStackName);
		Sequence->SetPreviewMeshPath(Payload.TargetSkeletalMeshPath);
		Sequence->SetDataModel(Payload.DataModel);
		Sequence->ClearNotifies();
		if (!Payload.NotifyTracks.empty())
		{
			Sequence->SetNotifyTracks(Payload.NotifyTracks);
		}
		else
		{
			for (const FAnimNotifyStateEvent& Notify : Payload.Notifies)
			{
				Sequence->AddNotifyEvent(0, Notify);
			}
		}
		return Sequence;
	}

	
}

#pragma region __BINARY__

namespace fs = std::filesystem;

uint64 FResourceManager::GetFileWriteTimeTicks(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	fs::path FilePath(FPaths::ToAbsolute(FPaths::ToWide(NormalizedPath)));
	std::error_code ErrorCode;
	if (!fs::exists(FilePath, ErrorCode) || ErrorCode)
	{
		return 0;
	}

	auto WriteTime = fs::last_write_time(FilePath, ErrorCode);
	if (ErrorCode)
	{
		return 0;
	}

	auto Duration = WriteTime.time_since_epoch();

	return static_cast<uint64>(
		std::chrono::duration_cast<std::chrono::seconds>(Duration).count());
}

uint64 FResourceManager::GetFileSizeBytes(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	fs::path FilePath(FPaths::ToAbsolute(FPaths::ToWide(NormalizedPath)));

	std::error_code ErrorCode;
	const uintmax_t FileSize = fs::file_size(FilePath, ErrorCode);
	if (ErrorCode)
	{
		return 0;
	}

	return static_cast<uint64>(FileSize);
}

FString FResourceManager::ComputeFileContentHashString(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	std::ifstream In(FPaths::ToAbsolute(FPaths::ToWide(NormalizedPath)), std::ios::binary);
	if (!In.is_open())
	{
		return "";
	}

	constexpr uint64 FnvOffsetBasis = 14695981039346656037ull;
	constexpr uint64 FnvPrime = 1099511628211ull;

	uint64 Hash = FnvOffsetBasis;
	char Buffer[64 * 1024];
	while (In.good())
	{
		In.read(Buffer, sizeof(Buffer));
		const std::streamsize BytesRead = In.gcount();
		for (std::streamsize Index = 0; Index < BytesRead; ++Index)
		{
			Hash ^= static_cast<unsigned char>(Buffer[Index]);
			Hash *= FnvPrime;
		}
	}

	char HashText[32] = {};
	std::snprintf(HashText, sizeof(HashText), "fnv1a64:%016llx", static_cast<unsigned long long>(Hash));
	return FString(HashText);
}

FString FResourceManager::GetCachedFileContentHashString(const FString& Path, uint64 WriteTimeTicks, uint64 FileSizeBytes)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	const FString CacheKey =
		NormalizedPath + "#" + std::to_string(WriteTimeTicks) + ":" + std::to_string(FileSizeBytes);

	auto Found = FileContentHashCache.find(CacheKey);
	if (Found != FileContentHashCache.end())
	{
		return Found->second;
	}

	const FString Hash = ComputeFileContentHashString(NormalizedPath);
	if (!Hash.empty())
	{
		FileContentHashCache[CacheKey] = Hash;
	}
	return Hash;
}

bool FResourceManager::IsStaticMeshBinaryValid(const FString& SourcePath, const FString& BinaryPath) const
{
	FStaticMeshBinaryHeader Header;
	const FString NormalizedBinaryPath = FPaths::Normalize(BinaryPath);
	if (!BinarySerializer.ReadStaticMeshHeader(NormalizedBinaryPath, Header))
	{
		return false;
	}

	const uint64 SourceWriteTime = GetFileWriteTimeTicks(FPaths::Normalize(SourcePath));
	if (SourceWriteTime == 0)
	{
		return false;
	}

	return Header.SourceFileWriteTime == SourceWriteTime;
}

bool FResourceManager::IsSkeletalMeshBinaryValid(const FString& SourcePath, const FString& BinaryPath) const
{
	FSkeletalMeshBinaryHeader Header;
	const FString NormalizedBinaryPath = FPaths::Normalize(BinaryPath);
	if (!BinarySerializer.ReadSkeletalMeshHeader(NormalizedBinaryPath, Header))
	{
		return false;
	}

	const uint64 SourceWriteTime = GetFileWriteTimeTicks(FPaths::Normalize(SourcePath));
	if (SourceWriteTime == 0)
	{
		return false;
	}

	return Header.SourceFileWriteTime == SourceWriteTime;
}

void FResourceManager::PreloadStaticMeshes()
{
	for (const auto& [Key, Resource] : StaticMeshCache.GetRegistry())
	{
		if (!Resource.bPreload)
		{
			continue;
		}

		if (LoadStaticMesh(Resource.Path) == nullptr)
		{
			UE_LOG_WARNING("Failed to load static mesh from Resource.ini: %s", Resource.Path.c_str());
		}
	}
}

UStaticMesh* FResourceManager::CreateStaticMeshFromLoadedData(FStaticMesh* LoadedMeshData, const FString& LogPath, bool bLogLodTiming, bool bLogLodSkipped) const
{
	UStaticMesh* LoadedMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
	LoadedMesh->SetMeshData(LoadedMeshData);

	if (ShouldBuildStaticMeshLODs())
	{
		if (bLogLodTiming)
		{
			const auto LodStart = std::chrono::steady_clock::now();
			FStaticMeshSimplifier::BuildLODs(LoadedMesh);
			const auto LodEnd = std::chrono::steady_clock::now();
			double LodSec = std::chrono::duration<double>(LodEnd - LodStart).count();
			UE_LOG("[StaticMeshLoad] Generated %d LODs for %s in %.3f sec",
				   LoadedMesh->GetValidLODCount(), LogPath.c_str(), LodSec);
		}
		else
		{
			FStaticMeshSimplifier::BuildLODs(LoadedMesh);
		}
	}
	else if (bLogLodSkipped)
	{
		UE_LOG_WARNING("[StaticMeshLoad] LOD generation skipped for %s (Enable LOD is off)", LogPath.c_str());
	}

	return LoadedMesh;
}

#pragma endregion


void FResourceManager::ClearDiscoveredResourceLists(bool bClearAtlasCache)
{
	ObjFilePaths.clear();
	FontFilePaths.clear();
	TextureFilePaths.clear();
	MaterialFilePaths.clear();
	SubUVFilePaths.clear();
	CurveFilePaths.clear();
	SkeletalMeshFilePaths.clear();
	AnimSequenceFilePaths.clear();
	AnimationFbxSourceFilePaths.clear();
	FileContentHashCache.clear();
	StaticMeshCache.ClearRegistry();

	if (bClearAtlasCache)
	{
		AtlasCache.Clear();
	}
}

void FResourceManager::RegisterDiscoveredAssetFile(const std::filesystem::path& FilePath, const std::filesystem::path& ProjectRootPath)
{
	std::wstring Extension = FilePath.extension().wstring();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);

	if (Extension == L".meta" || Extension == L".bin")
	{
		return;
	}

	const FString RelativePath = FPaths::Normalize(FPaths::ToString(std::filesystem::relative(FilePath, ProjectRootPath)));

	if (Extension == L".uasset")
	{
		FAssetMetaData MetaData;
		if (FAssetFile::LoadMetadataOnly(RelativePath, MetaData))
		{
			if (MetaData.ClassName == UCurveFloatAsset::StaticClass()->GetName())
			{
				CurveFilePaths.push_back(RelativePath);
			}
			else if (MetaData.ClassName == UMaterial::StaticClass()->GetName() ||
				MetaData.ClassName == UMaterialInstance::StaticClass()->GetName())
			{
				MaterialFilePaths.push_back(RelativePath);
			}
			else if (MetaData.ClassName == UStaticMesh::StaticClass()->GetName())
			{
				ObjFilePaths.push_back(RelativePath);

				FStaticMeshResource Resource;
				Resource.Name = RelativePath;
				Resource.Path = RelativePath;
				Resource.bPreload = false;
				StaticMeshCache.RegisterResource(Resource);
			}
			else if (MetaData.ClassName == USkeletalMesh::StaticClass()->GetName())
			{
				SkeletalMeshFilePaths.push_back(RelativePath);
			}
			else if (MetaData.ClassName == UAnimSequence::StaticClass()->GetName())
			{
				AnimSequenceFilePaths.push_back(RelativePath);
			}
		}
		return;
	}

	if (FAssetPathPolicy::IsCurveAssetPath(FPaths::ToUtf8(FilePath.generic_wstring())))
	{
		CurveFilePaths.push_back(RelativePath);
	}
	else if (FAssetPathPolicy::IsAnimSequenceAssetPath(FPaths::ToUtf8(FilePath.generic_wstring())))
	{
		AnimSequenceFilePaths.push_back(RelativePath);
	}
	else if (Extension == L".obj" || Extension == L".fbx")
	{
		if (Extension == L".fbx")
		{
			if (std::find(AnimationFbxSourceFilePaths.begin(), AnimationFbxSourceFilePaths.end(), RelativePath) == AnimationFbxSourceFilePaths.end())
			{
				AnimationFbxSourceFilePaths.push_back(RelativePath);
			}

			std::wstring RelativeGenericPath = std::filesystem::path(FPaths::ToWide(RelativePath)).generic_wstring();
			std::transform(RelativeGenericPath.begin(), RelativeGenericPath.end(), RelativeGenericPath.begin(), ::towlower);

			const bool bUnderSkeletalMeshRoot = RelativeGenericPath.rfind(L"asset/skeletalmesh/", 0) == 0;
			const FString SkeletalBinaryPath = FAssetPathPolicy::MakeWritableSkeletalMeshCacheBinaryPath(RelativePath);
			const bool bHasValidSkeletalBinary = IsSkeletalMeshBinaryValid(RelativePath, SkeletalBinaryPath);
			if (bUnderSkeletalMeshRoot || bHasValidSkeletalBinary)
			{
				if (std::find(AnimationFbxSourceFilePaths.begin(), AnimationFbxSourceFilePaths.end(), RelativePath) == AnimationFbxSourceFilePaths.end())
				{
					AnimationFbxSourceFilePaths.push_back(RelativePath);
				}
			}
		}
	}
	else if (Extension == L".mtl")
	{
		MaterialFilePaths.push_back(RelativePath);
	}
	else if (Extension == L".png" || Extension == L".dds" || Extension == L".jpg" || Extension == L".jpeg")
	{
		const FTextureAssetMeta Meta = LoadOrCreateTextureMeta(FilePath);

		if (Meta.Type == EAssetMetaType::Font)
		{
			FontFilePaths.push_back(RelativePath);
			RegisterFont(FName(RelativePath.c_str()), RelativePath, Meta.Columns, Meta.Rows);
		}
		else if (Meta.Type == EAssetMetaType::SubUV)
		{
			SubUVFilePaths.push_back(RelativePath);
			RegisterSubUV(FName(RelativePath.c_str()), RelativePath, Meta.Columns, Meta.Rows);
		}
		else if (Meta.Type == EAssetMetaType::Texture)
		{
			TextureFilePaths.push_back(RelativePath);
		}
	}
}

void FResourceManager::InitializeDefaultWhiteTexture(ID3D11Device* Device)
{
	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width = 1;
	Desc.Height = 1;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_IMMUTABLE;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	constexpr uint32_t WhitePixel = 0xFFFFFFFF;
	D3D11_SUBRESOURCE_DATA InitData = {&WhitePixel, 4, 0};

	if (!TextureCache.Contains("DefaultWhite"))  {
		Device->CreateTexture2D(&Desc, &InitData, DefaultWhiteTexture.ReleaseAndGetAddressOf());
		if (DefaultWhiteTexture)
		{
			UTexture* DefaultTexture = UObjectManager::Get().CreateObject<UTexture>();
			Device->CreateShaderResourceView(DefaultWhiteTexture.Get(), nullptr, DefaultTexture->GetAddressOfSRV());
			TextureCache.Register("DefaultWhite", DefaultTexture);
		}
	}
}

void FResourceManager::InitializeDefaultMaterial(ID3D11Device* Device)
{
	UMaterial* DefaultMat = GetOrCreateMaterial("DefaultWhite", EMaterialShaderType::SurfaceLit);
	DefaultMat->MaterialParams["AmbientColor"] = FMaterialParamValue(DefaultMat->MaterialData.AmbientColor);
	DefaultMat->MaterialParams["DiffuseColor"] = FMaterialParamValue(DefaultMat->MaterialData.DiffuseColor);
	DefaultMat->MaterialParams["SpecularColor"] = FMaterialParamValue(DefaultMat->MaterialData.SpecularColor);
	DefaultMat->MaterialParams["EmissiveColor"] = FMaterialParamValue(DefaultMat->MaterialData.EmissiveColor);
	DefaultMat->MaterialParams["Shininess"] = FMaterialParamValue(DefaultMat->MaterialData.Shininess);
	DefaultMat->MaterialParams["Opacity"] = FMaterialParamValue(DefaultMat->MaterialData.Opacity);

	UTexture* DefaultWhite = GetTexture("DefaultWhite");

	if (DefaultMat->MaterialData.bHasDiffuseTexture)
		DefaultMat->MaterialParams["DiffuseMap"] = FMaterialParamValue(LoadTexture(DefaultMat->MaterialData.DiffuseTexPath, Device));
	else
		DefaultMat->MaterialParams["DiffuseMap"] = FMaterialParamValue(DefaultWhite);

	if (DefaultMat->MaterialData.bHasAmbientTexture)
		DefaultMat->MaterialParams["AmbientMap"] = FMaterialParamValue(LoadTexture(DefaultMat->MaterialData.AmbientTexPath, Device));
	else
		DefaultMat->MaterialParams["AmbientMap"] = FMaterialParamValue(DefaultWhite);

	if (DefaultMat->MaterialData.bHasSpecularTexture)
		DefaultMat->MaterialParams["SpecularMap"] = FMaterialParamValue(LoadTexture(DefaultMat->MaterialData.SpecularTexPath, Device));
	else
		DefaultMat->MaterialParams["SpecularMap"] = FMaterialParamValue(DefaultWhite);

	if (DefaultMat->MaterialData.bHasEmissiveTexture)
		DefaultMat->MaterialParams["EmissiveMap"] = FMaterialParamValue(LoadTexture(DefaultMat->MaterialData.EmissiveTexPath, Device));
	else
		DefaultMat->MaterialParams["EmissiveMap"] = FMaterialParamValue(DefaultWhite);

	if (DefaultMat->MaterialData.bHasBumpTexture)
		DefaultMat->MaterialParams["BumpMap"] = FMaterialParamValue(LoadTexture(DefaultMat->MaterialData.BumpTexPath, Device));
	else
		DefaultMat->MaterialParams["BumpMap"] = FMaterialParamValue(DefaultWhite);

	DefaultMat->MaterialParams["bHasDiffuseMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasDiffuseTexture);
	DefaultMat->MaterialParams["bHasSpecularMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasSpecularTexture);
	DefaultMat->MaterialParams["bHasAmbientMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasAmbientTexture);
	DefaultMat->MaterialParams["bHasEmissiveMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasEmissiveTexture);
	DefaultMat->MaterialParams["bHasBumpMap"] = FMaterialParamValue(DefaultMat->MaterialData.bHasBumpTexture);
	DefaultMat->MaterialParams["ScrollUV"] = FMaterialParamValue(FVector2(0.0f, 0.0f));
}

void FResourceManager::InitializeOutlineMaterial()
{
	UMaterial* OutlineMat = GetOrCreateMaterial("OutlineMaterial", EMaterialShaderType::EditorOutline);
	OutlineMat->SetParam("OutlineColor", FMaterialParamValue(FVector4(1.0f, 0.5f, 0.0f, 1.0f)));
	OutlineMat->SetParam("OutlineThicknessPixels", FMaterialParamValue(5.0f));
	OutlineMat->SetParam("OutlineViewportSize", FMaterialParamValue(FVector2(800.0f, 600.0f)));
	OutlineMat->SetParam("OutlineViewportOrigin", FMaterialParamValue(FVector2(0.0f, 0.0f)));
}

//	RootPath ??瑜곷쭊?????덈츎 嶺뚮ㅄ維獄??????띠럾???Asset??????琉우뿰 ?貫?껆뵳?????????⑤갭由????貫??
void FResourceManager::LoadFromAssetDirectory(const FString& Path)
{
	//	?貫?껆뵳??
	ClearDiscoveredResourceLists(false);

	InitializeDefaultResources(CachedDevice.Get());

	namespace fs = std::filesystem;
	
	const fs::path RootPath = fs::path(FPaths::RootDir()) / FPaths::ToWide(Path);
	
	const fs::path ProjectRootPath = fs::path(FPaths::RootDir());

	if (!fs::exists(RootPath) || !fs::is_directory(RootPath))
	{
		UE_LOG_ERROR("[ResourceManager] Fatal Error : Root Directory Error");
		return;
	}

	for (const auto& Entry : fs::recursive_directory_iterator(RootPath))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		RegisterDiscoveredAssetFile(Entry.path(), ProjectRootPath);
	}

	// Startup should only discover assets. FBX animation import/binary cache generation
	// is intentionally deferred until an animation asset is explicitly opened or requested.
	
	PreloadStaticMeshes();

	if (LoadGPUResources(CachedDevice.Get()))
	{
		UE_LOG("Complete Load Resources!");
	}
	else
	{
		UE_LOG_ERROR("Failed to Load Resources...");
	}
}

void FResourceManager::RefreshFromAssetDirectory(const FString& Path)
{
	namespace fs = std::filesystem;

	ClearDiscoveredResourceLists(true);

	const fs::path RootPath = fs::path(FPaths::RootDir()) / FPaths::ToWide(Path);
	const fs::path ProjectRootPath = fs::path(FPaths::RootDir());

	if (!fs::exists(RootPath) || !fs::is_directory(RootPath))
	{
		UE_LOG_ERROR("[ResourceManager] Refresh Failed : Root Directory Error");
		return;
	}

	try
	{
		for (const auto& Entry : fs::recursive_directory_iterator(RootPath, fs::directory_options::skip_permission_denied))
		{
			if (!Entry.is_regular_file())
			{
				continue;
			}

			RegisterDiscoveredAssetFile(Entry.path(), ProjectRootPath);
		}
	}
	catch (const std::exception& Ex)
	{
		UE_LOG_ERROR("[ResourceManager] Refresh Exception: %s", Ex.what());
	}

	if (CachedDevice && !LoadGPUResources(CachedDevice.Get()))
	{
		UE_LOG_ERROR("[ResourceManager] Refresh Failed : GPU Resource Reload Error");
	}

	UE_LOG("[ResourceManager] Asset Refresh Complete");
}

void FResourceManager::DeleteAllCacheFiles()
{
	namespace fs = std::filesystem;

	const fs::path BinRootPath = fs::path(FPaths::RootDir()) / "Asset" / "Mesh" / "Bin";

	if (!fs::exists(BinRootPath) || !fs::is_directory(BinRootPath))
	{
		return;
	}

	for (const auto& Entry : fs::recursive_directory_iterator(BinRootPath))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		const fs::path& FilePath = Entry.path();
		if (FilePath.extension() == L".bin")
		{
			std::error_code Ec;
			fs::remove(FilePath, Ec);
		}
	}

	// ????븐뼚???ル벣遊??筌먲퐘遊?
	for (auto It = fs::recursive_directory_iterator(BinRootPath);
		 It != fs::recursive_directory_iterator();
		 ++It)
	{
		std::error_code Ec;
		if (It->is_directory(Ec) && fs::is_empty(It->path(), Ec))
		{
			fs::remove(It->path(), Ec);
		}
	}

	UE_LOG("[ResourceManager] All mesh cache files removed");
}

FTextureAssetMeta FResourceManager::LoadOrCreateTextureMeta(const std::filesystem::path& FilePath) const
{
	return FTextureAssetMetaService::LoadOrCreate(FilePath);
}

bool FResourceManager::LoadGPUResources(ID3D11Device* Device)
{
	return AtlasCache.LoadGPUResources(Device);
}

void FResourceManager::InitializeDefaultResources(ID3D11Device* Device)
{
	if (!Device) return;

	InitializeDefaultWhiteTexture(Device);
	InitializeDefaultMaterial(Device);
	InitializeOutlineMaterial();
}

void FResourceManager::ReleaseGPUResources()
{
	TextureCache.Release();

	MaterialCache.Release();

	ShaderCache.Release();

	AtlasCache.Release();

	StaticMeshCache.Release();

	CurveCache.Release();

	RenderStateCache.Release();

	std::unordered_set<USkeletalMesh*> DestroyedSkeletalMeshes;
	for (auto& [Path, Mesh] : SkeletalMeshMap)
	{
		if (Mesh && DestroyedSkeletalMeshes.insert(Mesh).second)
		{
			UObjectManager::Get().DestroyObject(Mesh);
		}
	}
	SkeletalMeshMap.clear();

	for (auto& [Path, ParticleSystem] : ParticleSystemMap)
	{
		UObjectManager::Get().DestroyObject(ParticleSystem);
	}
	ParticleSystemMap.clear();

	DefaultWhiteTexture.Reset();
	CachedDevice.Reset();
}

FVertexShader* FResourceManager::GetOrCreateVertexShader(
	const FShaderStageKey& Key,
	const D3D_SHADER_MACRO* Defines,
	const FVertexLayoutDesc* VertexLayout)
{
	return ShaderCache.GetOrCreateVertexShader(Key, Defines, CachedDevice.Get(), VertexLayout);
}

FPixelShader* FResourceManager::GetOrCreatePixelShader(const FShaderStageKey& Key, const D3D_SHADER_MACRO* Defines)
{
	return ShaderCache.GetOrCreatePixelShader(Key, Defines, CachedDevice.Get());
}

FShaderProgram* FResourceManager::GetOrCreateShaderProgram(
	const FShaderStageKey& VSKey,
	const FShaderStageKey& PSKey,
	const D3D_SHADER_MACRO* VSDefines,
	const D3D_SHADER_MACRO* PSDefines,
	const FVertexLayoutDesc* VertexLayout)
{
	return ShaderCache.GetOrCreateProgram(VSKey, PSKey, VSDefines, PSDefines, CachedDevice.Get(), VertexLayout);
}

bool FResourceManager::LoadComputeShader(const FString& FilePath, const FString& EntryPoint,
										 const D3D_SHADER_MACRO* Defines, const FString& Key)
{
	return ShaderCache.LoadComputeShader(FilePath, EntryPoint, Defines, Key, CachedDevice.Get());
}

void FResourceManager::InvalidateShaderFile(const FString& FilePath)
{
	ShaderCache.InvalidateShaderFile(FilePath);
}

FComputeShader* FResourceManager::GetComputeShader(const FString& Key) const
{
	return ShaderCache.GetComputeShader(Key);
}

TArray<FString> FResourceManager::GetMaterialNames() const
{
	return MaterialCache.GetMaterialNames();
}

TArray<FString> FResourceManager::GetMaterialInterfaceNames() const
{
	return MaterialCache.GetMaterialInterfaceNames(MaterialFilePaths);
}

UMaterial* FResourceManager::GetMaterial(const FString& MaterialName) const
{
	return MaterialCache.GetMaterial(MaterialName);
}

// 嶺뚮씞?녻뚯궘??????怨몃턄 ?띠럾????띠룄????Material????諛댁뎽
UMaterial* FResourceManager::GetOrCreateMaterial(const FString& Path, EMaterialShaderType ShaderType)
{
	UMaterial* Material = GetMaterial(Path);
	if (Material)
	{
		return Material;
	}

	Material = UObjectManager::Get().CreateObject<UMaterial>();
	Material->Name = Path;
	Material->FilePath = Path;

	Material->SetShaderType(ShaderType);

	MaterialCache.RegisterMaterial(Path, Material);

	return Material;
}

UMaterial* FResourceManager::GetOrCreateMaterial(const FString& Name, const FString& Path, EMaterialShaderType ShaderType)
{
	UMaterial* Material = GetMaterial(Name);
	if (Material)
	{
		return Material;
	}

	Material = UObjectManager::Get().CreateObject<UMaterial>();
	Material->Name = Name;
	Material->FilePath = Path;

	Material->SetShaderType(ShaderType);

	MaterialCache.RegisterMaterial(Name, Material);

	return Material;
}

bool FResourceManager::LoadMaterial(const FString& MtlFilePath, EMaterialShaderType ShaderType, ID3D11Device* Device)
{
	return FMaterialLoadService(*this).Load(MtlFilePath, ShaderType, Device);
}

void FResourceManager::RegisterObjMaterialSlotAliases(const FString& ObjPath, const FString& MtlPath)
{
	const FString NormalizedObjPath = FPaths::Normalize(ObjPath);
	const FString NormalizedMtlPath = FPaths::Normalize(MtlPath);
	const TArray<FString> SlotNames = FImportedMaterialPolicy::CollectObjMaterialSlotNames(NormalizedObjPath);

	for (const FString& SlotName : SlotNames)
	{
		const FString* MtlAlias = MaterialCache.FindMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(NormalizedMtlPath, SlotName));
		if (MtlAlias)
		{
			MaterialCache.SetMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(NormalizedObjPath, SlotName), *MtlAlias);
		}
	}
}

UMaterial* FResourceManager::GetMaterialForStaticMeshSlot(const FString& SourcePath, const FString& SlotName) const
{
	if (!SourcePath.empty())
	{
		const FString* Alias = MaterialCache.FindMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(SourcePath, SlotName));
		if (Alias)
		{
			if (UMaterial* Material = GetMaterial(*Alias))
			{
				return Material;
			}
		}
	}

	return GetMaterial(SlotName);
}

void FResourceManager::ResolveStaticMeshMaterialSlots(const FString& SourcePath, FStaticMesh* StaticMesh) const
{
	if (!StaticMesh)
	{
		return;
	}

	for (FStaticMeshMaterialSlot& Slot : StaticMesh->Slots)
	{
		if (!SourcePath.empty())
		{
			const FString* Alias = MaterialCache.FindMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(SourcePath, Slot.SlotName));
			if (Alias)
			{
				Slot.SlotName = *Alias;
			}
		}

		Slot.Material = GetMaterialForStaticMeshSlot(SourcePath, Slot.SlotName);
		if (Slot.Material == nullptr)
		{
			Slot.Material = GetMaterial("DefaultWhite");
		}
	}
}

void FResourceManager::ResolveSkeletalMeshMaterialSlots(const FString& SourcePath, FSkeletalMesh* SkeletalMesh) const
{
	if (!SkeletalMesh)
	{
		return;
	}

	for (FStaticMeshMaterialSlot& Slot : SkeletalMesh->MaterialSlots)
	{
		if (!SourcePath.empty())
		{
			const FString* Alias = MaterialCache.FindMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(SourcePath, Slot.SlotName));
			if (Alias)
			{
				Slot.SlotName = *Alias;
			}
		}

		Slot.Material = GetMaterialForStaticMeshSlot(SourcePath, Slot.SlotName);
		if (Slot.Material == nullptr)
		{
			Slot.Material = GetMaterial("DefaultWhite");
		}
	}
}

UMaterialInstance* FResourceManager::CreateMaterialInstance(const FString& Path, UMaterial* Parent)
{
	return MaterialCache.CreateMaterialInstance(Path, Parent);
}

UMaterialInstance* FResourceManager::GetMaterialInstance(const FString& Path) const
{
	return MaterialCache.GetMaterialInstance(Path);
}

UMaterialInterface* FResourceManager::GetMaterialInterface(const FString& Name)
{
	UMaterial* Mat = GetMaterial(Name);
	if (Mat)
	{
		return Mat;
	}
	else if (Mat = GetMaterial(FPaths::Normalize(Name)))
	{
		return Mat;
	}
	else if (UMaterialInstance* MatInst = GetMaterialInstance(Name))
	{
		return MatInst;
	}
	if (UMaterialInstance* MatInst = GetMaterialInstance(FPaths::Normalize(Name)))
	{
		return MatInst;
	}

	const FString NormalizedName = FPaths::Normalize(Name);
	if (FAssetPathPolicy::IsSerializedMaterialAssetPath(NormalizedName) && FAssetPathPolicy::FileExists(NormalizedName))
	{
		if (DeserializeMaterial(NormalizedName))
		{
			if (UMaterial* LoadedMat = GetMaterial(NormalizedName))
			{
				return LoadedMat;
			}
			if (UMaterialInstance* LoadedMatInst = GetMaterialInstance(NormalizedName))
			{
				return LoadedMatInst;
			}
		}
	}

	return nullptr;
}

bool FResourceManager::SerializeMaterial(const FString& MatFilePath, const UMaterial* Material)
{
	return FMaterialSerializationService(*this).SerializeMaterial(MatFilePath, Material);
}

bool FResourceManager::SerializeMaterialInstance(const FString& MatInstFilePath, const UMaterialInstance* MaterialInstance)
{
	return FMaterialSerializationService(*this).SerializeMaterialInstance(MatInstFilePath, MaterialInstance);
}

bool FResourceManager::DeserializeMaterial(const FString& MatFilePath)
{
	return FMaterialSerializationService(*this).DeserializeMaterial(MatFilePath);
}

UTexture* FResourceManager::GetTexture(const FString& Path) const
{
	return TextureCache.Get(Path);
}

UTexture* FResourceManager::LoadTexture(const FString& Path, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}

	return TextureCache.Load(Path, Device);
}

// --- Font ---
FFontResource* FResourceManager::FindFont(const FName& FontName)
{
	return AtlasCache.FindFont(FontName);
}

const FFontResource* FResourceManager::FindFont(const FName& FontName) const
{
	return AtlasCache.FindFont(FontName);
}

void FResourceManager::RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	AtlasCache.RegisterFont(FontName, InPath, Columns, Rows);
}

// --- SubUV ---
FTextureAtlasResource* FResourceManager::FindSubUV(const FName& SubUVName)
{
	return AtlasCache.FindSubUV(SubUVName);
}

const FTextureAtlasResource* FResourceManager::FindSubUV(const FName& SubUVName) const
{
	return AtlasCache.FindSubUV(SubUVName);
}

FTextureAtlasResource* FResourceManager::FindSubUVExact(const FName& SubUVName)
{
	return AtlasCache.FindSubUVExact(SubUVName);
}

const FTextureAtlasResource* FResourceManager::FindSubUVExact(const FName& SubUVName) const
{
	return AtlasCache.FindSubUVExact(SubUVName);
}

void FResourceManager::RegisterSubUV(const FName& SubUVName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	AtlasCache.RegisterSubUV(SubUVName, InPath, Columns, Rows);
}

TArray<FString> FResourceManager::GetFontNames() const
{
	return FontFilePaths;
}

TArray<FString> FResourceManager::GetSubUVNames() const
{
	return SubUVFilePaths;
}

UStaticMesh* FResourceManager::LoadStaticMesh(const FString& Path)
{
	return FStaticMeshLoadService(*this).Load(Path);
}

UStaticMesh* FResourceManager::FindStaticMesh(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	return StaticMeshCache.Find(NormalizedPath);
}

TArray<FString> FResourceManager::GetStaticMeshPaths() const
{
	return ObjFilePaths;
}

FString FResourceManager::ImportStaticMeshFromSource(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!IsStaticMeshSourcePath(NormalizedPath))
	{
		return {};
	}

	const FString ImportedAssetPath = FAssetPathPolicy::MakeImportedStaticMeshAssetPath(NormalizedPath);
	if (FAssetPathPolicy::FileExists(ImportedAssetPath))
	{
		FAssetMetaData ExistingMetaData;
		if (FAssetFile::LoadMetadataOnly(ImportedAssetPath, ExistingMetaData) &&
			ExistingMetaData.ClassName == UStaticMesh::StaticClass()->GetName())
		{
			if (std::find(ObjFilePaths.begin(), ObjFilePaths.end(), ImportedAssetPath) == ObjFilePaths.end())
			{
				ObjFilePaths.push_back(ImportedAssetPath);
			}
			return ImportedAssetPath;
		}
	}

	UStaticMesh* Mesh = LoadStaticMesh(NormalizedPath);
	if (!Mesh || !Mesh->HasValidMeshData() || !Mesh->GetMeshData())
	{
		UE_LOG_WARNING("[StaticMeshImport] Failed to load source static mesh: %s", NormalizedPath.c_str());
		return {};
	}

	FStaticMesh* MeshData = Mesh->GetMeshData();
	MeshData->PathFileName = ImportedAssetPath;

	FAssetMetaData MetaData;
	MetaData.Version = 1;
	MetaData.PayloadVersion = 1;
	MetaData.ClassName = UStaticMesh::StaticClass()->GetName();
	MetaData.DisplayName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(ImportedAssetPath)).stem().wstring());
	MetaData.SourceFile = MakeProjectRelativePath(NormalizedPath);

	if (!FAssetFile::Save(ImportedAssetPath, MetaData, [&](FArchive& Ar)
	{
		MeshData->Serialize(Ar, MetaData.PayloadVersion);
		return true;
	}))
	{
		UE_LOG_WARNING("[StaticMeshImport] Failed to save static mesh asset: %s", ImportedAssetPath.c_str());
		return {};
	}

	StaticMeshCache.RegisterLoaded(ImportedAssetPath, Mesh);
	if (std::find(ObjFilePaths.begin(), ObjFilePaths.end(), ImportedAssetPath) == ObjFilePaths.end())
	{
		ObjFilePaths.push_back(ImportedAssetPath);
	}

	UE_LOG("[StaticMeshImport] Imported source static mesh: %s -> %s",
		NormalizedPath.c_str(),
		ImportedAssetPath.c_str());
	return ImportedAssetPath;
}

USkeletalMesh* FResourceManager::LoadSkeletalMesh(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	USkeletalMesh* Mesh = FSkeletalMeshLoadService(*this).Load(NormalizedPath);

	//일단 최적화를 위해 anime stack 훑어보는 과정은 LoadAnimSequence(FBX 경로) 에서만...
	//단순히 fbx 내부를 보는 것만으로도 오래 걸림.
	return Mesh;
}

USkeletalMesh* FResourceManager::FindSkeletalMesh(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);

	auto It = SkeletalMeshMap.find(NormalizedPath);
	if (It != SkeletalMeshMap.end())
	{
		return It->second;
	}

	return nullptr;
}

TArray<FString> FResourceManager::GetSkeletalMeshPaths() const
{
	return SkeletalMeshFilePaths;
}

FFbxMeshContentInfo FResourceManager::InspectFbxMeshContent(const FString& Path)
{
	return FbxImporter.InspectMeshContent(Path);
}

FString FResourceManager::ImportSkeletalMeshFromSource(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!IsFbxSourcePath(NormalizedPath))
	{
		return {};
	}

	const FString ImportedAssetPath = FAssetPathPolicy::MakeImportedSkeletalMeshAssetPath(NormalizedPath);
	if (FAssetPathPolicy::FileExists(ImportedAssetPath))
	{
		FAssetMetaData ExistingMetaData;
		if (FAssetFile::LoadMetadataOnly(ImportedAssetPath, ExistingMetaData) &&
			ExistingMetaData.ClassName == USkeletalMesh::StaticClass()->GetName())
		{
			if (std::find(SkeletalMeshFilePaths.begin(), SkeletalMeshFilePaths.end(), ImportedAssetPath) == SkeletalMeshFilePaths.end())
			{
				SkeletalMeshFilePaths.push_back(ImportedAssetPath);
			}
			return ImportedAssetPath;
		}
	}

	USkeletalMesh* Mesh = LoadSkeletalMesh(NormalizedPath);
	if (!Mesh || !Mesh->HasValidMeshData() || !Mesh->GetMeshData())
	{
		UE_LOG_WARNING("[SkeletalMeshImport] Failed to load source skeletal mesh: %s", NormalizedPath.c_str());
		return {};
	}

	FSkeletalMesh* MeshData = Mesh->GetMeshData();
	MeshData->PathFileName = ImportedAssetPath;

	FAssetMetaData MetaData;
	MetaData.Version = 1;
	MetaData.PayloadVersion = 1;
	MetaData.ClassName = USkeletalMesh::StaticClass()->GetName();
	MetaData.DisplayName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(ImportedAssetPath)).stem().wstring());
	MetaData.SourceFile = MakeProjectRelativePath(NormalizedPath);

	if (!FAssetFile::Save(ImportedAssetPath, MetaData, [&](FArchive& Ar)
	{
		MeshData->Serialize(Ar, MetaData.PayloadVersion);
		return true;
	}))
	{
		UE_LOG_WARNING("[SkeletalMeshImport] Failed to save skeletal mesh asset: %s", ImportedAssetPath.c_str());
		return {};
	}

	SkeletalMeshMap[ImportedAssetPath] = Mesh;
	if (std::find(SkeletalMeshFilePaths.begin(), SkeletalMeshFilePaths.end(), ImportedAssetPath) == SkeletalMeshFilePaths.end())
	{
		SkeletalMeshFilePaths.push_back(ImportedAssetPath);
	}

	UE_LOG("[SkeletalMeshImport] Imported source skeletal mesh: %s -> %s",
		NormalizedPath.c_str(),
		ImportedAssetPath.c_str());
	return ImportedAssetPath;
}

bool FResourceManager::SaveSkeletalMesh(USkeletalMesh* Mesh)
{
	if (!Mesh) return false;
	FSkeletalMesh* Data = Mesh->GetMeshData();
	if (!Data) return false;

	const FString AssetPath = FPaths::Normalize(Mesh->GetAssetPathFileName());
	if (AssetPath.empty()) return false;

	if (!FAssetFile::IsAssetPath(AssetPath))
	{
		UE_LOG_WARNING("[SkeletalMeshSave] Only .uasset skeletal meshes can be saved: %s", AssetPath.c_str());
		return false;
	}

	FAssetMetaData MetaData;
	if (!FAssetFile::LoadMetadataOnly(AssetPath, MetaData))
	{
		MetaData.Version = 1;
		MetaData.PayloadVersion = 1;
		MetaData.ClassName = USkeletalMesh::StaticClass()->GetName();
		MetaData.DisplayName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(AssetPath)).stem().wstring());
		MetaData.SourceFile.clear();
	}

	if (MetaData.ClassName != USkeletalMesh::StaticClass()->GetName())
	{
		UE_LOG_WARNING("[SkeletalMeshSave] Asset is not SkeletalMesh | Path=%s | Class=%s",
			AssetPath.c_str(),
			MetaData.ClassName.c_str());
		return false;
	}

	Data->PathFileName = AssetPath;
	return FAssetFile::Save(AssetPath, MetaData, [&](FArchive& Ar)
	{
		Data->Serialize(Ar, MetaData.PayloadVersion);
		return true;
	});
}

UCurveFloatAsset* FResourceManager::LoadCurve(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	UCurveFloatAsset* Curve = CurveCache.Load(NormalizedPath);
	if (!Curve)
	{
		return nullptr;
	}

	if (std::find(CurveFilePaths.begin(), CurveFilePaths.end(), NormalizedPath) == CurveFilePaths.end())
	{
		CurveFilePaths.push_back(NormalizedPath);
	}

	return Curve;
}

UCurveFloatAsset* FResourceManager::FindCurve(const FString& Path) const
{
	return CurveCache.Find(Path);
}

bool FResourceManager::SaveCurve(const FString& Path, const UCurveFloatAsset* Curve)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!CurveCache.Save(NormalizedPath, Curve))
	{
		return false;
	}

	if (std::find(CurveFilePaths.begin(), CurveFilePaths.end(), NormalizedPath) == CurveFilePaths.end())
	{
		CurveFilePaths.push_back(NormalizedPath);
	}

	return true;
}

TArray<FString> FResourceManager::GetCurvePaths() const
{
	return CurveFilePaths;
}

void FResourceManager::WarmUpAnimationPreviewMeshCaches(const TArray<FString>& AnimSequenceAssetPaths)
{
	TArray<FString> PreviewMeshPaths;
	PreviewMeshPaths.reserve(AnimSequenceAssetPaths.size());

	for (const FString& AnimSequenceAssetPath : AnimSequenceAssetPaths)
	{
		FString PreviewMeshPath;
		if (!FAssetFile::IsAssetPath(AnimSequenceAssetPath))
		{
			continue;
		}

		FAssetMetaData AssetMetaData;
		FAnimSequenceAssetPayload Payload;
		if (!LoadAnimSequenceUAssetPayload(AnimSequenceAssetPath, AssetMetaData, Payload))
		{
			continue;
		}

		PreviewMeshPath = FPaths::Normalize(Payload.TargetSkeletalMeshPath);
		if (PreviewMeshPath.empty())
		{
			PreviewMeshPath = FPaths::Normalize(AssetMetaData.SourceFile);
		}

		if (PreviewMeshPath.empty())
		{
			continue;
		}

		if (std::find(PreviewMeshPaths.begin(), PreviewMeshPaths.end(), PreviewMeshPath) == PreviewMeshPaths.end())
		{
			PreviewMeshPaths.push_back(PreviewMeshPath);
		}
	}

	int32 WarmedCount = 0;
	for (const FString& PreviewMeshPath : PreviewMeshPaths)
	{
		if (EnsureSkeletalMeshCacheForAnimationPreview(PreviewMeshPath))
		{
			++WarmedCount;
		}
	}

	if (!PreviewMeshPaths.empty())
	{
		UE_LOG("[AnimSequenceStartupImport] Warmed animation preview mesh caches: Requested=%d Ready=%d",
			static_cast<int32>(PreviewMeshPaths.size()),
			WarmedCount);
	}
}

bool FResourceManager::EnsureSkeletalMeshCacheForAnimationPreview(const FString& PreviewMeshPath)
{
	const FString NormalizedPreviewMeshPath = FPaths::Normalize(PreviewMeshPath);
	if (NormalizedPreviewMeshPath.empty() || !IsFbxSourcePath(NormalizedPreviewMeshPath))
	{
		return false;
	}

	if (FindSkeletalMesh(NormalizedPreviewMeshPath))
	{
		return true;
	}

	const FString BinaryPath = FAssetPathPolicy::MakeWritableSkeletalMeshCacheBinaryPath(NormalizedPreviewMeshPath);
	if (IsSkeletalMeshBinaryValid(NormalizedPreviewMeshPath, BinaryPath))
	{
		return true;
	}

	const FFbxMeshContentInfo ContentInfo = InspectFbxMeshContent(NormalizedPreviewMeshPath);
	if (!ContentInfo.bHasSkeletalMesh)
	{
		UE_LOG_WARNING("[AnimSequenceStartupImport] Preview mesh is not a skeletal FBX: %s",
			NormalizedPreviewMeshPath.c_str());
		return false;
	}

	return LoadSkeletalMesh(NormalizedPreviewMeshPath) != nullptr;
}

TArray<FString> FResourceManager::ImportAnimationStacksFromFbx(const FString& Path)
{
	TArray<FString> ImportedAssetPaths;
	TMap<FString, FString> ExistingAssetPathByStackName;

	const FString NormalizedPath = FPaths::Normalize(Path);
	const FString StableSourcePath = MakeProjectRelativePath(NormalizedPath);
	if (!IsFbxSourcePath(NormalizedPath))
	{
		return ImportedAssetPaths;
	}

	// 1. 이미 임포트된 uasset이 있는지 확인 (메타데이터를 로드해 FBX 원본 경로 대조)
	for (const FString& AssetPath : AnimSequenceFilePaths)
	{
		if (!FAssetFile::IsAssetPath(AssetPath))
		{
			continue;
		}

		FAssetMetaData AssetMetaData;
		FAnimSequenceAssetPayload Payload;
		if (!LoadAnimSequenceUAssetPayload(AssetPath, AssetMetaData, Payload))
		{
			continue;
		}

		if (MakeProjectRelativePath(AssetMetaData.SourceFile) == StableSourcePath)
		{
			ImportedAssetPaths.push_back(AssetPath);
			if (!Payload.SourceStackName.empty())
			{
				ExistingAssetPathByStackName[Payload.SourceStackName] = AssetPath;
			}
		}
	}
	
	if (!ImportedAssetPaths.empty())
	{
		if (UAnimSequence* FirstSequence = FindAnimSequence(ImportedAssetPaths.front()))
		{
			AnimSequenceMap[NormalizedPath] = FirstSequence;
		}
		return ImportedAssetPaths;
	}
	
	// 2. 임포트된 에셋이 하나도 없는 경우에만 FBX 파싱 (최초 1회)
	FFbxAnimImportOptions ImportOptions;
	ImportOptions.PreviewMeshPath = NormalizedPath;

	// LoadAnimSequences가 FBX를 한 번 열어서 stack 순회와 sequence 생성을 같이 처리한다.
	TArray<FFbxAnimStackImportResult> ImportResults = FbxImporter.LoadAnimSequences(NormalizedPath, ImportOptions);
	
	for (const FFbxAnimStackImportResult& Result : ImportResults)
	{
		if (!Result.Sequence || Result.StackName.empty())
		{
			continue;
		}

		FString ImportedAssetPath = FAssetPathPolicy::MakeImportedAnimSequenceAssetPath(NormalizedPath, Result.StackName);
		auto ExistingPathIt = ExistingAssetPathByStackName.find(Result.StackName);
		if (ExistingPathIt != ExistingAssetPathByStackName.end())
		{
			ImportedAssetPath = ExistingPathIt->second;
		}
		
		Result.Sequence->SetAssetPath(ImportedAssetPath);
		Result.Sequence->SetPreviewMeshPath(NormalizedPath);

		if (SaveAnimSequence(ImportedAssetPath, Result.Sequence))
		{
			AnimSequenceMap[ImportedAssetPath] = Result.Sequence;
			
			if (std::find(AnimSequenceFilePaths.begin(), AnimSequenceFilePaths.end(), ImportedAssetPath) == AnimSequenceFilePaths.end())
			{
				AnimSequenceFilePaths.push_back(ImportedAssetPath);
			}

			if (std::find(ImportedAssetPaths.begin(), ImportedAssetPaths.end(), ImportedAssetPath) == ImportedAssetPaths.end())
			{
				ImportedAssetPaths.push_back(ImportedAssetPath);
			}
			
			UE_LOG("[AnimSequenceImport] Imported FBX animation stack: %s | Stack=%s | Asset=%s",
				NormalizedPath.c_str(),
				Result.StackName.c_str(),
				ImportedAssetPath.c_str());
		}
		else
		{
			UE_LOG_WARNING("[AnimSequenceImport] Failed to save imported animation stack: %s -> %s",
				NormalizedPath.c_str(),
				ImportedAssetPath.c_str());
		}
	}

	if (!ImportedAssetPaths.empty())
	{
		EnsureSkeletalMeshCacheForAnimationPreview(NormalizedPath);

		if (UAnimSequence* FirstSequence = FindAnimSequence(ImportedAssetPaths.front()))
		{
			AnimSequenceMap[NormalizedPath] = FirstSequence;
		}
	}

	return ImportedAssetPaths;
}

UAnimSequence* FResourceManager::LoadAnimSequence(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);

	// 1. 메모리 캐시를 일단 확인해
	if (UAnimSequence* FoundSequence = FindAnimSequence(NormalizedPath))
	{
		return FoundSequence;
	}

	UAnimSequence* LoadedSequence = nullptr;

	// 2. 바이너리 에셋 경로라면 즉시 로드해
	if (FAssetFile::IsAssetPath(NormalizedPath))
	{
		FAssetMetaData MetaData;
		FAnimSequenceAssetPayload Payload;
		if (LoadAnimSequenceUAssetPayload(NormalizedPath, MetaData, Payload))
		{
			LoadedSequence = CreateAnimSequenceFromUAssetPayload(NormalizedPath, MetaData, Payload);
		}
		else if (!FAssetPathPolicy::FileExists(NormalizedPath))
		{
			for (const FString& SkeletalMeshPath : AnimationFbxSourceFilePaths)
			{
				if (!IsFbxSourcePath(SkeletalMeshPath))
				{
					continue;
				}

				const TArray<FString> ImportedAssetPaths = ImportAnimationStacksFromFbx(SkeletalMeshPath);
				if (std::find(ImportedAssetPaths.begin(), ImportedAssetPaths.end(), NormalizedPath) == ImportedAssetPaths.end())
				{
					continue;
				}

				if (UAnimSequence* RebuiltSequence = FindAnimSequence(NormalizedPath))
				{
					LoadedSequence = RebuiltSequence;
					break;
				}

				FAssetMetaData RebuiltMetaData;
				FAnimSequenceAssetPayload RebuiltPayload;
				if (LoadAnimSequenceUAssetPayload(NormalizedPath, RebuiltMetaData, RebuiltPayload))
				{
					LoadedSequence = CreateAnimSequenceFromUAssetPayload(NormalizedPath, RebuiltMetaData, RebuiltPayload);
					break;
				}
			}
		}
	}
	// 3. FBX 소스 경로인 경우라면 진짜 일을 시작함
	else if (IsFbxSourcePath(NormalizedPath))
	{
		//순회하며 animstack을 싹 다 가져옴
		const TArray<FString> ImportedAssetPaths = ImportAnimationStacksFromFbx(NormalizedPath);
		
		if (!ImportedAssetPaths.empty())
		{
			const FString& FirstAssetPath = ImportedAssetPaths.front();
			LoadedSequence = FindAnimSequence(FirstAssetPath);
			if (!LoadedSequence)
			{
				LoadedSequence = LoadAnimSequence(FirstAssetPath);
				if (LoadedSequence)
				{
					// FBX 경로로 요청해서 이미 생성된 animation asset을 읽은 경우,
					// source FBX key와 asset key 둘 다 같은 객체를 가리키게 해서 다음 요청에서 재로드하지 않는다.
					AnimSequenceMap[FirstAssetPath] = LoadedSequence;
				}
			}
		}
	}

	if (!LoadedSequence)
	{
		UE_LOG_WARNING("[AnimSequenceLoad] Failed to load anim sequence: %s", NormalizedPath.c_str());
		return nullptr;
	}

	// 4. 로드된 결과를 메모리 캐시에 등록
	AnimSequenceMap[NormalizedPath] = LoadedSequence;
	
	// 원래의 Asset Path 정보 보정
	if (LoadedSequence->GetAssetPath().empty())
	{
		LoadedSequence->SetAssetPath(NormalizedPath);
	}

	// 5. 관리 목록(FilePaths) 등록 최적화 (std::find 중복 방지를 위한 단순화)
	// 보통 엔진 시작 시 폴더를 훑어서 AnimSequenceFilePaths를 다 채워놓으므로, 
	// 런타임에 동적으로 파일이 생기는 게 아니라면 이 과정은 이미 되어있을 확률이 높습니다.
	if (std::find(AnimSequenceFilePaths.begin(), AnimSequenceFilePaths.end(), NormalizedPath) == AnimSequenceFilePaths.end())
	{
		AnimSequenceFilePaths.push_back(NormalizedPath);
	}

	return LoadedSequence;
}

bool FResourceManager::SaveAnimSequence(const FString& Path, const UAnimSequence* Sequence)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!Sequence)
	{
		return false;
	}

	if (FAssetFile::IsAssetPath(NormalizedPath))
	{
		FAssetMetaData MetaData;
		MetaData.Version = 1;
		MetaData.PayloadVersion = 4;
		MetaData.ClassName = UAnimSequence::StaticClass()->GetName();
		MetaData.SourceFile = MakeProjectRelativePath(Sequence->GetSourceFilePath());
		MetaData.DisplayName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(NormalizedPath)).stem().wstring());

		FAnimSequenceAssetPayload Payload;
		Payload.TargetSkeletalMeshPath = MakeProjectRelativePath(Sequence->GetPreviewMeshPath());
		Payload.SourceStackName = Sequence->GetSourceStackName();
		Payload.SourceAnimStackIndex = 0;
		Payload.DataModel = const_cast<UAnimDataModel*>(Sequence->GetDataModel());
		Payload.Notifies = Sequence->GetNotifies();
		Payload.NotifyTracks = Sequence->GetNotifyTracks();

		if (!FAssetFile::Save(NormalizedPath, MetaData, [&](FArchive& Ar)
		{
			Payload.Serialize(Ar, MetaData.PayloadVersion);
			return true;
		}))
		{
			return false;
		}
	}
	else
	{
		UE_LOG_ERROR("[AnimSequenceSave] AnimSequence must be saved as .uasset: %s", NormalizedPath.c_str());
		return false;
	}

	AnimSequenceMap[NormalizedPath] = const_cast<UAnimSequence*>(Sequence);

	if (std::find(AnimSequenceFilePaths.begin(), AnimSequenceFilePaths.end(), NormalizedPath) == AnimSequenceFilePaths.end())
	{
		AnimSequenceFilePaths.push_back(NormalizedPath);
	}

	return true;
}

UAnimSequence* FResourceManager::FindAnimSequence(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	auto It = AnimSequenceMap.find(NormalizedPath);
	return It != AnimSequenceMap.end() ? It->second : nullptr;
}

TArray<FString> FResourceManager::GetAnimSequencePaths() const
{
	return AnimSequenceFilePaths;
}

namespace
{
	int32 GetJsonInt(json::JSON& Object, const char* Key, int32 DefaultValue = 0)
	{
		return Object.hasKey(Key) ? static_cast<int32>(Object[Key].ToInt()) : DefaultValue;
	}

	float GetJsonFloat(json::JSON& Object, const char* Key, float DefaultValue = 0.0f)
	{
		return Object.hasKey(Key) ? static_cast<float>(Object[Key].ToFloat()) : DefaultValue;
	}

	bool GetJsonBool(json::JSON& Object, const char* Key, bool DefaultValue = false)
	{
		return Object.hasKey(Key) ? Object[Key].ToBool() : DefaultValue;
	}

	FString GetJsonString(json::JSON& Object, const char* Key, const FString& DefaultValue = "")
	{
		return Object.hasKey(Key) ? Object[Key].ToString() : DefaultValue;
	}

	FVector2 GetJsonVector2(json::JSON& Object, const char* Key, const FVector2& DefaultValue = FVector2(0.0f, 0.0f))
	{
		if (!Object.hasKey(Key))
		{
			return DefaultValue;
		}

		json::JSON& Value = Object[Key];
		if (Value.JSONType() == json::JSON::Class::Array && Value.length() >= 2)
		{
			return FVector2(
				static_cast<float>(Value[0].ToFloat()),
				static_cast<float>(Value[1].ToFloat()));
		}

		if (Value.JSONType() == json::JSON::Class::Object)
		{
			return FVector2(
				Value.hasKey("X") ? static_cast<float>(Value["X"].ToFloat()) : DefaultValue.X,
				Value.hasKey("Y") ? static_cast<float>(Value["Y"].ToFloat()) : DefaultValue.Y);
		}

		return DefaultValue;
	}

	FAnimTransitionConditionDesc ParseAnimTransitionCondition(json::JSON& Object)
	{
		FAnimTransitionConditionDesc Condition;
		Condition.Type = static_cast<EAnimTransitionConditionType>(
			GetJsonInt(Object, "Type", static_cast<int32>(EAnimTransitionConditionType::AlwaysTrue)));
		Condition.ParameterName = GetJsonString(Object, "ParameterName");
		Condition.BoolValue = GetJsonBool(Object, "BoolValue", true);
		Condition.Threshold = GetJsonFloat(Object, "Threshold", 0.0f);
		Condition.IntValue = GetJsonInt(Object, "IntValue", 0);
		Condition.LuaFunctionName = GetJsonString(Object, "LuaFunctionName");
		return Condition;
	}

	FAnimStateTransitionDesc ParseAnimStateTransition(json::JSON& Object)
	{
		FAnimStateTransitionDesc Transition;
		Transition.FromStateId = GetJsonInt(Object, "FromStateId", -1);
		Transition.ToStateId = GetJsonInt(Object, "ToStateId", -1);
		Transition.BlendTime = GetJsonFloat(Object, "BlendTime", 0.2f);
		Transition.Priority = GetJsonInt(Object, "Priority", 0);
		if (Object.hasKey("Condition") && Object["Condition"].JSONType() == json::JSON::Class::Object)
		{
			Transition.Condition = ParseAnimTransitionCondition(Object["Condition"]);
		}
		return Transition;
	}

	FAnimStateDesc ParseAnimState(json::JSON& Object)
	{
		FAnimStateDesc State;
		State.StateId = GetJsonInt(Object, "StateId", -1);
		State.Name = GetJsonString(Object, "Name");
		State.AnimationPath = GetJsonString(Object, "AnimationPath");
		State.Position = GetJsonVector2(Object, "Position");
		State.PlayRate = GetJsonFloat(Object, "PlayRate", 1.0f);
		State.bLoop = GetJsonBool(Object, "bLoop", true);
		State.bAutoAdvanceOnEnd = GetJsonBool(Object, "bAutoAdvanceOnEnd", true);
		return State;
	}

	FAnimStateMachineDesc ParseAnimStateMachine(json::JSON& Object)
	{
		FAnimStateMachineDesc Machine;
		Machine.EntryStateId = GetJsonInt(Object, "EntryStateId", -1);

		if (Object.hasKey("States") && Object["States"].JSONType() == json::JSON::Class::Array)
		{
			json::JSON& States = Object["States"];
			for (int32 Index = 0; Index < static_cast<int32>(States.length()); ++Index)
			{
				if (States[Index].JSONType() == json::JSON::Class::Object)
				{
					Machine.States.push_back(ParseAnimState(States[Index]));
				}
			}
		}

		if (Object.hasKey("Transitions") && Object["Transitions"].JSONType() == json::JSON::Class::Array)
		{
			json::JSON& Transitions = Object["Transitions"];
			for (int32 Index = 0; Index < static_cast<int32>(Transitions.length()); ++Index)
			{
				if (Transitions[Index].JSONType() == json::JSON::Class::Object)
				{
					Machine.Transitions.push_back(ParseAnimStateTransition(Transitions[Index]));
				}
			}
		}

		return Machine;
	}

}

UAnimGraphAsset* FResourceManager::LoadAnimGraph(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);

	if (FAssetFile::IsAssetPath(NormalizedPath))
	{
		FAssetMetaData MetaData;
		UAnimGraphAsset* Asset = nullptr;
		const bool bLoaded = FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
		{
			if (!MetaData.ClassName.empty() && MetaData.ClassName != UAnimGraphAsset::StaticClass()->GetName())
			{
				return false;
			}

			Asset = UObjectManager::Get().CreateObject<UAnimGraphAsset>();
			if (!Asset)
			{
				return false;
			}

			Asset->Serialize(Ar);
			Asset->ValidateAndRepairGraph();
			return true;
		});

		if (!bLoaded || !Asset)
		{
			UE_LOG_ERROR("[AnimGraphAsset] Failed to load .uasset: %s", NormalizedPath.c_str());
			return nullptr;
		}

		return Asset;
	}

	UE_LOG_ERROR("[AnimGraphAsset] Legacy AnimGraph path is no longer supported: %s", NormalizedPath.c_str());
	return nullptr;
}

bool FResourceManager::SaveAnimGraph(UAnimGraphAsset* Asset, const FString& Path)
{
	if (!Asset)
	{
		return false;
	}

	const FString NormalizedPath = FPaths::Normalize(Path);
	if (FAssetFile::IsAssetPath(NormalizedPath))
	{
		FAssetMetaData MetaData;
		MetaData.PayloadVersion = 1;
		MetaData.ClassName = UAnimGraphAsset::StaticClass()->GetName();
		MetaData.DisplayName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(NormalizedPath)).filename().wstring());

		const bool bSaved = FAssetFile::Save(NormalizedPath, MetaData, [&](FArchive& Ar)
		{
			Asset->Serialize(Ar);
			return true;
		});
		if (!bSaved)
		{
			UE_LOG_ERROR("[AnimGraphAsset] Failed to save .uasset: %s", NormalizedPath.c_str());
		}
		return bSaved;
	}

	UE_LOG_ERROR("[AnimGraphAsset] Legacy AnimGraph save path is no longer supported: %s", NormalizedPath.c_str());
	return false;
}

UParticleSystem* FResourceManager::LoadParticleSystem(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (NormalizedPath.empty() || !IsParticleSystemAssetPath(NormalizedPath))
	{
		return nullptr;
	}

	if (UParticleSystem* CachedAsset = FindParticleSystem(NormalizedPath))
	{
		return CachedAsset;
	}

	if (FAssetFile::IsAssetPath(NormalizedPath))
	{
		FAssetMetaData MetaData;
		UParticleSystem* Asset = nullptr;
		const bool bLoaded = FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
		{
			if (!MetaData.ClassName.empty() && MetaData.ClassName != "ParticleSystem")
			{
				return false;
			}

			if (MetaData.PayloadVersion == 2)
			{
				FString JsonText;
				Ar << "JsonPayload" << JsonText;
				json::JSON JsonData = json::JSON::Load(JsonText);
				Asset = LoadParticleSystemFromJson(JsonData, NormalizedPath);
			}
			else
			{
				Asset = LoadParticleSystemObjectGraph(Ar, NormalizedPath);
			}
			return Asset != nullptr;
		});

		if (!bLoaded || !Asset)
		{
			UE_LOG_ERROR("[ParticleSystemAsset] Failed to load .uasset: %s", NormalizedPath.c_str());
			return nullptr;
		}

		Asset->SetAssetPath(NormalizedPath);
		ParticleSystemMap[NormalizedPath] = Asset;
		return Asset;
	}

	return nullptr;
}

UParticleSystem* FResourceManager::FindParticleSystem(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	auto It = ParticleSystemMap.find(NormalizedPath);
	return It != ParticleSystemMap.end() ? It->second : nullptr;
}

void FResourceManager::RegisterParticleSystem(UParticleSystem* Asset, const FString& Path)
{
	if (!Asset)
	{
		return;
	}

	const FString NormalizedPath = FPaths::Normalize(Path);
	if (NormalizedPath.empty() || !IsParticleSystemAssetPath(NormalizedPath))
	{
		return;
	}

	Asset->SetAssetPath(NormalizedPath);
	ParticleSystemMap[NormalizedPath] = Asset;
}

bool FResourceManager::SaveParticleSystem(UParticleSystem* Asset, const FString& Path)
{
	if (!Asset)
	{
		return false;
	}

	const FString NormalizedPath = FPaths::Normalize(Path);
	if (NormalizedPath.empty() || !IsParticleSystemAssetPath(NormalizedPath))
	{
		return false;
	}

	if (FAssetFile::IsAssetPath(NormalizedPath))
	{
		Asset->SetAssetPath(NormalizedPath);

		FAssetMetaData MetaData;
		FAssetMetaData ExistingMetaData;
		MetaData.AssetGuid = FAssetFile::LoadMetadataOnly(NormalizedPath, ExistingMetaData) && !ExistingMetaData.AssetGuid.empty()
			? ExistingMetaData.AssetGuid
			: FGuid::NewGuid().ToString();
		MetaData.ClassName = "ParticleSystem";
		MetaData.DisplayName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(NormalizedPath)).stem().wstring());
		MetaData.PayloadVersion = 1;

		return FAssetFile::Save(NormalizedPath, MetaData, [&](FArchive& Ar)
		{
			return SerializeParticleSystemObjectGraph(Ar, Asset, NormalizedPath);
		});
	}

	return false;
}

bool FResourceManager::RunParticleSystemSerializationSmokeTest(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (NormalizedPath.empty() || !IsParticleSystemAssetPath(NormalizedPath))
	{
		UE_LOG_ERROR("[ParticleSystemAssetSmoke] Invalid path: %s", NormalizedPath.c_str());
		return false;
	}

	UParticleSystem* Original = UParticleSystem::CreateDefaultSpriteSystem();
	if (!Original)
	{
		UE_LOG_ERROR("[ParticleSystemAssetSmoke] Failed to create default sprite system.");
		return false;
	}

	TArray<FString> OriginalErrors;
	if (!Original->Validate(&OriginalErrors))
	{
		for (const FString& Error : OriginalErrors)
		{
			UE_LOG_ERROR("[ParticleSystemAssetSmoke] Source validation error: %s", Error.c_str());
		}

		UObjectManager::Get().DestroyObject(Original);
		return false;
	}

	const bool bSaved = SaveParticleSystem(Original, NormalizedPath);
	UObjectManager::Get().DestroyObject(Original);
	Original = nullptr;

	if (!bSaved)
	{
		UE_LOG_ERROR("[ParticleSystemAssetSmoke] Save failed: %s", NormalizedPath.c_str());
		return false;
	}

	UParticleSystem* Loaded = LoadParticleSystem(NormalizedPath);
	if (!Loaded)
	{
		UE_LOG_ERROR("[ParticleSystemAssetSmoke] Load failed: %s", NormalizedPath.c_str());
		return false;
	}
	Loaded->SetAssetPath(NormalizedPath);

	Loaded->CacheEmitterModuleInfo();

	bool bPassed = true;
	if (Loaded->GetAssetPath() != NormalizedPath)
	{
		UE_LOG_ERROR(
			"[ParticleSystemAssetSmoke] Asset path mismatch. Expected '%s', got '%s'.",
			NormalizedPath.c_str(),
			Loaded->GetAssetPath().c_str()
		);
		bPassed = false;
	}

	TArray<FString> LoadedErrors;
	if (!Loaded->Validate(&LoadedErrors))
	{
		bPassed = false;
		for (const FString& Error : LoadedErrors)
		{
			UE_LOG_ERROR("[ParticleSystemAssetSmoke] Loaded validation error: %s", Error.c_str());
		}
	}

	const TArray<UParticleEmitter*>& Emitters = Loaded->GetEmitters();
	if (Emitters.size() != 1)
	{
		UE_LOG_ERROR("[ParticleSystemAssetSmoke] Expected one emitter, got %d.", static_cast<int32>(Emitters.size()));
		bPassed = false;
	}

	const UParticleEmitter* Emitter = Emitters.empty() ? nullptr : Emitters[0];
	const UParticleLODLevel* LODLevel = Emitter ? Emitter->GetLODLevel(0) : nullptr;
	const FCompiledParticleLODData* CompiledLOD = Emitter ? Emitter->GetCompiledLODData(0) : nullptr;
	if (!LODLevel)
	{
		UE_LOG_ERROR("[ParticleSystemAssetSmoke] Missing LOD0 after load.");
		bPassed = false;
	}
	else
	{
		if (!LODLevel->GetRequiredModule())
		{
			UE_LOG_ERROR("[ParticleSystemAssetSmoke] Missing required module after load.");
			bPassed = false;
		}

		if (!LODLevel->GetSpawnModule())
		{
			UE_LOG_ERROR("[ParticleSystemAssetSmoke] Missing cached spawn module after load.");
			bPassed = false;
		}

		if (LODLevel->GetModules().size() < 6)
		{
			UE_LOG_ERROR(
				"[ParticleSystemAssetSmoke] Expected at least six regular modules, got %d.",
				static_cast<int32>(LODLevel->GetModules().size())
			);
			bPassed = false;
		}

		if (LODLevel->GetSpawnModules().empty())
		{
			UE_LOG_ERROR("[ParticleSystemAssetSmoke] Spawn module cache is empty after load.");
			bPassed = false;
		}

		if (LODLevel->GetUpdateModules().empty())
		{
			UE_LOG_ERROR("[ParticleSystemAssetSmoke] Update module cache is empty after load.");
			bPassed = false;
		}

		if (!LODLevel->GetEffectiveRendererProperties())
		{
			UE_LOG_ERROR("[ParticleSystemAssetSmoke] Missing renderer properties after load.");
			bPassed = false;
		}
		else if (LODLevel->GetEffectiveRenderMode() != EParticleEmitterRenderMode::Sprite)
		{
			UE_LOG_ERROR("[ParticleSystemAssetSmoke] Expected sprite renderer after load.");
			bPassed = false;
		}

		if (!CompiledLOD)
		{
			UE_LOG_ERROR("[ParticleSystemAssetSmoke] Missing compiled LOD data after load.");
			bPassed = false;
		}
		else
		{
			if (CompiledLOD->SourceLODLevel != LODLevel)
			{
				UE_LOG_ERROR("[ParticleSystemAssetSmoke] Compiled LOD source does not match LOD0.");
				bPassed = false;
			}

			if (CompiledLOD->RequiredModule != LODLevel->GetRequiredModule())
			{
				UE_LOG_ERROR("[ParticleSystemAssetSmoke] Compiled required module mismatch.");
				bPassed = false;
			}

			if (CompiledLOD->SpawnModule != LODLevel->GetSpawnModule())
			{
				UE_LOG_ERROR("[ParticleSystemAssetSmoke] Compiled spawn module mismatch.");
				bPassed = false;
			}

			if (CompiledLOD->RendererProperties != LODLevel->GetEffectiveRendererProperties())
			{
				UE_LOG_ERROR("[ParticleSystemAssetSmoke] Compiled renderer properties mismatch.");
				bPassed = false;
			}

			if (CompiledLOD->RenderMode != EParticleEmitterRenderMode::Sprite)
			{
				UE_LOG_ERROR("[ParticleSystemAssetSmoke] Expected compiled sprite render mode.");
				bPassed = false;
			}

			if (CompiledLOD->ParticleSize != sizeof(FBaseParticle))
			{
				UE_LOG_ERROR("[ParticleSystemAssetSmoke] Compiled particle size mismatch.");
				bPassed = false;
			}

			if (CompiledLOD->MaxActiveParticles <= 0)
			{
				UE_LOG_ERROR("[ParticleSystemAssetSmoke] Compiled max active particles is invalid.");
				bPassed = false;
			}

			if (CompiledLOD->SpawnModules.empty())
			{
				UE_LOG_ERROR("[ParticleSystemAssetSmoke] Compiled spawn module list is empty.");
				bPassed = false;
			}

			if (CompiledLOD->UpdateModules.empty())
			{
				UE_LOG_ERROR("[ParticleSystemAssetSmoke] Compiled update module list is empty.");
				bPassed = false;
			}
		}
	}

	ParticleSystemMap.erase(NormalizedPath);
	UObjectManager::Get().DestroyObject(Loaded);

	if (bPassed)
	{
		UE_LOG("[ParticleSystemAssetSmoke] Passed: %s", NormalizedPath.c_str());
	}

	return bPassed;
}

FString FResourceManager::SerializeParticleSystemToString(UParticleSystem* Asset)
{
	if (!Asset)
	{
		return "";
	}

	json::JSON JsonData = BuildParticleSystemAssetJson(Asset, "");
	return JsonData.dump();
}

UParticleSystem* FResourceManager::LoadParticleSystemFromString(const FString& Snapshot)
{
	if (Snapshot.empty())
	{
		return nullptr;
	}

	json::JSON JsonData = json::JSON::Load(Snapshot);
	return LoadParticleSystemFromJson(JsonData, "ParticleSystemSnapshot");
}

const TArray<FString>& FResourceManager::GetTextureFilePath() const
{
	return TextureFilePaths;
}

ID3D11SamplerState* FResourceManager::GetOrCreateSamplerState(ESamplerType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}

	return RenderStateCache.GetOrCreateSamplerState(Type, Device);
}

ID3D11DepthStencilState* FResourceManager::GetOrCreateDepthStencilState(EDepthStencilType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}
	return RenderStateCache.GetOrCreateDepthStencilState(Type, Device);
}

ID3D11BlendState* FResourceManager::GetOrCreateBlendState(EBlendType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}
	return RenderStateCache.GetOrCreateBlendState(Type, Device);
}

ID3D11RasterizerState* FResourceManager::GetOrCreateRasterizerState(ERasterizerType Type, ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		Device = CachedDevice.Get();
	}
	return RenderStateCache.GetOrCreateRasterizerState(Type, Device);
}

size_t FResourceManager::GetMaterialMemorySize() const
{
	return FResourceMemoryReporter::GetMaterialMemorySize(MaterialCache);
}
