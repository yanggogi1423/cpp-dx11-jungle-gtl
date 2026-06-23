#pragma once

#include "Core/CoreTypes.h"
#include "Object/ObjectIterator.h"
#include "Render/Types/RenderTypes.h"
#include <map>
#include <string>
#include <memory>

struct FStaticMesh;
struct FStaticMaterial;
struct FImportOptions;
class UStaticMesh;
class UMaterial;

struct FMeshAssetListItem
{
	FString DisplayName;
	FString FullPath;
};

struct FMaterialAssetListItem
{
	FString DisplayName;
	FString FullPath;
};

// 에셋 로드/캐싱 관리자
class FObjManager
{
	// path → UStaticMesh* 캐시 (소유권은 UObjectManager)
	static std::map<std::string, UStaticMesh*> StaticMeshCache;
	static TMap<FString, UMaterial*> MaterialCache;
	static TArray<FMeshAssetListItem> AvailableMeshFiles;
	static TArray<FMeshAssetListItem> AvailableObjFiles;
	static TArray< FMaterialAssetListItem> AvailableMaterialFiles;


public:
	static std::string GetBinaryFilePath(const std::string& OriginalPath);

	// mbin 경로 계산 — 임포트 시 1회 호출하여 UMaterial::CachePath에 저장합니다.
	// MtlFilePath: "Data/model/model.mtl" (없으면 "")
	// SlotName: usemtl 슬롯명 원문 (예: "MatID_1.001", "None")
	// 반환: "Asset/MeshCache/{mtl_stem}/{safe_slot}.mbin"
	//        예: "Asset/MeshCache/bitten_apple_mid/MatID_1.001.mbin"
	//        SlotName이 "None"이면: "Asset/MeshCache/None.mbin"
	static FString ComputeMBinaryFilePath(const FString& MtlFilePath, const FString& SlotName);

	static UStaticMesh* LoadObjStaticMesh(const std::string& PathFileName, ID3D11Device* InDevice);
	static UStaticMesh* LoadObjStaticMesh(const FString& PathFileName, const FImportOptions& Options, ID3D11Device* InDevice);

	// CachePath를 키로 머티리얼을 RAM 캐시 또는 mbin 파일에서 로드합니다.
	// CachePath: UMaterial::CachePath와 동일한 형식 (예: "Asset/MeshCache/bitten_apple_mid/MatID_1.001.mbin")
	// mbin이 없으면 빈 UMaterial 반환 — 호출자(Convert/LoadObjStaticMesh)에서 속성 설정 및 mbin 저장 책임
	static UMaterial* GetOrLoadMaterial(const FString& CachePath);

	// 엔진 초기화 시 호출되어 "None" 머티리얼(WorldGridMaterial 역할) 기본 파일이 없으면 생성합니다.
	static void InitializeNoneMaterial();

	static void ScanMeshAssets();
	static const TArray<FMeshAssetListItem>& GetAvailableMeshFiles();
	static void ScanObjSourceFiles();
	static const TArray<FMeshAssetListItem>& GetAvailableObjFiles();
	static void ScanMaterialAssets();
	static const TArray<FMaterialAssetListItem>& GetAvailableMaterialFiles();

private:
	// Windows 파일명 금지 문자(\/:*?"<>|)를 '_'로 교체합니다.
	// 점(.)과 하이픈(-) 등은 유지됩니다. (예: "MatID_1.001" → "MatID_1.001", "Mat::Red" → "Mat__Red")
	static FString SanitizeForFilename(const FString& Input);

	static bool LoadStaticMeshAsset(const std::string& PathFileName, ID3D11Device* InDevice,
		FStaticMesh*& OutMesh, TArray<FStaticMaterial>& OutMaterials);
};
