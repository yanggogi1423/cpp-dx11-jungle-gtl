#pragma once

#include "Object/ObjectFactory.h"
#include "Math/Vector.h"

class UTexture2D;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
class FArchive;

enum class EMaterialType : uint32
{
	Master = 0,
	Instance = 1
};

class UMaterialInterface : public UObject
{
public:
  DECLARE_CLASS(UMaterialInterface, UObject)

	FString PathFileName;

	virtual EMaterialType GetMaterialType() const = 0;

	const FString& GetAssetPathFileName() const;
	void Serialize(FArchive& Ar) override;

	// 파라미터 Getter
	virtual UTexture2D* GetDiffuseTexture() const;
	virtual FVector4 GetDiffuseColor() const;

	// 셰이더 Getter (렌더러가 파이프라인에 바인딩할 때 사용)
    virtual ID3D11VertexShader* GetVertexShader() const;
	virtual ID3D11PixelShader* GetPixelShader() const;
	virtual ID3D11InputLayout* GetInputLayout() const;
};
