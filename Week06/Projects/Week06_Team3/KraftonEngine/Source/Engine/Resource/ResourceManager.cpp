#include "Resource/ResourceManager.h"
#include "Platform/Paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <d3d11.h>
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "UI/EditorConsoleWidget.h"
#include "Profiling/MemoryStats.h"

namespace
{
	constexpr const wchar_t* ParticleDir = L"Asset\\Particle\\";
	constexpr const wchar_t* TextureDir = L"Asset\\Textures\\";

	constexpr const char* DecalIconTextureName = "DecalIcon";
	constexpr const wchar_t* DecalIconPath = L"Asset\\Editor\\Icons\\DecalActorIcon.png";
	constexpr const char* PawnIconTextureName = "PawnIcon";
	constexpr const wchar_t* PawnIconPath = L"Asset\\Editor\\Icons\\Pawn_64x.png";
	constexpr const char* PointLightIconTextureName = "PointLightIcon";
	constexpr const wchar_t* PointLightIconPath = L"Asset\\Editor\\Icons\\PointLight_64x.png";
	constexpr const char* SpotLightIconTextureName = "SpotLightIcon";
	// 시연용
	constexpr const wchar_t* SpotLightIconPath = L"Asset\\Editor\\Icons\\SpotLight_64x.png";
	//constexpr const wchar_t* SpotLightIconPath = L"Asset\\Editor\\Icons\\SpotLight.png";
	constexpr const char* EmptyActorIconTextureName = "EmptyActorIcon";
	constexpr const wchar_t* EmptyActorIconPath = L"Asset\\Editor\\Icon\\EmptyActor.png";
	constexpr const wchar_t* EmptyActorIconFallbackPath = L"Asset\\Editor\\Icons\\EmptyActor.png";
	
	constexpr const char* DefaultFontName = "Default";
	constexpr const char* DefaultFontPath = "Asset/Font/FontAtlas.dds";

	FString ToLower(FString Value)
	{
		for (char& c : Value)
		{
			c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
		}
		return Value;
	}

	bool IsSupportedTextureExtension(const std::filesystem::path& Path)
	{
		const FString Ext = ToLower(Path.extension().string());
		return Ext == ".dds" || Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg" || Ext == ".bmp";
	}

	FString ToResourcePath(const std::filesystem::path& FullPath)
	{
		const std::filesystem::path RelativePath = std::filesystem::relative(FullPath, std::filesystem::path(FPaths::RootDir()));
		return FPaths::ToUtf8(RelativePath.generic_wstring());
	}

	TArray<std::filesystem::path> GetTextureFilesInDirectory(const std::wstring& RelativeDir)
	{
		TArray<std::filesystem::path> Files;
		const std::filesystem::path FullDir = std::filesystem::path(FPaths::RootDir()) / RelativeDir;
		if (!std::filesystem::exists(FullDir))
		{
			return Files;
		}

		for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(FullDir))
		{
			if (Entry.is_regular_file() && IsSupportedTextureExtension(Entry.path()))
			{
				Files.push_back(Entry.path());
			}
		}

		std::sort(Files.begin(), Files.end(), [](const std::filesystem::path& A, const std::filesystem::path& B)
		{
			return A.generic_wstring() < B.generic_wstring();
		});
		return Files;
	}
}

void FResourceManager::LoadDefaultResources(ID3D11Device* InDevice)
{
	ReleaseGPUResources();
	FontResources.clear();
	ParticleResources.clear();
	TextureResources.clear();

	RegisterFont(FName(DefaultFontName), DefaultFontPath, 128, 128);

	for (const std::filesystem::path& Path : GetTextureFilesInDirectory(ParticleDir))
	{
		const FString Name = FPaths::ToUtf8(Path.stem().wstring());
		if (ParticleResources.find(Name) != ParticleResources.end())
		{
			UE_LOG("Duplicate particle resource name skipped: %s", Name.c_str());
			continue;
		}
		RegisterParticle(FName(Name), ToResourcePath(Path), 1, 1);
	}

	for (const std::filesystem::path& Path : GetTextureFilesInDirectory(TextureDir))
	{
		const FString Name = FPaths::ToUtf8(Path.stem().wstring());
		if (TextureResources.find(Name) != TextureResources.end())
		{
			UE_LOG("Duplicate texture resource name skipped: %s", Name.c_str());
			continue;
		}
		RegisterTexture(FName(Name), ToResourcePath(Path));
	}

 auto RegisterEditorIconTexture = [&](const char* TextureName, const wchar_t* IconPath)
	{
		const std::filesystem::path FullPath = std::filesystem::path(FPaths::RootDir()) / IconPath;
		if (std::filesystem::exists(FullPath) && TextureResources.find(TextureName) == TextureResources.end())
		{
			RegisterTexture(FName(TextureName), ToResourcePath(FullPath));
		}
	};

	RegisterEditorIconTexture(DecalIconTextureName, DecalIconPath);
	RegisterEditorIconTexture(PawnIconTextureName, PawnIconPath);
	RegisterEditorIconTexture(PointLightIconTextureName, PointLightIconPath);
	RegisterEditorIconTexture(SpotLightIconTextureName, SpotLightIconPath);
	RegisterEditorIconTexture(EmptyActorIconTextureName, EmptyActorIconPath);
	RegisterEditorIconTexture(EmptyActorIconTextureName, EmptyActorIconFallbackPath);

	if (LoadGPUResources(InDevice))
	{
		UE_LOG("Complete Load Resources!");
	}
	else
	{
		UE_LOG("Failed to Load Resources...");
	}
}

bool FResourceManager::LoadGPUResources(ID3D11Device* Device)
{
	if (!Device)
	{
		return false;
	}

	auto LoadSRV = [&](FTextureAtlasResource& Resource) -> bool
	{
		if (Resource.SRV)
		{
			if (Resource.TrackedMemoryBytes > 0)
			{
				MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
				Resource.TrackedMemoryBytes = 0;
			}
			Resource.SRV->Release();
			Resource.SRV = nullptr;
			Resource.Width = 0;
			Resource.Height = 0;
		}

			std::wstring FullPath = FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(Resource.Path));

			const FString ExtStr = ToLower(std::filesystem::path(Resource.Path).extension().string());

		HRESULT hr;
		if (ExtStr == ".dds")
		{
			hr = DirectX::CreateDDSTextureFromFileEx(
				Device,
				FullPath.c_str(),
				0,
				D3D11_USAGE_IMMUTABLE,
				D3D11_BIND_SHADER_RESOURCE,
				0, 0,
				DirectX::DDS_LOADER_DEFAULT,
				nullptr,
				&Resource.SRV
			);
		}
		else
		{
			// .png/.jpg/.bmp/.tga 등 — WIC 경유
			hr = DirectX::CreateWICTextureFromFileEx(
				Device,
				FullPath.c_str(),
				0,
				D3D11_USAGE_IMMUTABLE,
				D3D11_BIND_SHADER_RESOURCE,
				0, 0,
				// 이 버전의 DirectXTK 에는 PREMULTIPLY_ALPHA 플래그가 없다.
				// straight-alpha 보간으로 생기는 검은 헤일로는 PS 측 알파 컷오프(0.5) 로 회피.
				DirectX::WIC_LOADER_FORCE_RGBA32,
				nullptr,
				&Resource.SRV
			);
		}
		if (FAILED(hr) || !Resource.SRV)
		{
			return false;
		}

		ID3D11Resource* TextureResource = nullptr;
		Resource.SRV->GetResource(&TextureResource);
		Resource.TrackedMemoryBytes = MemoryStats::CalculateTextureMemory(TextureResource);
		if (TextureResource)
		{
			ID3D11Texture2D* Texture2D = nullptr;
			if (SUCCEEDED(TextureResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture2D))) && Texture2D)
			{
				D3D11_TEXTURE2D_DESC Desc = {};
				Texture2D->GetDesc(&Desc);
				Resource.Width = Desc.Width;
				Resource.Height = Desc.Height;
				Texture2D->Release();
			}
			TextureResource->Release();
		}

		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::AddTextureMemory(Resource.TrackedMemoryBytes);
		}

		return true;
	};

	for (auto& [Key, Resource] : FontResources)
	{
		if (!LoadSRV(Resource)) return false;
	}

	for (auto& [Key, Resource] : ParticleResources)
	{
		if (!LoadSRV(Resource)) return false;
	}

	for (auto& [Key, Resource] : TextureResources)
	{
		if (!LoadSRV(Resource)) return false;
	}

	return true;
}

void FResourceManager::ReleaseGPUResources()
{
	for (auto& [Key, Resource] : FontResources)
	{
		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
			Resource.TrackedMemoryBytes = 0;
		}
		if (Resource.SRV) { Resource.SRV->Release(); Resource.SRV = nullptr; }
	}
	for (auto& [Key, Resource] : ParticleResources)
	{
		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
			Resource.TrackedMemoryBytes = 0;
		}
		if (Resource.SRV) { Resource.SRV->Release(); Resource.SRV = nullptr; }
	}
	for (auto& [Key, Resource] : TextureResources)
	{
		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
			Resource.TrackedMemoryBytes = 0;
		}
		if (Resource.SRV) { Resource.SRV->Release(); Resource.SRV = nullptr; }
	}
}

// --- Font ---
FFontResource* FResourceManager::FindFont(const FName& FontName)
{
	auto It = FontResources.find(FontName.ToString());
	return (It != FontResources.end()) ? &It->second : nullptr;
}

const FFontResource* FResourceManager::FindFont(const FName& FontName) const
{
	auto It = FontResources.find(FontName.ToString());
	return (It != FontResources.end()) ? &It->second : nullptr;
}

void FResourceManager::RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	FFontResource Resource;
	Resource.Name    = FontName;
	Resource.Path    = InPath;
	Resource.Columns = Columns;
	Resource.Rows    = Rows;
	Resource.SRV     = nullptr;
	FontResources[FontName.ToString()] = Resource;
}

// --- Particle ---
FParticleResource* FResourceManager::FindParticle(const FName& ParticleName)
{
	auto It = ParticleResources.find(ParticleName.ToString());
	return (It != ParticleResources.end()) ? &It->second : nullptr;
}

const FParticleResource* FResourceManager::FindParticle(const FName& ParticleName) const
{
	auto It = ParticleResources.find(ParticleName.ToString());
	return (It != ParticleResources.end()) ? &It->second : nullptr;
}

void FResourceManager::RegisterParticle(const FName& ParticleName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	FParticleResource Resource;
	Resource.Name    = ParticleName;
	Resource.Path    = InPath;
	Resource.Columns = Columns;
	Resource.Rows    = Rows;
	Resource.SRV     = nullptr;
	ParticleResources[ParticleName.ToString()] = Resource;
}

TArray<FString> FResourceManager::GetFontNames() const
{
	TArray<FString> Names;
	Names.reserve(FontResources.size());
	for (const auto& [Key, _] : FontResources)
	{
		Names.push_back(Key);
	}
	std::sort(Names.begin(), Names.end());
	return Names;
}

TArray<FString> FResourceManager::GetParticleNames() const
{
	TArray<FString> Names;
	Names.reserve(ParticleResources.size());
	for (const auto& [Key, _] : ParticleResources)
	{
		Names.push_back(Key);
	}
	std::sort(Names.begin(), Names.end());
	return Names;
}

// --- Texture ---
FTextureResource* FResourceManager::FindTexture(const FName& TextureName)
{
	auto It = TextureResources.find(TextureName.ToString());
	return (It != TextureResources.end()) ? &It->second : nullptr;
}

const FTextureResource* FResourceManager::FindTexture(const FName& TextureName) const
{
	auto It = TextureResources.find(TextureName.ToString());
	return (It != TextureResources.end()) ? &It->second : nullptr;
}

void FResourceManager::RegisterTexture(const FName& TextureName, const FString& InPath)
{
	FTextureResource Resource;
	Resource.Name    = TextureName;
	Resource.Path    = InPath;
	Resource.Columns = 1;
	Resource.Rows    = 1;
	Resource.SRV     = nullptr;
	TextureResources[TextureName.ToString()] = Resource;
}

TArray<FString> FResourceManager::GetTextureNames() const
{
	TArray<FString> Names;
	Names.reserve(TextureResources.size());
	for (const auto& [Key, _] : TextureResources)
	{
		Names.push_back(Key);
	}
	std::sort(Names.begin(), Names.end());
	return Names;
}
