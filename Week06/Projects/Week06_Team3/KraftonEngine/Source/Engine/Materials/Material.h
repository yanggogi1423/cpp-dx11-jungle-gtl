#pragma once

#include "Materials/MaterialInterface.h"
#include "Math/Vector.h"

class UTexture2D;
class FArchive;

class UMaterial : public UMaterialInterface
{
public:
   DECLARE_CLASS(UMaterial, UMaterialInterface)

  // FString PathFileName; // UMaterialInterface로 이동
	FString DiffuseTextureFilePath;
	FVector4 DiffuseColor = FVector4(1.0f, 0.0f, 1.0f, 1.0f);
	UTexture2D* DiffuseTexture = nullptr;	// UObjectManager 소유, 여기선 참조만

	FString VertexShaderFilePath; // 예: "Shaders/Default_VS.hlsl"
	FString PixelShaderFilePath;  // 예: "Shaders/Default_PS.hlsl"

	// 런타임 DX11 리소스 (직렬화 안 함, 런타임에 로드/컴파일)
	ID3D11VertexShader* VertexShader = nullptr;
	ID3D11PixelShader* PixelShader = nullptr;
	ID3D11InputLayout* InputLayout = nullptr;

	virtual EMaterialType GetMaterialType() const override { return EMaterialType::Master; }

	// 인터페이스 구현
    UTexture2D* GetDiffuseTexture() const override { return DiffuseTexture; }
	FVector4 GetDiffuseColor() const override { return DiffuseColor; }
	ID3D11VertexShader* GetVertexShader() const override { return VertexShader; }
	ID3D11PixelShader* GetPixelShader() const override { return PixelShader; }
	ID3D11InputLayout* GetInputLayout() const override { return InputLayout; }

public:
    void Serialize(FArchive& Ar) override;
};
