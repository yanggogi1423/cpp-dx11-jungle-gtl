#include "Core/ResourceManager.h"

#include "Core/Paths.h"
#include "Core/AssetPathPolicy.h"
#include "Core/ImportedMaterialPolicy.h"
#include "Core/MaterialSerializationService.h"
#include "Core/ResourceMemoryReporter.h"
#include "Core/SkeletalMeshLoadService.h"
#include "Core/StaticMeshLoadService.h"

#include "Animation/AnimationStateMachine.h"
#include "Animation/AnimSequence.h"
#include "Asset/FbxImporter.h"
#include "Asset/AssetFile.h"
#include "Asset/CurveFloatAsset.h"

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cwctype>
#include "Asset/FileUtils.h"

#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "Core/Logging/Log.h"

#if WITH_EDITOR
#include "Settings/EditorSettings.h"
#endif

#include "Asset/StaticMeshTypes.h"
#include "Asset/StaticMeshSimplifier.h"
#include "Render/Scene/RenderCommand.h"

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
}

// Remove later
const char* GetAssetClassDisplayName(const FString& ClassName)
{
	if (ClassName == "UStaticMesh") return "Static Mesh";
	if (ClassName == "USkeletalMesh") return "Skeletal Mesh";
	if (ClassName == "UAnimSequence") return "Animation Sequence";
	if (ClassName == "UAnimationStateMachine") return "Animation State Machine";
	return ClassName.c_str();
}

FString MakeDisplayNameFromAssetPath(const FString& Path)
{
	const std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	return FPaths::ToUtf8(FsPath.stem().wstring());
}

FString FindExistingOrNewAssetGuid(const FString& AssetPath)
{
	FAssetMetaData ExistingMetaData;
	if (FAssetFile::LoadMetadataOnly(AssetPath, ExistingMetaData) &&
		!ExistingMetaData.AssetGuid.empty())
	{
		return ExistingMetaData.AssetGuid;
	}

	return FGuid::NewGuid().ToString();
}

FResourceManager::FResourceManager()
	: FbxImporter(std::make_unique<FFbxImporter>())
{
}

FResourceManager::~FResourceManager()
{
	ReleaseGPUResources();
}

#pragma region __BINARY__

namespace fs = std::filesystem;

uint64 FResourceManager::GetFileWriteTimeTicks(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	fs::path FilePath(FPaths::ToAbsolute(FPaths::ToWide(NormalizedPath)));
	if (!fs::exists(FilePath))
	{
		return 0;
	}

	auto WriteTime = fs::last_write_time(FilePath);
	auto Duration = WriteTime.time_since_epoch();

	return static_cast<uint64>(
		std::chrono::duration_cast<std::chrono::seconds>(Duration).count());
}

FString FResourceManager::MakeAnimSequenceCacheKey(const FString& NormalizedPath, const USkeletalMesh* TargetMesh, int32 AnimStackIndex) const
{
	FString TargetMeshKey;
	if (TargetMesh)
	{
		TargetMeshKey = FPaths::Normalize(TargetMesh->GetAssetPathFileName());
		if (TargetMeshKey.empty())
		{
			TargetMeshKey = TargetMesh->GetFName().ToString();
		}
	}

	return NormalizedPath + "|" + TargetMeshKey + "|" + std::to_string(AnimStackIndex);
}

bool FResourceManager::IsAnimSequenceUAssetPath(const FString& Path) const
{
    FAssetMetaData MetaData;
    if (!FAssetFile::LoadMetadataOnly(Path,MetaData)) return false;

	return MetaData.ClassName == "UAnimSequence";
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
	ParticleFilePaths.clear();
	CurveFilePaths.clear();
	AnimationStateMachineFilePaths.clear();
	SkeletalMeshFilePaths.clear();
	AnimSequenceFilePaths.clear();
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

	if (Extension == L".meta")
	{
		return;
	}

	const FString RelativePath = FPaths::Normalize(FPaths::ToString(std::filesystem::relative(FilePath, ProjectRootPath)));

	if (Extension == L".uasset")
	{
		FAssetMetaData MetaData;
		if (FAssetFile::LoadMetadataOnly(RelativePath, MetaData))
		{
			if (MetaData.ClassName == UStaticMesh::StaticClass()->ClassName)
			{
				ObjFilePaths.push_back(RelativePath);

				FStaticMeshResource Resource;
				Resource.Name = RelativePath;
				Resource.Path = RelativePath;
				Resource.bPreload = false;
				Resource.bNormalizeToUnitCube = false;
				StaticMeshCache.RegisterResource(Resource);
			}
			else if (MetaData.ClassName == USkeletalMesh::StaticClass()->ClassName)
			{
				SkeletalMeshFilePaths.push_back(RelativePath);
			}
			else if (MetaData.ClassName == UAnimSequence::StaticClass()->ClassName)
			{
				AnimSequenceFilePaths.push_back(RelativePath);
			}
			else if (MetaData.ClassName == UAnimationStateMachine::StaticClass()->ClassName)
			{
				AnimationStateMachineFilePaths.push_back(RelativePath);
			}
			else if (MetaData.ClassName == UMaterial::StaticClass()->ClassName ||
					 MetaData.ClassName == UMaterialInstance::StaticClass()->ClassName)
			{
				MaterialFilePaths.push_back(RelativePath);
			}
			else if (MetaData.ClassName == UCurveFloatAsset::StaticClass()->ClassName)
			{
				CurveFilePaths.push_back(RelativePath);
			}
		}
	}
	else if (Extension == L".png" || Extension == L".dds" || Extension == L".jpg" || Extension == L".jpeg")
	{
		const FTextureAssetMeta Meta = LoadOrCreateTextureMeta(FilePath);

		if (Meta.Type == EAssetMetaType::Font)
		{
			FontFilePaths.push_back(RelativePath);
			RegisterFont(FName(RelativePath.c_str()), RelativePath, Meta.Columns, Meta.Rows);
		}
		else if (Meta.Type == EAssetMetaType::Particle)
		{
			ParticleFilePaths.push_back(RelativePath);
			RegisterParticle(FName(RelativePath.c_str()), RelativePath, Meta.Columns, Meta.Rows);
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

    for (auto& [Path, Mesh] : SkeletalMeshMap)
    {
        UObjectManager::Get().DestroyObject(Mesh);
	}
	SkeletalMeshMap.clear();

	for (auto& Pair : AnimSequenceMap)
	{
		UObjectManager::Get().DestroyObject(Pair.second);
	}
	AnimSequenceMap.clear();

	for (auto& Pair : AnimationStateMachineMap)
	{
		UObjectManager::Get().DestroyObject(Pair.second);
	}
	AnimationStateMachineMap.clear();

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

UMaterialInterface* FResourceManager::GetMaterialForStaticMeshSlot(const FString& SourcePath, const FString& SlotName)
{
	if (!SourcePath.empty())
	{
		const FString* Alias = MaterialCache.FindMaterialSlotAlias(FImportedMaterialPolicy::MakeMaterialSlotAliasKey(SourcePath, SlotName));
		if (Alias)
		{
			if (UMaterialInterface* Material = GetMaterialInterface(*Alias))
			{
				return Material;
			}
		}
	}

	return GetMaterialInterface(SlotName);
}

void FResourceManager::ResolveStaticMeshMaterialSlots(const FString& SourcePath, FStaticMesh* StaticMesh)
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

void FResourceManager::ResolveSkeletalMeshMaterialSlots(const FString& SourcePath, FSkeletalMesh* SkeletalMesh)
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
	const FString NormalizedPath = FPaths::Normalize(MatFilePath);
	const bool bSaved = FMaterialSerializationService(*this).SerializeMaterial(NormalizedPath, Material);
	if (bSaved)
	{
		if (std::find(MaterialFilePaths.begin(), MaterialFilePaths.end(), NormalizedPath) == MaterialFilePaths.end())
		{
			MaterialFilePaths.push_back(NormalizedPath);
		}
		if (Material)
		{
			MaterialCache.RegisterMaterial(NormalizedPath, const_cast<UMaterial*>(Material));
		}
	}
	return bSaved;
}

bool FResourceManager::SerializeMaterialInstance(const FString& MatInstFilePath, const UMaterialInstance* MaterialInstance)
{
	const FString NormalizedPath = FPaths::Normalize(MatInstFilePath);
	const bool bSaved = FMaterialSerializationService(*this).SerializeMaterialInstance(NormalizedPath, MaterialInstance);
	if (bSaved)
	{
		if (std::find(MaterialFilePaths.begin(), MaterialFilePaths.end(), NormalizedPath) == MaterialFilePaths.end())
		{
			MaterialFilePaths.push_back(NormalizedPath);
		}
		if (MaterialInstance)
		{
			MaterialCache.RegisterMaterialInstance(NormalizedPath, const_cast<UMaterialInstance*>(MaterialInstance));
		}
	}
	return bSaved;
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

// --- Particle ---
FParticleResource* FResourceManager::FindParticle(const FName& ParticleName)
{
	return AtlasCache.FindParticle(ParticleName);
}

const FParticleResource* FResourceManager::FindParticle(const FName& ParticleName) const
{
	return AtlasCache.FindParticle(ParticleName);
}

void FResourceManager::RegisterParticle(const FName& ParticleName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	AtlasCache.RegisterParticle(ParticleName, InPath, Columns, Rows);
}

TArray<FString> FResourceManager::GetFontNames() const
{
	return FontFilePaths;
}

TArray<FString> FResourceManager::GetParticleNames() const
{
	return ParticleFilePaths;
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

USkeletalMesh* FResourceManager::LoadSkeletalMesh(const FString& Path)
{
    return FSkeletalMeshLoadService(*this).Load(Path);
}

USkeletalMesh* FResourceManager::ImportSkeletalMeshFromSource(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	InvalidateSkeletalMesh(NormalizedPath);
	return FSkeletalMeshLoadService(*this).ImportSource(NormalizedPath);
}

void FResourceManager::InvalidateSkeletalMesh(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	SkeletalMeshMap.erase(NormalizedPath);
	SkeletalMeshFilePaths.erase(
		std::remove(SkeletalMeshFilePaths.begin(), SkeletalMeshFilePaths.end(), NormalizedPath),
		SkeletalMeshFilePaths.end());
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
	const FString NormalizedPath = FPaths::Normalize(Path);
	const FString AbsolutePath = FPaths::Normalize(FPaths::ToAbsoluteString(FPaths::ToWide(NormalizedPath)));
	const uint64 SourceWriteTimeTicks = GetFileWriteTimeTicks(AbsolutePath);

	auto CacheIt = FbxInspectCache.find(AbsolutePath);
	if (CacheIt != FbxInspectCache.end() &&
		CacheIt->second.SourceWriteTimeTicks == SourceWriteTimeTicks &&
		CacheIt->second.bHasMeshInfo)
	{
		return CacheIt->second.MeshInfo;
	}

    return FbxImporter->InspectMeshContent(AbsolutePath);
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
		MetaData.ClassName = USkeletalMesh::StaticClass()->ClassName;
		MetaData.DisplayName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(AssetPath)).stem().wstring());
		MetaData.SourceFile = "";
	}

	if (MetaData.ClassName != USkeletalMesh::StaticClass()->ClassName)
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

UAnimSequence* FResourceManager::LoadAnimSequence(const FString& Path, const USkeletalMesh* TargetMesh, int32 AnimStackIndex)
{
	const FString NormalizedPath = FPaths::Normalize(Path);

	if (!TargetMesh || !TargetMesh->HasValidMeshData())
	{
		UE_LOG_ERROR("[AnimSequenceLoad] Invalid target skeletal mesh for animation sequence: %s", NormalizedPath.c_str());
		return nullptr;
	}

	FAssetMetaData MetaData;
	const bool bHasAssetData = FAssetFile::LoadMetadataOnly(NormalizedPath, MetaData);
	const bool bisAnimSeqAsset = bHasAssetData && MetaData.ClassName == UAnimSequence::StaticClass()->ClassName;

	if (bisAnimSeqAsset)
	{
		const FString CacheKey = MakeAnimSequenceCacheKey(NormalizedPath, TargetMesh, 0);
		auto It = AnimSequenceMap.find(CacheKey);
		if (It != AnimSequenceMap.end())
		{
			return It->second;
		}

		FAnimSequenceAssetPayload Payload;
		const bool bLoaded = FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
		{
			Payload.Serialize(Ar, MetaData.PayloadVersion);
			return true;
		});

		if (!bLoaded || !Payload.DataModel)
		{
			UE_LOG_ERROR("[AnimSequenceLoad] Failed to load animation sequence asset: %s", NormalizedPath.c_str());
			return nullptr;
		}

		const FString PayloadTargetMeshPath = FPaths::Normalize(Payload.TargetSkeletalMeshPath);
		const FString CurrentTargetMeshPath = FPaths::Normalize(TargetMesh->GetAssetPathFileName());

		if (!PayloadTargetMeshPath.empty() && PayloadTargetMeshPath != CurrentTargetMeshPath)
		{
			UE_LOG_ERROR(
				"[AnimSequenceLoad] Target skeletal mesh mismatch | Anim=%s | PayloadTarget=%s | CurrentTarget=%s",
				NormalizedPath.c_str(),
				PayloadTargetMeshPath.c_str(),
				CurrentTargetMeshPath.c_str());

			return nullptr;
		}

		UAnimSequence* AnimSequence = UObjectManager::Get().CreateObject<UAnimSequence>();
		AnimSequence->SetDataModel(Payload.DataModel);
		AnimSequence->SetNotifyTracks(Payload.NotifyTracks);
		AnimSequence->SetAssetPathFileName(NormalizedPath);

		AnimSequenceMap[CacheKey] = AnimSequence;

		if (std::find(AnimSequenceFilePaths.begin(), AnimSequenceFilePaths.end(), NormalizedPath) == AnimSequenceFilePaths.end())
		{
			AnimSequenceFilePaths.push_back(NormalizedPath);
		}

		UE_LOG("[AnimSequenceLoad] Loaded animation sequence asset: %s", NormalizedPath.c_str());

		return AnimSequence;
	}

	const FString CacheKey = MakeAnimSequenceCacheKey(NormalizedPath, TargetMesh, AnimStackIndex);
	if (UAnimSequence* FoundAnimSequence = FindAnimSequence(NormalizedPath, TargetMesh, AnimStackIndex))
	{
		return FoundAnimSequence;
	}

	UAnimSequence* AnimSequence = FbxImporter->LoadAnimSequence(NormalizedPath, TargetMesh, AnimStackIndex);
	if (!AnimSequence)
	{
		UE_LOG_ERROR("[AnimSequenceLoad] Failed to load animation sequence from FBX: %s", NormalizedPath.c_str());
		return nullptr;
	}

	AnimSequence->SetAssetPathFileName(NormalizedPath);
	AnimSequenceMap[CacheKey] = AnimSequence;

	if (std::find(AnimSequenceFilePaths.begin(), AnimSequenceFilePaths.end(), NormalizedPath) == AnimSequenceFilePaths.end())
	{
		AnimSequenceFilePaths.push_back(NormalizedPath);
	}

	return AnimSequence;
}

UAnimSequence* FResourceManager::FindAnimSequence(const FString& Path, const USkeletalMesh* TargetMesh, int32 AnimStackIndex) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	const FString CacheKey = MakeAnimSequenceCacheKey(NormalizedPath, TargetMesh, AnimStackIndex);

	auto It = AnimSequenceMap.find(CacheKey);
	if (It != AnimSequenceMap.end())
	{
		return It->second;
	}

	return nullptr;
}

TArray<FString> FResourceManager::GetAnimSequencePaths() const
{
	return AnimSequenceFilePaths;
}

TArray<FFbxAnimationClipInfo> FResourceManager::InspectAnimationClips(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	const FString AbsolutePath = FPaths::Normalize(FPaths::ToAbsoluteString(FPaths::ToWide(NormalizedPath)));
	const uint64 SourceWriteTimeTicks = GetFileWriteTimeTicks(AbsolutePath);

	auto CacheIt = FbxInspectCache.find(AbsolutePath);
	if (CacheIt != FbxInspectCache.end() &&
		CacheIt->second.SourceWriteTimeTicks == SourceWriteTimeTicks &&
		CacheIt->second.bHasAnimationClips)
	{
		return CacheIt->second.AnimationClips;
	}

	TArray<FFbxAnimationClipInfo> AnimationClips = FbxImporter->InspectAnimationClips(AbsolutePath);
	FFbxInspectCacheEntry& CacheEntry = FbxInspectCache[AbsolutePath];
	if (CacheEntry.SourceWriteTimeTicks != SourceWriteTimeTicks)
	{
		CacheEntry = FFbxInspectCacheEntry();
	}
	CacheEntry.SourceWriteTimeTicks = SourceWriteTimeTicks;
	CacheEntry.bHasAnimationClips = true;
	CacheEntry.AnimationClips = AnimationClips;
	return AnimationClips;
}

bool FResourceManager::InspectMeshAndAnimClips(const FString& Path, FFbxMeshContentInfo& OutInfo, TArray<FFbxAnimationClipInfo>& OutAnimationClips)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	const FString AbsolutePath = FPaths::Normalize(FPaths::ToAbsoluteString(FPaths::ToWide(NormalizedPath)));
	const uint64 SourceWriteTimeTicks = GetFileWriteTimeTicks(AbsolutePath);

	auto CacheIt = FbxInspectCache.find(AbsolutePath);
	if (CacheIt != FbxInspectCache.end() &&
		CacheIt->second.SourceWriteTimeTicks == SourceWriteTimeTicks &&
		CacheIt->second.bHasMeshInfo &&
		CacheIt->second.bHasAnimationClips)
	{
		OutInfo = CacheIt->second.MeshInfo;
		OutAnimationClips = CacheIt->second.AnimationClips;
		return true;
	}

	FFbxMeshContentInfo MeshInfo;
	TArray<FFbxAnimationClipInfo> AnimationClips;
	if (!FbxImporter->InspectMeshAndAnimClips(AbsolutePath, MeshInfo, AnimationClips))
	{
		OutInfo = FFbxMeshContentInfo();
		OutAnimationClips.clear();
		return false;
	}

	FFbxInspectCacheEntry CacheEntry;
	CacheEntry.SourceWriteTimeTicks = SourceWriteTimeTicks;
	CacheEntry.bHasMeshInfo = true;
	CacheEntry.bHasAnimationClips = true;
	CacheEntry.MeshInfo = MeshInfo;
	CacheEntry.AnimationClips = AnimationClips;
	FbxInspectCache[AbsolutePath] = CacheEntry;

	OutInfo = MeshInfo;
	OutAnimationClips = AnimationClips;
	return true;
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

UAnimationStateMachine* FResourceManager::LoadAnimationStateMachine(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);

	if (UAnimationStateMachine* FoundStateMachine = FindAnimationStateMachine(NormalizedPath))
	{
		return FoundStateMachine;
	}

	FAssetMetaData MetaData;
	if (!FAssetFile::LoadMetadataOnly(NormalizedPath, MetaData) ||
		MetaData.ClassName != UAnimationStateMachine::StaticClass()->ClassName)
	{
		UE_LOG_ERROR("[AnimationStateMachineLoad] Invalid state machine asset: %s", NormalizedPath.c_str());
		return nullptr;
	}

	UAnimationStateMachine* StateMachine = UObjectManager::Get().CreateObject<UAnimationStateMachine>();
	const bool bLoaded = FAssetFile::Load(NormalizedPath, MetaData, [&](FArchive& Ar)
	{
		StateMachine->Serialize(Ar);
		return true;
	});

	if (!bLoaded)
	{
		UObjectManager::Get().DestroyObject(StateMachine);
		UE_LOG_ERROR("[AnimationStateMachineLoad] Failed to load state machine asset: %s", NormalizedPath.c_str());
		return nullptr;
	}

	AnimationStateMachineMap[NormalizedPath] = StateMachine;
	if (std::find(AnimationStateMachineFilePaths.begin(), AnimationStateMachineFilePaths.end(), NormalizedPath) ==
		AnimationStateMachineFilePaths.end())
	{
		AnimationStateMachineFilePaths.push_back(NormalizedPath);
	}

	return StateMachine;
}

UAnimationStateMachine* FResourceManager::FindAnimationStateMachine(const FString& Path) const
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	auto It = AnimationStateMachineMap.find(NormalizedPath);
	return It != AnimationStateMachineMap.end() ? It->second : nullptr;
}

bool FResourceManager::SaveAnimationStateMachine(const FString& Path, UAnimationStateMachine* StateMachine)
{
	if (!StateMachine)
	{
		return false;
	}

	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!FAssetFile::IsAssetPath(NormalizedPath))
	{
		UE_LOG_ERROR("[AnimationStateMachineSave] Destination is not .uasset: %s", NormalizedPath.c_str());
		return false;
	}

	FAssetMetaData MetaData;
	MetaData.Version = 1;
	MetaData.PayloadVersion = 1;
	MetaData.AssetGuid = FindExistingOrNewAssetGuid(NormalizedPath);
	MetaData.ClassName = UAnimationStateMachine::StaticClass()->ClassName;
	MetaData.DisplayName = MakeDisplayNameFromAssetPath(NormalizedPath);
	MetaData.SourceFile = "";

	const bool bSaved = FAssetFile::Save(NormalizedPath, MetaData, [&](FArchive& Ar)
	{
		StateMachine->Serialize(Ar);
		return true;
	});

	if (!bSaved)
	{
		UE_LOG_ERROR("[AnimationStateMachineSave] Failed to save state machine asset: %s", NormalizedPath.c_str());
		return false;
	}

	AnimationStateMachineMap[NormalizedPath] = StateMachine;
	if (std::find(AnimationStateMachineFilePaths.begin(), AnimationStateMachineFilePaths.end(), NormalizedPath) ==
		AnimationStateMachineFilePaths.end())
	{
		AnimationStateMachineFilePaths.push_back(NormalizedPath);
	}

	return true;
}

TArray<FString> FResourceManager::GetAnimationStateMachinePaths() const
{
	return AnimationStateMachineFilePaths;
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
