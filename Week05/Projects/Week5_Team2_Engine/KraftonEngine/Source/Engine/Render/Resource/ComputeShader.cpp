#include "ComputeShader.h"
#include "Profiling/MemoryStats.h"
#include <iostream>

FComputeShader::FComputeShader(FComputeShader&& Other) noexcept
	: ComputeShader(Other.ComputeShader)
	, CachedComputeShaderSize(Other.CachedComputeShaderSize)
{
	Other.ComputeShader = nullptr;
	Other.CachedComputeShaderSize = 0;
}

FComputeShader& FComputeShader::operator=(FComputeShader&& Other) noexcept
{
	if (this != &Other)
	{
		Release();
		ComputeShader = Other.ComputeShader;
		CachedComputeShaderSize = Other.CachedComputeShaderSize;
		Other.ComputeShader = nullptr;
		Other.CachedComputeShaderSize = 0;
	}
	return *this;
}

bool FComputeShader::Create(ID3D11Device* InDevice, const wchar_t* InFilePath, const char* InCSEntryPoint,
	const D3D_SHADER_MACRO* InDefines)
{
	Release();

	ID3DBlob* computeShaderCSO = nullptr;
	ID3DBlob* errorBlob = nullptr;

	// Compute Shader 컴파일
	HRESULT hr = D3DCompileFromFile(InFilePath, InDefines, D3D_COMPILE_STANDARD_FILE_INCLUDE, InCSEntryPoint, "cs_5_0", 0, 0, &computeShaderCSO, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob)
		{
			MessageBoxA(nullptr, (char*)errorBlob->GetBufferPointer(), "Compute Shader Compile Error", MB_OK | MB_ICONERROR);
			errorBlob->Release();
		}
		return false;
	}

	// Compute Shader 생성
	hr = InDevice->CreateComputeShader(computeShaderCSO->GetBufferPointer(), computeShaderCSO->GetBufferSize(), nullptr, &ComputeShader);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create Compute Shader (HRESULT: " << hr << ")" << std::endl;
		computeShaderCSO->Release();
		return false;
	}

	CachedComputeShaderSize = computeShaderCSO->GetBufferSize();
	MemoryStats::AddComputeShaderMemory(static_cast<uint32>(CachedComputeShaderSize));

	computeShaderCSO->Release();
	return true;
}

void FComputeShader::Release()
{
	if (ComputeShader)
	{
		MemoryStats::SubComputeShaderMemory(static_cast<uint32>(CachedComputeShaderSize));
		CachedComputeShaderSize = 0;

		ComputeShader->Release();
		ComputeShader = nullptr;
	}
}

void FComputeShader::Bind(ID3D11DeviceContext* InDeviceContext) const
{
	InDeviceContext->CSSetShader(ComputeShader, nullptr, 0);
}

void FComputeShader::Unbind(ID3D11DeviceContext* InDeviceContext) const
{
	InDeviceContext->CSSetShader(nullptr, nullptr, 0);
}

void FComputeShader::Dispatch(ID3D11DeviceContext* InDeviceContext, uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) const
{
	InDeviceContext->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}
