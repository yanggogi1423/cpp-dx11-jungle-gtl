#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Engine/Object/Object.h"
#include "Render/Resource/Buffer.h"
#include "Serialization/Archive.h"
#include "Engine/Object/FName.h"
#include "Engine/Mesh/ObjManager.h"
#include "Collision/StaticMeshBVH.h"
#include "Materials/Material.h"
#include "Editor/UI/EditorConsoleWidget.h"
#include <memory>

// Cooked Data 내부용 정점
struct FNormalVertex
{
	FVector pos;
	FVector normal;
	FVector4 color;
	FVector2 tex;
};


struct FStaticMeshSection
{
	int32 MaterialIndex = -1; // Index into UStaticMesh's StaticMaterials array.
	uint32 FirstIndex = 0;
	uint32 NumTriangles = 0;

	friend FArchive& operator<<(FArchive& Ar, FStaticMeshSection& Section)
	{
		Ar << Section.MaterialIndex << Section.FirstIndex << Section.NumTriangles;
		return Ar;
	}
};

struct FStaticMaterial
{
	// std::shared_ptr<class UMaterialInterface> MaterialInterface;
	UMaterial* MaterialInterface;
	// TODO: FName 으로 사용
	FString MaterialSlotName = "None"; // "None"은 특별한 슬롯 이름으로, OBJ 파일에서 머티리얼이 지정되지 않은 섹션에 할당됩니다.
	// FName ImportedMaterialSlotName;

	friend FArchive& operator<<(FArchive& Ar, FStaticMaterial& Mat)
	{
		// 1. 슬롯 이름 직렬화 (메시 섹션과 매핑용)
		Ar << Mat.MaterialSlotName;

		// 2. 머티리얼 식별자(CachePath) 직렬화
		// CachePath 예시: "Asset/MeshCache/bitten_apple_mid/MatID_1.001.mbin"
		FString MatPath;
		if (Ar.IsSaving() && Mat.MaterialInterface)
		{
			// UMaterial::CachePath를 직접 사용 — ComputeMBinaryFilePath()로 임포트 시 1회 계산된 값
			MatPath = Mat.MaterialInterface->CachePath;
		}

		Ar << MatPath; // 메시 bin 파일에는 문자열(경로) 1개만 저장됩니다.

		// 3. 로딩 시 해당 경로에서 머티리얼 에셋 로드
		if (Ar.IsLoading())
		{
			if (!MatPath.empty())
			{
				// 저장된 경로를 이용해 별도의 bin 파일에서 로드
				Mat.MaterialInterface = FObjManager::GetOrLoadMaterial(MatPath);
			}
			else
			{
				Mat.MaterialInterface = nullptr;
			}
		}

		return Ar;
	}
};

// Cooked Data — GPU용 정점/인덱스
// FStaticMeshLODResources in UE5
struct FStaticMesh
{
	FString PathFileName;
	TArray<FNormalVertex> Vertices;
	TArray<uint32> Indices;

	TArray<FStaticMeshSection> Sections;

	std::unique_ptr<FMeshBuffer> RenderBuffer;
	std::unique_ptr<FStaticMeshBVH> BVH;

	void BuildBVH()
	{
		if (Vertices.empty() || Indices.size() < 3)
		{
			BVH.reset();
			return;
		}

		if (!BVH)
		{
			BVH = std::make_unique<FStaticMeshBVH>();
		}

		BVH->Build(&Vertices[0].pos, sizeof(FNormalVertex), Indices);
	}

	const FStaticMeshBVH* GetBVH() const
	{
		return BVH.get();
	}

	void Serialize(FArchive& Ar)
	{
		Ar << PathFileName;
		Ar << Vertices;
		Ar << Indices;
		Ar << Sections;
	}
};