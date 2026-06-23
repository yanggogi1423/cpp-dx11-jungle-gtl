#pragma once
#include "Render/Types/RenderTypes.h"
#include "Core/CoreTypes.h"

class FComputeShader
{
public:
	FComputeShader() = default;
	~FComputeShader() { Release(); }

	FComputeShader(const FComputeShader&) = delete;
	FComputeShader& operator=(const FComputeShader&) = delete;
	FComputeShader(FComputeShader&& Other) noexcept;
	FComputeShader& operator=(FComputeShader&& Other) noexcept;

	bool Create(ID3D11Device* InDevice, const wchar_t* InFilePath, const char* InCSEntryPoint,
		const D3D_SHADER_MACRO* InDefines = nullptr);
	void Release();

	void Bind(ID3D11DeviceContext* InDeviceContext) const;
	void Unbind(ID3D11DeviceContext* InDeviceContext) const;
	void Dispatch(ID3D11DeviceContext* InDeviceContext, uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) const;

private:
	ID3D11ComputeShader* ComputeShader = nullptr;
	size_t CachedComputeShaderSize = 0;
};
