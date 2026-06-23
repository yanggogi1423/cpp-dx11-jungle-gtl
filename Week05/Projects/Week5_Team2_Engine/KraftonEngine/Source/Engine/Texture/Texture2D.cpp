#include "Texture/Texture2D.h"
#include "Object/ObjectFactory.h"
#include "UI/EditorConsoleWidget.h"
#include "Platform/Paths.h"
#include "WICTextureLoader.h"

#include <d3d11.h>
#include <filesystem>

IMPLEMENT_CLASS(UTexture2D, UObject)

std::map<FString, UTexture2D*> UTexture2D::TextureCache;

UTexture2D::~UTexture2D()
{
	if (SRV)
	{
		if (TrackedTextureMemory > 0)
		{
			MemoryStats::SubTextureMemory(TrackedTextureMemory);
			TrackedTextureMemory = 0;
		}

		SRV->Release();
		SRV = nullptr;
	}

	// 캐시에서 제거
	auto It = TextureCache.find(SourceFilePath);
	if (It != TextureCache.end() && It->second == this)
	{
		TextureCache.erase(It);
	}
}

UTexture2D* UTexture2D::LoadFromFile(const FString& FilePath, ID3D11Device* Device)
{
	if (FilePath.empty() || !Device) return nullptr;

	// 캐시 히트
	auto It = TextureCache.find(FilePath);
	if (It != TextureCache.end())
	{
		return It->second;
	}

	// 새 UTexture2D 생성
	UTexture2D* Texture = UObjectManager::Get().CreateObject<UTexture2D>();
	if (!Texture->LoadInternal(FilePath, Device))
	{
		UObjectManager::Get().DestroyObject(Texture);
		return nullptr;
	}

	TextureCache[FilePath] = Texture;
	return Texture;
}

bool UTexture2D::LoadInternal(const FString& FilePath, ID3D11Device* Device)
{
	std::wstring WidePath = FPaths::ToWide(FilePath);

	// DirectX::DX11::WIC_LOADER_FLAGS LoadFlags = DirectX::DX11::WIC_LOADER_FORCE_SRGB;
	DirectX::DX11::WIC_LOADER_FLAGS LoadFlags = DirectX::DX11::WIC_LOADER_IGNORE_SRGB;

	ID3D11Resource* Resource = nullptr;
	HRESULT hr = DirectX::CreateWICTextureFromFileEx(
		Device,
		WidePath.c_str(),
		0, // MaxSize
		D3D11_USAGE::D3D11_USAGE_DEFAULT,
		D3D11_BIND_SHADER_RESOURCE,
		0, // CPUAccessFlags
		0, // MiscFlags
		LoadFlags,
		&Resource,
		&SRV
	);

	if (FAILED(hr))
	{
		UE_LOG("Failed to load texture: %s", FilePath.c_str());
		return false;
	}

	// 텍스처 크기 추출
	if (Resource)
	{
		TrackedTextureMemory = MemoryStats::CalculateTextureMemory(Resource);

		ID3D11Texture2D* Tex2D = nullptr;
		if (SUCCEEDED(Resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&Tex2D)))
		{
			D3D11_TEXTURE2D_DESC Desc;
			Tex2D->GetDesc(&Desc);
			Width = Desc.Width;
			Height = Desc.Height;
			Tex2D->Release();
		}

		if (TrackedTextureMemory > 0)
		{
			MemoryStats::AddTextureMemory(TrackedTextureMemory);
		}
		Resource->Release();
	}

	SourceFilePath = FilePath;
	return true;
}
