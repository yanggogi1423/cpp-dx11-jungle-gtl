#pragma once

#include <string>
#include <filesystem>
#include "Core/CoreTypes.h"
#include "Platform/Paths.h"
#include "GameFramework/WorldContext.h"
#include "Math/Vector.h"

// Forward declarations
class UObject;
class UWorld;
class AActor;
class UActorComponent;
class USceneComponent;

namespace json
{
	class JSON;
}

#include "Core/PropertyTypes.h"

using std::string;

// Perspective 뷰포트 카메라의 씬 스냅샷 — 씬 저장/로드 시 주고받는 순수 데이터
struct FPerspectiveCameraData
{
	FVector Location = FVector(0, 0, 0);
	FVector Rotation = FVector(0, 0, 0); // Euler (Roll, Pitch, Yaw) in degrees
	float   FOV      = 3.14159265f / 3.0f;
	float   NearClip = 0.1f;
	float   FarClip  = 1000.0f;
	bool    bValid   = false;
};

class FLevelSaveManager
{
public:
	static constexpr const wchar_t* LevelExtension = L".Scene";

	static std::wstring GetSceneDirectory() { return FPaths::LevelDir(); }

	static void SaveLevelAsJSON(const string& LevelName, FWorldContext& WorldContext, const FPerspectiveCameraData* PerspectiveCamData = nullptr);
	static void LoadLevelFromJSON(const string& filepath, FWorldContext& OutWorldContext, FPerspectiveCameraData& OutCam);

	static TArray<FString> GetLevelFileList();

private:
	// ---- Serialization ----
	static json::JSON SerializeWorld(UWorld* World, const FWorldContext& Ctx, const FPerspectiveCameraData* PerspectiveCamData);
	static json::JSON SerializeActor(AActor* Actor);
	static json::JSON SerializeLevelComponentTree(USceneComponent* Comp);
	static json::JSON SerializeProperties(UActorComponent* Comp);
	static json::JSON SerializePropertyValue(const FPropertyDescriptor& Prop);

	// ---- Camera ----
	static json::JSON SerializeCamera(const FPerspectiveCameraData& CamData);
	static void DeserializeCamera(json::JSON& CamJSON, FPerspectiveCameraData& OutCam);

	// ---- Primitives ----
	static void DeserializePrimitives(json::JSON& Primitives, UWorld* World, std::unordered_map<string, AActor*>& OutCreatedActors);

	// ---- Deserialization helpers ----
	static void DeserializeSceneComponentIntoExisting(USceneComponent* Existing, json::JSON& Node, AActor* Owner);
	static USceneComponent* DeserializeSceneComponentTree(json::JSON& Node, AActor* Owner);
	static void DeserializeProperties(UActorComponent* Comp, json::JSON& PropsJSON);
	static void DeserializePropertyValue(FPropertyDescriptor& Prop, json::JSON& Value);

	static string GetCurrentTimeStamp();
};
