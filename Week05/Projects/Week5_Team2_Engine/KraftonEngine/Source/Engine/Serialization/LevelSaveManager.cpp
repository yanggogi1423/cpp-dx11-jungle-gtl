#include "LevelSaveManager.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <unordered_map>

#include "SimpleJSON/json.hpp"
#include "GameFramework/World.h"
#include "GameFramework/Level.h"
#include "GameFramework/AActor.h"
#include "Component/SceneComponent.h"
#include "Component/ActorComponent.h"
#include "Component/StaticMeshComponent.h"
#include "GameFramework/StaticMeshActor.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Core/PropertyTypes.h"
#include "Object/FName.h"
#include "Mesh/ObjManager.h"

// ---- JSON vector helpers ---------------------------------------------------

static void WriteVec3(json::JSON& Obj, const char* Key, const FVector& V)
{
	json::JSON arr = json::Array();
	arr.append(static_cast<double>(V.X));
	arr.append(static_cast<double>(V.Y));
	arr.append(static_cast<double>(V.Z));
	Obj[Key] = arr;
}

static FVector ReadVec3(json::JSON& Arr)
{
	FVector out(0, 0, 0);
	int i = 0;
	for (auto& e : Arr.ArrayRange()) {
		if      (i == 0) out.X = static_cast<float>(e.ToFloat());
		else if (i == 1) out.Y = static_cast<float>(e.ToFloat());
		else if (i == 2) out.Z = static_cast<float>(e.ToFloat());
		++i;
	}
	return out;
}

// ---------------------------------------------------------------------------

namespace SceneKeys
{
	static constexpr const char* Version = "Version";
	static constexpr const char* Name = "Name";
	static constexpr const char* ClassName = "ClassName";
	static constexpr const char* WorldType = "WorldType";
	static constexpr const char* ContextName = "ContextName";
	static constexpr const char* ContextHandle = "ContextHandle";
	static constexpr const char* Actors = "Actors";
	static constexpr const char* Visible = "bVisible";
	static constexpr const char* RootComponent = "RootComponent";
	static constexpr const char* NonSceneComponents = "NonSceneComponents";
	static constexpr const char* Properties = "Properties";
	static constexpr const char* Children = "Children";
}

static const char* WorldTypeToString(EWorldType Type)
{
	switch (Type) {
	case EWorldType::Game: return "Game";
	case EWorldType::PIE:  return "PIE";
	default:               return "Editor";
	}
}

// ============================================================
// Save
// ============================================================

void FLevelSaveManager::SaveLevelAsJSON(const string& InLevelName, FWorldContext& WorldContext, const FPerspectiveCameraData* PerspectiveCamData)
{
	using namespace json;

	if (!WorldContext.World) return;

	string FinalName = InLevelName.empty()
		? "Save_" + GetCurrentTimeStamp()
		: InLevelName;

	std::wstring SceneDir = GetSceneDirectory();
	std::filesystem::path FileDestination = std::filesystem::path(SceneDir) / (FPaths::ToWide(FinalName) + LevelExtension);
	std::filesystem::create_directories(SceneDir);

	JSON Root = SerializeWorld(WorldContext.World, WorldContext, PerspectiveCamData);
	Root[SceneKeys::Version] = 2;
	Root[SceneKeys::Name] = FinalName;

	std::ofstream File(FileDestination);
	if (File.is_open()) {
		File << Root.dump();
		File.flush();
		File.close();
	}
}

json::JSON FLevelSaveManager::SerializeWorld(UWorld* World, const FWorldContext& Ctx, const FPerspectiveCameraData* PerspectiveCamData)
{
	using namespace json;
	JSON w = json::Object();
	w[SceneKeys::ClassName] = World->GetTypeInfo()->name;
	w[SceneKeys::WorldType] = WorldTypeToString(Ctx.WorldType);
	w[SceneKeys::ContextName] = Ctx.ContextName;
	w[SceneKeys::ContextHandle] = Ctx.ContextHandle.ToString();

	// ---- Primitives: gather static mesh components into a top-level block
	JSON Primitives = json::Object();
	std::unordered_map<AActor*, string> ActorPrimitiveKey;

	for (AActor* Actor : World->GetActors()) {
		if (!Actor) continue;

		for (UActorComponent* Comp : Actor->GetComponents()) {
			if (!Comp) continue;
			if (Comp->IsVisualizationComponent()) continue;
			if (!Comp->IsA<UStaticMeshComponent>()) continue;

			UStaticMeshComponent* S = static_cast<UStaticMeshComponent*>(Comp);

			JSON p = json::Object();

			const FMatrix& M = S->GetWorldMatrix();
			FVector loc = M.GetLocation();
			FVector rot = M.GetEuler();
			FVector scale = M.GetScale();

			p["ObjStaticMeshAsset"] = S->GetStaticMeshPath();
			WriteVec3(p, "Location", loc);
			WriteVec3(p, "Rotation", rot);
			WriteVec3(p, "Scale",    scale);
			p["Type"] = "StaticMeshComp";

			string key = std::to_string(Actor->GetUUID());
			Primitives[key] = p;
			ActorPrimitiveKey[Actor] = key;
			break;
		}
	}

	if (Primitives.size() > 0) {
		w["Primitives"] = Primitives;
	}

	// ---- Actors: serialize and attach PrimitiveKey when present ----
	JSON Actors = json::Array();
	for (AActor* Actor : World->GetActors()) {
		if (!Actor) continue;
		JSON a = SerializeActor(Actor);
		auto it = ActorPrimitiveKey.find(Actor);
		if (it != ActorPrimitiveKey.end()) {
			a["PrimitiveKey"] = it->second;
		}
		Actors.append(a);
	}
	w[SceneKeys::Actors] = Actors;

	// ---- Perspective camera ----
	if (PerspectiveCamData && PerspectiveCamData->bValid)
	{
		JSON cam = SerializeCamera(*PerspectiveCamData);
		if (cam.size() > 0) {
			w["PerspectiveCamera"] = cam;
		}
	}

	return w;
}

json::JSON FLevelSaveManager::SerializeActor(AActor* Actor)
{
	using namespace json;
	JSON a = json::Object();
	a[SceneKeys::ClassName] = Actor->GetTypeInfo()->name;
	a[SceneKeys::Visible] = Actor->IsVisible();

	// RootComponent 트리 직렬화
	if (Actor->GetRootComponent() && !Actor->GetRootComponent()->IsVisualizationComponent()) {
		a[SceneKeys::RootComponent] = SerializeLevelComponentTree(Actor->GetRootComponent());
	}

	// Non-scene components
	JSON NonScene = json::Array();
	for (UActorComponent* Comp : Actor->GetComponents()) {
		if (!Comp) continue;
		if (Comp->IsVisualizationComponent()) continue;
		if (Comp->IsA<USceneComponent>()) continue;

		JSON c = json::Object();
		c[SceneKeys::ClassName] = Comp->GetTypeInfo()->name;
		c[SceneKeys::Properties] = SerializeProperties(Comp);
		NonScene.append(c);
	}
	a[SceneKeys::NonSceneComponents] = NonScene;

	return a;
}

json::JSON FLevelSaveManager::SerializeLevelComponentTree(USceneComponent* Comp)
{
	using namespace json;
	JSON c = json::Object();
	c[SceneKeys::ClassName] = Comp->GetTypeInfo()->name;
	c[SceneKeys::Properties] = SerializeProperties(Comp);

	JSON Children = json::Array();
	for (USceneComponent* Child : Comp->GetChildren()) {
		if (!Child) continue;
		if (Child->IsVisualizationComponent()) continue;
		Children.append(SerializeLevelComponentTree(Child));
	}
	c[SceneKeys::Children] = Children;

	return c;
}

json::JSON FLevelSaveManager::SerializeProperties(UActorComponent* Comp)
{
	using namespace json;
	JSON props = json::Object();

	TArray<FPropertyDescriptor> Descriptors;
	Comp->GetEditableProperties(Descriptors);

	for (const auto& Prop : Descriptors) {
		if (Prop.Name == "Static Mesh") continue; // Primitives 블록에 이미 저장됨
		props[Prop.Name] = SerializePropertyValue(Prop);
	}
	return props;
}

json::JSON FLevelSaveManager::SerializePropertyValue(const FPropertyDescriptor& Prop)
{
	using namespace json;

	switch (Prop.Type) {
	case EPropertyType::Bool:
		return JSON(*static_cast<bool*>(Prop.ValuePtr));

	case EPropertyType::Int:
		return JSON(*static_cast<int32*>(Prop.ValuePtr));

	case EPropertyType::Float:
		return JSON(static_cast<double>(*static_cast<float*>(Prop.ValuePtr)));

	case EPropertyType::Vec3:
	case EPropertyType::Rotator: {
		float* v = static_cast<float*>(Prop.ValuePtr);
		JSON arr = json::Array();
		arr.append(static_cast<double>(v[0]));
		arr.append(static_cast<double>(v[1]));
		arr.append(static_cast<double>(v[2]));
		return arr;
	}
	case EPropertyType::Vec4: {
		float* v = static_cast<float*>(Prop.ValuePtr);
		JSON arr = json::Array();
		arr.append(static_cast<double>(v[0]));
		arr.append(static_cast<double>(v[1]));
		arr.append(static_cast<double>(v[2]));
		arr.append(static_cast<double>(v[3]));
		return arr;
	}
	case EPropertyType::String:
	case EPropertyType::StaticMeshRef:
		return JSON(*static_cast<FString*>(Prop.ValuePtr));

	case EPropertyType::MaterialRef: {
		const FString* SlotPath = static_cast<const FString*>(Prop.ValuePtr);
		JSON obj = json::Object();
		obj["Path"] = JSON(*SlotPath);
		return obj;
	}

	case EPropertyType::ByteBool:
		return JSON(static_cast<bool>(*static_cast<uint8_t*>(Prop.ValuePtr) != 0));

	case EPropertyType::Name:
		return JSON(static_cast<FName*>(Prop.ValuePtr)->ToString());

	default:
		return JSON();
	}
}

// ---- Camera helpers ----

json::JSON FLevelSaveManager::SerializeCamera(const FPerspectiveCameraData& CamData)
{
	constexpr float Rad2Deg = 180.0f / 3.14159265358979f;

	using namespace json;
	JSON cam = json::Object();
	WriteVec3(cam, "Location", CamData.Location);
	WriteVec3(cam, "Rotation", CamData.Rotation);
	cam["FOV"] = static_cast<double>(CamData.FOV * Rad2Deg);
	cam["NearClip"] = static_cast<double>(CamData.NearClip);
	cam["FarClip"] = static_cast<double>(CamData.FarClip);
	return cam;
}

void FLevelSaveManager::DeserializePrimitives(json::JSON& Primitives, UWorld* World, std::unordered_map<string, AActor*>& OutCreatedActors)
{
	for (auto& kv : Primitives.ObjectRange()) {
		const string& Key  = kv.first;
		json::JSON    Entry = kv.second;

		if (!Entry.hasKey("Type") || Entry["Type"].ToString() != "StaticMeshComp") continue;

		string MeshPath = Entry.hasKey("ObjStaticMeshAsset") ? Entry["ObjStaticMeshAsset"].ToString() : "None";

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
		if (!Actor) continue;
		Actor->InitDefaultComponents(FString(MeshPath));
		OutCreatedActors[Key] = Actor;

		if (Entry.hasKey("Location")) Actor->SetActorLocation(ReadVec3(Entry["Location"]));
		if (Entry.hasKey("Rotation")) Actor->SetActorRotation(ReadVec3(Entry["Rotation"]));
		if (Entry.hasKey("Scale"))    Actor->SetActorScale(ReadVec3(Entry["Scale"]));
		// Material/UV overrides are applied later via RootComponent Properties
	}
}

// Competition 포맷에서 FOV/NearClip/FarClip이 단일 원소 배열([60.0])로 오는 경우를 처리
static float ReadScalarOrArray(json::JSON& Val)
{
	if (Val.JSONType() == json::JSON::Class::Array) {
		for (auto& e : Val.ArrayRange()) return static_cast<float>(e.ToFloat());
		return 0.0f;
	}
	return static_cast<float>(Val.ToFloat());
}

void FLevelSaveManager::DeserializeCamera(json::JSON& CameraJSON, FPerspectiveCameraData& OutCam)
{
	constexpr float Rad2Deg = 180.0f / 3.14159265358979f;

	using namespace json;
	if (CameraJSON.JSONType() == JSON::Class::Null) return;

	if (CameraJSON.hasKey("Location")) OutCam.Location = ReadVec3(CameraJSON["Location"]);
	if (CameraJSON.hasKey("Rotation")) OutCam.Rotation = ReadVec3(CameraJSON["Rotation"]);
	if (CameraJSON.hasKey("FOV"))      OutCam.FOV      = ReadScalarOrArray(CameraJSON["FOV"]) / Rad2Deg;
	if (CameraJSON.hasKey("NearClip")) OutCam.NearClip = ReadScalarOrArray(CameraJSON["NearClip"]);
	if (CameraJSON.hasKey("FarClip"))  OutCam.FarClip  = ReadScalarOrArray(CameraJSON["FarClip"]);
	OutCam.bValid = true;
}

// ============================================================
// Load
// ============================================================

void FLevelSaveManager::LoadLevelFromJSON(const string& filepath, FWorldContext& OutWorldContext, FPerspectiveCameraData& OutCam)
{
	using json::JSON;
	std::ifstream File(std::filesystem::path(FPaths::ToWide(filepath)));
	if (!File.is_open()) {
		std::cerr << "Failed to open file at target destination" << std::endl;
		return;
	}

	string FileContent((std::istreambuf_iterator<char>(File)),
		std::istreambuf_iterator<char>());

	JSON root = JSON::Load(FileContent);

	// ClassName은 힌트: 명시된 클래스로 시도 후 실패 시 UWorld로 폴백
	string ClassHint = root.hasKey(SceneKeys::ClassName) ? root[SceneKeys::ClassName].ToString() : "UWorld";
	UObject* WorldObj = FObjectFactory::Get().Create(ClassHint);
	if (!WorldObj || !WorldObj->IsA<UWorld>()) WorldObj = FObjectFactory::Get().Create("UWorld");
	if (!WorldObj || !WorldObj->IsA<UWorld>()) return;

	UWorld* World = static_cast<UWorld*>(WorldObj);

	// WorldType/ContextName/ContextHandle은 메타데이터: 항상 에디터 기본값 사용,
	// 씬 이름은 파일 경로의 stem에서 추출
	EWorldType WorldType = EWorldType::Editor;
	FString ContextName = FPaths::ToUtf8(
		std::filesystem::path(FPaths::ToWide(filepath)).stem().wstring()
	);
	FString ContextHandle = ContextName;

	// Deserialize Primitives (top-level) and Camera first
	std::unordered_map<string, AActor*> CreatedFromPrimitives;
	if (root.hasKey("Primitives")) {
		auto Prims = root["Primitives"];
		DeserializePrimitives(Prims, World, CreatedFromPrimitives);
	}

	// "PerspectiveCamera" 우선, 구버전 "Camera" 키도 지원
	const char* CamKey = root.hasKey("PerspectiveCamera") ? "PerspectiveCamera"
	                   : root.hasKey("Camera")            ? "Camera"
	                   : nullptr;
	if (CamKey) {
		auto Cam = root[CamKey];
		DeserializeCamera(Cam, OutCam);
	}

	// Deserialize Actors
	if (!root.hasKey(SceneKeys::Actors)) {
		OutWorldContext.WorldType = WorldType;
		OutWorldContext.World = World;
		OutWorldContext.ContextName = ContextName;
		OutWorldContext.ContextHandle = FName(ContextHandle);
		return;
	}
	for (auto& ActorJSON : root[SceneKeys::Actors].ArrayRange()) {
		string ActorClass = ActorJSON[SceneKeys::ClassName].ToString();
		// If this actor references a PrimitiveKey and that primitive already created an actor,
		// prefer the primitive-created actor and update it instead of creating a duplicate.
		AActor* Actor = nullptr;
		if (ActorJSON.hasKey("PrimitiveKey")) {
			string pk = ActorJSON["PrimitiveKey"].ToString();
			auto it = CreatedFromPrimitives.find(pk);
			if (it != CreatedFromPrimitives.end()) {
				Actor = it->second;
			}
		}

		if (!Actor) {
			UObject* ActorObj = FObjectFactory::Get().Create(ActorClass);
			if (!ActorObj || !ActorObj->IsA<AActor>()) continue;
			Actor = static_cast<AActor*>(ActorObj);
			Actor->SetWorld(World);
			World->GetActiveLevel()->AddActor(Actor);
		}

		if (ActorJSON.hasKey(SceneKeys::Visible)) {
			Actor->SetVisible(ActorJSON[SceneKeys::Visible].ToBool());
		}

		// RootComponent 트리 복원
		if (ActorJSON.hasKey(SceneKeys::RootComponent)) {
			auto RootJSON = ActorJSON[SceneKeys::RootComponent];
			if (Actor->GetRootComponent()) {
				// Merge properties into existing root component created by primitives
				DeserializeSceneComponentIntoExisting(Actor->GetRootComponent(), RootJSON, Actor);
			} else {
				USceneComponent* Root = DeserializeSceneComponentTree(RootJSON, Actor);
				if (Root) Actor->SetRootComponent(Root);
			}
		}

		// Non-scene components 복원
		if (ActorJSON.hasKey(SceneKeys::NonSceneComponents)) {
			for (auto& CompJSON : ActorJSON[SceneKeys::NonSceneComponents].ArrayRange()) {
				string CompClass = CompJSON[SceneKeys::ClassName].ToString();
				UObject* CompObj = FObjectFactory::Get().Create(CompClass);
				if (!CompObj || !CompObj->IsA<UActorComponent>()) continue;

				UActorComponent* Comp = static_cast<UActorComponent*>(CompObj);
				Actor->RegisterComponent(Comp);

				if (CompJSON.hasKey(SceneKeys::Properties)) {
					auto PropsJSON = CompJSON[SceneKeys::Properties];
					DeserializeProperties(Comp, PropsJSON);
				}
			}
		}
	}

	// 씬 로드 완료 후 에셋 목록 1회 갱신
	FObjManager::ScanMeshAssets();
	FObjManager::ScanMaterialAssets();

	OutWorldContext.WorldType = WorldType;
	OutWorldContext.World = World;
	OutWorldContext.ContextName = ContextName;
	OutWorldContext.ContextHandle = FName(ContextHandle);
}

USceneComponent* FLevelSaveManager::DeserializeSceneComponentTree(json::JSON& Node, AActor* Owner)
{
	string ClassName = Node[SceneKeys::ClassName].ToString();
	UObject* Obj = FObjectFactory::Get().Create(ClassName);
	if (!Obj || !Obj->IsA<USceneComponent>()) return nullptr;

	USceneComponent* Comp = static_cast<USceneComponent*>(Obj);
	Owner->RegisterComponent(Comp);

	// Restore properties
	if (Node.hasKey(SceneKeys::Properties)) {
		auto PropsJSON = Node[SceneKeys::Properties];
		DeserializeProperties(Comp, PropsJSON);
	}
	Comp->MarkTransformDirty();

	// Restore children recursively
	if (Node.hasKey(SceneKeys::Children)) {
		for (auto& ChildJSON : Node[SceneKeys::Children].ArrayRange()) {
			USceneComponent* Child = DeserializeSceneComponentTree(ChildJSON, Owner);
			if (Child) {
				Child->AttachToComponent(Comp);
			}
		}
	}

	return Comp;
}

void FLevelSaveManager::DeserializeSceneComponentIntoExisting(USceneComponent* Existing, json::JSON& Node, AActor* Owner)
{
	using namespace json;
	if (!Existing) return;

	if (Node.hasKey(SceneKeys::Properties)) {
		auto PropsJSON = Node[SceneKeys::Properties];
		DeserializeProperties(Existing, PropsJSON);
	}

	// Children: merge into existing children by order; create new children if missing
	if (Node.hasKey(SceneKeys::Children)) {
		auto& ChildrenJSON = Node[SceneKeys::Children];
		auto ExistingChildren = Existing->GetChildren();

		size_t idx = 0;
		for (auto& ChildJSON : ChildrenJSON.ArrayRange()) {
			if (idx < ExistingChildren.size()) {
				DeserializeSceneComponentIntoExisting(ExistingChildren[idx], const_cast<json::JSON&>(ChildJSON), Owner);
			} else {
				USceneComponent* NewChild = DeserializeSceneComponentTree(const_cast<json::JSON&>(ChildJSON), Owner);
				if (NewChild) NewChild->AttachToComponent(Existing);
			}
			idx++;
		}
	}
}

void FLevelSaveManager::DeserializeProperties(UActorComponent* Comp, json::JSON& PropsJSON)
{
	auto ApplyProp = [&](FPropertyDescriptor& Prop) {
		if (!PropsJSON.hasKey(Prop.Name.c_str())) return;
		auto Value = PropsJSON[Prop.Name.c_str()];
		DeserializePropertyValue(Prop, Value);
		Comp->PostEditProperty(Prop.Name.c_str());
	};

	TArray<FPropertyDescriptor> Before;
	Comp->GetEditableProperties(Before);
	for (auto& Prop : Before) ApplyProp(Prop);

	// PostEditProperty가 새 디스크립터를 추가할 수 있음 (예: SetStaticMesh → MaterialSlots)
	TArray<FPropertyDescriptor> After;
	Comp->GetEditableProperties(After);
	for (size_t i = Before.size(); i < After.size(); ++i) ApplyProp(After[i]);
}

void FLevelSaveManager::DeserializePropertyValue(FPropertyDescriptor& Prop, json::JSON& Value)
{
	switch (Prop.Type) {
	case EPropertyType::Bool:
		*static_cast<bool*>(Prop.ValuePtr) = Value.ToBool();
		break;

	case EPropertyType::ByteBool:
		*static_cast<uint8_t*>(Prop.ValuePtr) = Value.ToBool() ? 1 : 0;
		break;

	case EPropertyType::Int:
		*static_cast<int32*>(Prop.ValuePtr) = Value.ToInt();
		break;

	case EPropertyType::Float:
		*static_cast<float*>(Prop.ValuePtr) = static_cast<float>(Value.ToFloat());
		break;

	case EPropertyType::Vec3:
	case EPropertyType::Rotator: {
		float* v = static_cast<float*>(Prop.ValuePtr);
		int i = 0;
		for (auto& elem : Value.ArrayRange()) {
			if (i < 3) v[i] = static_cast<float>(elem.ToFloat());
			i++;
		}
		break;
	}
	case EPropertyType::Vec4: {
		float* v = static_cast<float*>(Prop.ValuePtr);
		int i = 0;
		for (auto& elem : Value.ArrayRange()) {
			if (i < 4) v[i] = static_cast<float>(elem.ToFloat());
			i++;
		}
		break;
	}
	case EPropertyType::String:
	case EPropertyType::StaticMeshRef:
		*static_cast<FString*>(Prop.ValuePtr) = Value.ToString();
		break;

	case EPropertyType::MaterialRef: {
		FString* SlotPath = static_cast<FString*>(Prop.ValuePtr);
		if (Value.hasKey("Path")) *SlotPath = Value["Path"].ToString();
		break;
	}

	case EPropertyType::Name:
		*static_cast<FName*>(Prop.ValuePtr) = FName(Value.ToString());
		break;

	default:
		break;
	}
}

// ============================================================
// Utility
// ============================================================

string FLevelSaveManager::GetCurrentTimeStamp()
{
	std::time_t t = std::time(nullptr);
	std::tm tm{};
	localtime_s(&tm, &t);

	char buf[20];
	std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
	return buf;
}

TArray<FString> FLevelSaveManager::GetLevelFileList()
{
	TArray<FString> Result;
	std::wstring SceneDir = GetSceneDirectory();
	if (!std::filesystem::exists(SceneDir))
	{
		return Result;
	}

	for (auto& Entry : std::filesystem::directory_iterator(SceneDir))
	{
		if (!Entry.is_regular_file()) continue;
		std::wstring Ext = Entry.path().extension().wstring();
		if (Ext == L".Scene" || Ext == L".scene")
		{
			// stem + 원본 확장자를 함께 저장 (대/소문자 보존)
			Result.push_back(FPaths::ToUtf8(Entry.path().filename().wstring()));
		}
	}
	return Result;
}
