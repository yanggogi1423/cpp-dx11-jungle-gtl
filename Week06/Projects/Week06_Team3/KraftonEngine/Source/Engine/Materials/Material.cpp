#include "Materials/Material.h"
#include "Materials/Material.h"
#include "Serialization/Archive.h"

IMPLEMENT_CLASS(UMaterial, UMaterialInterface)

void UMaterial::Serialize(FArchive& Ar)
{
    UMaterialInterface::Serialize(Ar);

	// 구조체나 기본 데이터 타입은 Archive.h에 정의된 operator<<를 통해 
	// 읽기(IsLoading)와 쓰기(IsSaving)가 알아서 처리됩니다.
	// Ar << PathFileName; UMaterialInterface로 이동
	Ar << DiffuseTextureFilePath;
	Ar << DiffuseColor;
	Ar << VertexShaderFilePath;
	Ar << PixelShaderFilePath;

	// 주의: 포인터인 DiffuseTexture는 주소값이므로 절대 직렬화하지 않습니다.
}