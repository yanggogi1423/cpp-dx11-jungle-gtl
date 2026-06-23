#include "Materials/MaterialInstance.h"
#include "Serialization/Archive.h"

IMPLEMENT_CLASS(UMaterialInstance, UMaterialInterface)

void UMaterialInstance::Serialize(FArchive& Ar)
{
	// 1. 공통 부모 속성(PathFileName) 직렬화
	UMaterialInterface::Serialize(Ar);

	// 2. 부모 머티리얼의 경로 직렬화 (나중에 이 경로로 Parent 포인터를 복구해야 함)
	Ar << ParentPathFileName;

	// 3. Diffuse Color 오버라이드 정보 직렬화
	Ar << bOverride_DiffuseColor;
	if (bOverride_DiffuseColor)
	{
		Ar << OverriddenDiffuseColor;
	}

	// 4. Diffuse Texture 오버라이드 정보 직렬화
	Ar << bOverride_DiffuseTexture;
	if (bOverride_DiffuseTexture)
	{
		Ar << OverriddenDiffuseTexturePath;
	}

	// 주의: 포인터인 Parent나 OverriddenDiffuseTexture는 주소값이므로 직렬화하지 않습니다.
	// 이 포인터들은 로딩이 끝난 후 ObjManager나 PostLoad 단계에서 Path 정보를 바탕으로 복구됩니다.
}