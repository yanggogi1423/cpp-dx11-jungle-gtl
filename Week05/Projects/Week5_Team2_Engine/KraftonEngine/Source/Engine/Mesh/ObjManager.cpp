#include "Mesh/ObjManager.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/ObjImporter.h"
#include "Materials/Material.h"
#include "Editor/UI/EditorConsoleWidget.h"
#include "Serialization/WindowsArchive.h"
#include "Engine/Platform/Paths.h"
#include <filesystem>
#include <algorithm>
#include <chrono>

std::map<FString, UStaticMesh*> FObjManager::StaticMeshCache;
TMap<FString, UMaterial*> FObjManager::MaterialCache;
TArray<FMeshAssetListItem> FObjManager::AvailableMeshFiles;
TArray<FMeshAssetListItem> FObjManager::AvailableObjFiles;
TArray<FMaterialAssetListItem> FObjManager::AvailableMaterialFiles;

FString FObjManager::GetBinaryFilePath(const FString& OriginalPath)
{
	std::filesystem::path SrcPath(FPaths::ToWide(OriginalPath));
	std::wstring Ext = SrcPath.extension().wstring();

	// 이미 bin 경로가 들어온 경우에는 그대로 사용
	if (Ext == L".bin")
	{
		return OriginalPath;
	}

	// obj 등 원본 메시 경로가 들어온 경우에는 MeshCache 아래에 bin 생성
	static bool bCacheDirCreated = false;
	if (!bCacheDirCreated)
	{
		std::wstring CacheDir = FPaths::RootDir() + L"Asset\\MeshCache\\";
		FPaths::CreateDir(CacheDir);
		bCacheDirCreated = true;
	}

	// 상대 경로로 반환
	std::filesystem::path RelPath = std::filesystem::path(L"Asset\\MeshCache") / SrcPath.stem();
	RelPath += L".bin";

	return FPaths::ToUtf8(RelPath.generic_wstring());
}

FString FObjManager::SanitizeForFilename(const FString& Input)
{
	// Windows 파일명 금지 문자 \ / : * ? " < > | 를 '_'로 교체합니다.
	// 점(.)과 하이픈(-) 등 대부분의 문자는 그대로 유지됩니다.
	// 예: "MatID_1.001" → "MatID_1.001"  (변화 없음)
	//     "Mat::Red"    → "Mat__Red"
	//     "a/b\\c"      → "a_b_c"
	static constexpr char Forbidden[] = R"(\/:*?"<>|)";
	FString Result = Input;
	for (char& C : Result)
	{
		if (std::strchr(Forbidden, C))
			C = '_';
	}
	return Result;
}

FString FObjManager::ComputeMBinaryFilePath(const FString& MtlFilePath, const FString& SlotName)
{
	// MtlFilePath: "Data/model/model.mtl" 또는 "".
	// SlotName: usemtl 원문.
	// 반환 예시: "Asset/MeshCache/model/{safe_material}.mbin", "Asset/MeshCache/None.mbin"

	// MeshCache 루트 디렉토리 보장
	static bool bCacheDirCreated = false;
	if (!bCacheDirCreated)
	{
		FPaths::CreateDir(FPaths::RootDir() + L"Asset\\MeshCache\\");
		bCacheDirCreated = true;
	}

	// MTL stem을 하위 폴더명으로 사용, 비어 있으면 루트 사용
	FString Prefix;
	if (!MtlFilePath.empty())
	{
		std::filesystem::path MtlPath(FPaths::ToWide(MtlFilePath));
		Prefix = FPaths::ToUtf8(MtlPath.stem().wstring()); // 확장자 제외 파일명
	}

	// 슬롯명에서 파일시스템 금지 문자만 치환
	FString SafeSlotName = SanitizeForFilename(SlotName);

	// 경로 생성
	std::filesystem::path RelPath = std::filesystem::path(L"Asset\\MeshCache");

	if (!Prefix.empty())
	{
		// Prefix가 있으면 하위 디렉토리 생성
		RelPath /= FPaths::ToWide(Prefix);

		// 디렉토리 보장
		std::wstring FullDir = FPaths::RootDir() + RelPath.wstring() + L"\\";
		FPaths::CreateDir(FullDir);
	}

	// 최종 mbin 경로
	RelPath /= FPaths::ToWide(SafeSlotName);
	RelPath += L".mbin";

	return FPaths::ToUtf8(RelPath.generic_wstring());
}

void FObjManager::ScanMeshAssets()
{
	AvailableMeshFiles.clear();

	const std::filesystem::path MeshCacheRoot = FPaths::RootDir() + L"Asset\\MeshCache\\";

	if (!std::filesystem::exists(MeshCacheRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(MeshCacheRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();
		if (Path.extension() != L".bin") continue;

		FMeshAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Path.stem().wstring());
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
		AvailableMeshFiles.push_back(std::move(Item));
	}
}

void FObjManager::ScanMaterialAssets()
{
	AvailableMaterialFiles.clear();

	// .mbin은 MeshCache 하위 폴더까지 재귀 스캔
	const std::filesystem::path MeshCacheRoot = FPaths::RootDir() + L"Asset\\MeshCache\\";

	if (!std::filesystem::exists(MeshCacheRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(MeshCacheRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();

		// .mbin만 대상
		if (Path.extension() != L".mbin") continue;
		if (Path.stem() == L"None") continue; // Fallback 머티리얼은 목록에서 제외

		FMaterialAssetListItem Item;
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());

		// DisplayName: "부모폴더 / 파일명" (루트면 파일명만)
		FString Stem = FPaths::ToUtf8(Path.stem().wstring());
		FString ParentDirName = FPaths::ToUtf8(Path.parent_path().filename().wstring());

		if (ParentDirName == "MeshCache" || ParentDirName == "None" || ParentDirName.empty())
		{
			Item.DisplayName = Stem;
		}
		else
		{
			Item.DisplayName = ParentDirName + " / " + Stem;
		}

		AvailableMaterialFiles.push_back(std::move(Item));
	}
}

void FObjManager::ScanObjSourceFiles()
{
	AvailableObjFiles.clear();

	const std::filesystem::path DataRoot = FPaths::RootDir() + L"Data\\";

	if (!std::filesystem::exists(DataRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());


	for (const auto& Entry : std::filesystem::recursive_directory_iterator(DataRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();
		std::wstring Ext = Path.extension().wstring();

		// 대소문자 무시
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		if (Ext != L".obj") continue;

		FMeshAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Path.filename().wstring());
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
		AvailableObjFiles.push_back(std::move(Item));
	}
}

const TArray<FMeshAssetListItem>& FObjManager::GetAvailableMeshFiles()
{
	return AvailableMeshFiles;
}

const TArray<FMaterialAssetListItem>& FObjManager::GetAvailableMaterialFiles()
{
	return AvailableMaterialFiles;
}

const TArray<FMeshAssetListItem>& FObjManager::GetAvailableObjFiles()
{
	return AvailableObjFiles;
}

UStaticMesh* FObjManager::LoadObjStaticMesh(const FString& PathFileName, const FImportOptions& Options, ID3D11Device* InDevice)
{
	FString CacheKey = GetBinaryFilePath(PathFileName);
	auto ImportStartTime = std::chrono::high_resolution_clock::now();
	UE_LOG("[IMPORT] OBJ BEGIN src=%s key=%s", PathFileName.c_str(), CacheKey.c_str());

	UStaticMesh* StaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
	FStaticMesh* NewMeshAsset = new FStaticMesh();
	TArray<FStaticMaterial> ParsedMaterials;

	// 원본 OBJ 파일 파싱
	if (!FObjImporter::Import(PathFileName, Options, *NewMeshAsset, ParsedMaterials))
	{
		delete NewMeshAsset;

		auto ImportEndTime = std::chrono::high_resolution_clock::now();
		const double ImportMs = std::chrono::duration<double, std::milli>(ImportEndTime - ImportStartTime).count();
		UE_LOG("[IMPORT] OBJ END result=FAIL src=%s key=%s time_ms=%.3f",
			PathFileName.c_str(),
			CacheKey.c_str(),
			ImportMs);

		UObjectManager::Get().DestroyObject(StaticMesh);
		return nullptr;
	}

	// 파싱된 각 머티리얼을 .mbin 으로 캐시 저장
	for (auto& Mat : ParsedMaterials)
	{
		if (Mat.MaterialInterface)
		{
			// MatCachePath: 예) "Asset/MeshCache/model_MatID.mbin"
			const FString& MatCachePath = Mat.MaterialInterface->CachePath;

			// 디스크에 직렬화 저장
			FWindowsBinWriter MatWriter(MatCachePath);
			if (MatWriter.IsValid())
			{
				Mat.MaterialInterface->Serialize(MatWriter);
			}
		}
	}

	NewMeshAsset->PathFileName = PathFileName;
	StaticMesh->SetStaticMeshAsset(NewMeshAsset);
	StaticMesh->SetStaticMaterials(std::move(ParsedMaterials));

	// 완성된 StaticMesh를 .bin 으로 캐시 저장
	FWindowsBinWriter Writer(CacheKey);
	if (Writer.IsValid())
	{
		StaticMesh->Serialize(Writer);
	}

	// Import 직후 에디터 드롭다운이 즉시 갱신되도록 캐시 목록을 재스캔
	ScanMeshAssets();
	ScanMaterialAssets();

	// GPU 리소스 생성 및 메모리 캐시 등록
	StaticMesh->InitResources(InDevice);
	StaticMeshCache[CacheKey] = StaticMesh;

	auto ImportEndTime = std::chrono::high_resolution_clock::now();
	const double ImportMs = std::chrono::duration<double, std::milli>(ImportEndTime - ImportStartTime).count();
	UE_LOG("[IMPORT] OBJ END result=SUCCESS src=%s key=%s time_ms=%.3f",
		PathFileName.c_str(),
		CacheKey.c_str(),
		ImportMs);

	return StaticMesh;
}

// 캐시 기반 로드 (캐시가 없거나 원본이 더 최신이면 자동 임포트=재빌드)
UStaticMesh* FObjManager::LoadObjStaticMesh(const std::string& PathFileName, ID3D11Device* InDevice)
{
	FString CacheKey = GetBinaryFilePath(PathFileName);

	// 1. RAM 캐시 조회
	if (auto It = StaticMeshCache.find(CacheKey); It != StaticMeshCache.end())
	{
		return It->second;
	}

	// 2. 디스크 캐시(.bin) 최신 여부 확인
	bool bNeedRebuild = true;
	std::filesystem::path BinPathW(FPaths::ToWide(CacheKey));
	std::filesystem::path PathFileNameW(FPaths::ToWide(PathFileName));

	if (std::filesystem::exists(BinPathW))
	{
		// 원본 파일이 없거나, 원본 파일이 .bin 자체이거나, 캐시가 원본보다 최신이면 재사용
		if (!std::filesystem::exists(PathFileNameW) || PathFileName == CacheKey ||
			std::filesystem::last_write_time(BinPathW) >= std::filesystem::last_write_time(PathFileNameW))
		{
			bNeedRebuild = false;
		}
	}

	// 3. 디스크 캐시에서 로드
	if (!bNeedRebuild)
	{
		UStaticMesh* StaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
		FWindowsBinReader Reader(CacheKey);

		if (Reader.IsValid())
		{
			StaticMesh->Serialize(Reader);

			StaticMesh->InitResources(InDevice);
			StaticMeshCache[CacheKey] = StaticMesh;
			return StaticMesh;
		}
		else
		{
			UObjectManager::Get().DestroyObject(StaticMesh);
			StaticMeshCache.erase(CacheKey);
			bNeedRebuild = true; // 읽기 실패 시 강제 재빌드로 폴백
		}
	}

	// 4. 캐시 실패 또는 최신화 필요 시 강제 재빌드(Import)로 위임
	if (bNeedRebuild)
	{
		StaticMeshCache.erase(CacheKey);

		if (PathFileName == CacheKey)
		{
			return nullptr;
		}

		return LoadObjStaticMesh(PathFileName, FImportOptions::Default(), InDevice);
	}

	return nullptr;
}

UMaterial* FObjManager::GetOrLoadMaterial(const FString& CachePath)
{
	// CachePath 예시: "Asset/MeshCache/model/material.mbin"

	// 1. RAM 캐시 조회
	if (MaterialCache.contains(CachePath))
	{
		return MaterialCache[CachePath];
	}

	// 2. 빈 UMaterial 생성
	UMaterial* NewMaterial = UObjectManager::Get().CreateObject<UMaterial>();

	// 3. 디스크(.mbin)에서 복원 시도
	std::filesystem::path MBinPathW = FPaths::ToWide(CachePath);
	if (std::filesystem::exists(MBinPathW))
	{
		FWindowsBinReader Reader(CachePath);
		if (Reader.IsValid())
		{
			// Serialize 과정에서 MaterialName, DiffuseTextureFilePath 등이 복원됨
			NewMaterial->Serialize(Reader);
		}
	}

	// 4. RAM 캐시 등록 후 반환
	MaterialCache[CachePath] = NewMaterial;
	return NewMaterial;
}

void FObjManager::InitializeNoneMaterial()
{
	FString NoneCachePath = ComputeMBinaryFilePath("", "None");
	std::filesystem::path MBinPathW = FPaths::ToWide(NoneCachePath);

	// 파일이 없으면 기본 WorldGridMaterial(None) 생성
	if (!std::filesystem::exists(MBinPathW))
	{
		UMaterial* NoneMaterial = UObjectManager::Get().CreateObject<UMaterial>();
		NoneMaterial->MaterialName = "None";
		NoneMaterial->CachePath = NoneCachePath;
		NoneMaterial->DiffuseColor = FVector4(1.0f, 0.0f, 1.0f, 1.0f); // Magenta

		FWindowsBinWriter Writer(NoneCachePath);
		if (Writer.IsValid())
		{
			NoneMaterial->Serialize(Writer);
		}
	}
}