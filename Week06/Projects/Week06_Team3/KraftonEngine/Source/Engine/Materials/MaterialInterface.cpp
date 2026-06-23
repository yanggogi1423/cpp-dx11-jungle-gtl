#include "Materials/MaterialInterface.h"
#include "Serialization/Archive.h"

IMPLEMENT_ABSTRACT_CLASS(UMaterialInterface, UObject)

const FString& UMaterialInterface::GetAssetPathFileName() const
{
    return PathFileName;
}

void UMaterialInterface::Serialize(FArchive& Ar)
{
	Ar << PathFileName;
}

UTexture2D* UMaterialInterface::GetDiffuseTexture() const
{
    return nullptr;
}

FVector4 UMaterialInterface::GetDiffuseColor() const
{
    return FVector4(1.0f, 0.0f, 1.0f, 1.0f);
}

ID3D11VertexShader* UMaterialInterface::GetVertexShader() const
{
    return nullptr;
}

ID3D11PixelShader* UMaterialInterface::GetPixelShader() const
{
    return nullptr;
}

ID3D11InputLayout* UMaterialInterface::GetInputLayout() const
{
    return nullptr;
}
