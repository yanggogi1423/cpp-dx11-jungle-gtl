#pragma once

#include "Object/ObjectFactory.h"
#include "Math/Vector.h"

class UTexture2D;
class FArchive;

// class UMaterialInterface
// {
// };

class UMaterial : public UObject // : public UMaterialInterface
{
public:
	DECLARE_CLASS(UMaterial, UObject)

	// usemtl 슬롯명 원문 (예: "MatID_1.001", "Mat::Red", "None") — 직렬화용
	// 파일시스템 금지 문자(\/:*?"<>|)가 포함될 수 있으므로 식별자로는 사용하지 않습니다.
	FString MaterialName;

	// mbin 상대 경로 (예: "Asset/MeshCache/bitten_apple_mid_MatID_1.001.mbin") — 진짜 식별자
	// ComputeMBinaryFilePath()에서 1회 계산되며, 직렬화/캐시 키/파일 저장 경로로 사용됩니다.
	// "None" 머티리얼은 "Asset/MeshCache/None.mbin" (디스크에는 저장되지 않음)
	FString CachePath;

	// GUI 표시용 친화적 이름 (computed once, cached)
	// 예: "bitten_apple_mid / MatID_1.001"
	mutable FString DisplayName;

	FString DiffuseTextureFilePath;
	FVector4 DiffuseColor = FVector4(1.0f, 0.0f, 1.0f, 1.0f);
	UTexture2D* DiffuseTexture = nullptr;	// UObjectManager 소유, 여기선 참조만

public:
	// 에디터 표시용 이름 반환 — 캐시된 DisplayName (computed on first call)
	const FString& GetDisplayName() const;
	// 식별자/경로 반환 — mbin 상대 경로
	const FString& GetCachePath() const;
	void Serialize(FArchive& Ar);
};
