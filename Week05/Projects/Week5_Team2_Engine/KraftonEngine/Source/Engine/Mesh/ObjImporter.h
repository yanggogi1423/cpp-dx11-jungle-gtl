#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"

struct FStaticMesh;
struct FStaticMaterial;

// 파싱 전용 섹션 — 직렬화되지 않음. usemtl 이름으로 구간을 식별합니다.
struct FRawMeshSection
{
	FString MaterialNameInObjFile = "None";
	uint32 FirstIndex = 0;
	uint32 NumTriangles = 0;
};

// Raw Data — OBJ 파싱 직후 상태
struct FObjInfo
{
	TArray<FVector>  Positions;       // v
	TArray<FVector2> UVs;             // vt
	TArray<FVector>  Normals;         // vn
	TArray<uint32>   PosIndices;      // f - position index
	TArray<uint32>   UVIndices;       // f - uv index
	TArray<uint32>   NormalIndices;   // f - normal index

	FString ObjectName; // object name (optional)

	FString MaterialLibraryFilePath;
	TArray<FRawMeshSection> Sections; // 파싱 단계 전용, Convert 후 FStaticMeshSection(MaterialIndex)으로 변환됨
};

// MTL 재질 정보
struct FObjMaterialInfo
{
	FString MaterialNameInMtlFile = "None"; // newmtl
	FVector Kd; // diffuse color
	FString map_Kd; // diffuse texture file path

	FVector Ka; // ambient color
	FVector Ks; // specular color
	float Ns; // specular exponent
	float Ni; // optical density
	int32 illum; // illumination model
};

// TODO: 좌표계를 정의하기 위해서는 ForwardAxis와 UpAxis가 모두 필요함.
enum class EForwardAxis : uint8
{
	X, NegX,   // +X, -X
	Y, NegY,   // +Y, -Y
	Z, NegZ    // +Z, -Z
};

enum class EWindingOrder : uint8
{
	CCW_to_CW,  // OBJ CCW → DX CW (인덱스 [0,2,1]) — 기본값
	Keep         // 원본 유지 [0,1,2]
};

struct FImportOptions
{
	float Scale = 1.0f;
	EForwardAxis ForwardAxis = EForwardAxis::NegY;  // UE/Blender 파이프라인 기본: Z-up, -Y Forward
	EWindingOrder WindingOrder = EWindingOrder::Keep; // UE 스타일 기본: 좌표 변환 후 원본 와인딩 유지
	float YawOffsetDegrees = -90.0f; // UE 호환 보정: 최종 Z축 -90도 회전
	static FImportOptions Default() { return {}; }
};

// OBJ/MTL 파싱 + Raw→Cooked 변환
struct FObjImporter
{
	static bool Import(const FString& ObjFilePath, FStaticMesh& OutMesh, TArray<FStaticMaterial>& OutMaterials);
	static bool Import(const FString& ObjFilePath, const FImportOptions& Options, FStaticMesh& OutMesh, TArray<FStaticMaterial>& OutMaterials);
private:
	static bool ParseObj(const FString& ObjFilePath, FObjInfo& OutObjInfo);
	static bool ParseMtl(const FString& MtlFilePath, TArray<FObjMaterialInfo>& OutMaterials);
	static bool Convert(const FObjInfo& ObjInfo, const TArray<FObjMaterialInfo>& MtlInfos, const FImportOptions& Options, FStaticMesh& OutMesh, TArray<FStaticMaterial>& OutMaterials);

	static FVector RemapPosition(const FVector& ObjPos, EForwardAxis Axis);
};
