#include "Materials/Material.h"
#include "Serialization/Archive.h"
#include "Engine/Platform/Paths.h"
#include <filesystem>

IMPLEMENT_CLASS(UMaterial, UObject)

const FString& UMaterial::GetDisplayName() const
{
	// DisplayName을 처음 요청할 때 한 번만 계산하고 캐시
	if (DisplayName.empty() && !CachePath.empty())
	{
		// 추출
		std::filesystem::path Path(FPaths::ToWide(CachePath));
		FString Stem = FPaths::ToUtf8(Path.stem().wstring());
		FString ParentDirName = FPaths::ToUtf8(Path.parent_path().filename().wstring());

		if (ParentDirName == "MeshCache" || ParentDirName == "None" || ParentDirName.empty())
		{
			DisplayName = Stem;
		}
		else
		{
			DisplayName = ParentDirName + " / " + Stem;
		}
	}
	return DisplayName;
}

const FString& UMaterial::GetCachePath() const
{
	// 반환 예시: "Asset/MeshCache/bitten_apple_mid_MatID_1.001.mbin"
	return CachePath;
}

void UMaterial::Serialize(FArchive& Ar)
{
	// 구조체나 기본 데이터 타입은 Archive.h에 정의된 operator<<를 통해
	// 읽기(IsLoading)와 쓰기(IsSaving)가 알아서 처리됩니다.

	// MaterialName: usemtl 슬롯명 원문 (예: "MatID_1.001")
	Ar << MaterialName;
	// CachePath: mbin 상대 경로 (예: "Asset/MeshCache/bitten_apple_mid_MatID_1.001.mbin")
	Ar << CachePath;
	Ar << DiffuseTextureFilePath;
	Ar << DiffuseColor;

	// 주의: 포인터인 DiffuseTexture는 주소값이므로 절대 직렬화하지 않습니다.
}