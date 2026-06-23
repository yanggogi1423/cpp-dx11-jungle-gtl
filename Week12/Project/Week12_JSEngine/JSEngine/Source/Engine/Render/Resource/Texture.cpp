#include "Texture.h"
#include "Core/Paths.h"
#include "Core/Logging/Log.h"

#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"

bool UTexture::LoadFromFile(const FString& InFilePath, ID3D11Device* InDevice)
{
	FilePath = InFilePath;
	if (InFilePath.empty() || InDevice == nullptr)
	{
		return false;
	}

	std::wstring FullPath = FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(InFilePath));

	HRESULT hr;
	ID3D11ShaderResourceView* NewSRV = nullptr;
	if (FullPath.size() >= 4 && FullPath.substr(FullPath.size() - 4) == L".dds")
	{
		hr = DirectX::CreateDDSTextureFromFile(InDevice, FullPath.c_str(), nullptr, &NewSRV);
	}
	else
	{
		hr = DirectX::CreateWICTextureFromFileEx(
			InDevice,
			FullPath.c_str(),
			0,
			D3D11_USAGE_DEFAULT,
			D3D11_BIND_SHADER_RESOURCE,
			0,
			0,
			DirectX::WIC_LOADER_FORCE_RGBA32,
			nullptr,
			&NewSRV);
	}

	if (FAILED(hr))
	{
		UE_LOG_WARNING("Failed to load texture: %s", InFilePath.c_str());
		return false;
	}

	TextureData.Release();
	TextureData.SRV = NewSRV;
	return true;
}
