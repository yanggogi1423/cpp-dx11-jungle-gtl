#include "Object.h"
#include "EngineStatics.h"
#include "Object/FName.h"
#include "Object/ObjectFactory.h"
#include "Math/Vector.h"

#include <cstring>

TArray<UObject*> GUObjectArray;

UObject::UObject()
{
	UUID = EngineStatics::GenUUID();
	InternalIndex = static_cast<uint32>(GUObjectArray.size());
	GUObjectArray.push_back(this);
}

UObject::~UObject()
{
	uint32 LastIndex = static_cast<uint32>(GUObjectArray.size() - 1);

	if (InternalIndex != LastIndex)
	{
		UObject* LastObject = GUObjectArray[LastIndex];
		GUObjectArray[InternalIndex] = LastObject;
		LastObject->InternalIndex = InternalIndex;
	}

	GUObjectArray.pop_back();
}

UClass* UObject::StaticClass()
{
	static UClass Class(
		"UObject",
		nullptr,
		sizeof(UObject),
		[]() -> UObject* { return new UObject(); }
	);

	return &Class;
}

// FObjectFactory 로 같은 타입의 인스턴스를 생성한 뒤 프로퍼티 복사 → PostDuplicate 훅을 실행합니다.
// 팩토리에 등록되지 않은 추상 클래스(PrimitiveComponent 등)는 Create() 가 nullptr 를 반환하므로
// 그대로 nullptr 를 반환합니다.
UObject* UObject::Duplicate()
{
    UObject* Dup = FObjectFactory::Get().Create(GetClass()->ClassName);
    if (!Dup) return nullptr;
    Dup->CopyPropertiesFrom(this);
    Dup->PostDuplicate(this);
    return Dup;
}

// GetEditableProperties 에 노출된 프로퍼티를 이름 기반으로 매칭하여 복사합니다.
// ・ Bool / Int / Float / Vec3 / Vec4  → memcpy
// ・ String                            → FString 대입
// ・ Name                              → FName 대입 후 PostEditChangeProperty 호출
// ・ SceneComponentRef                 → 포인터 복원은 호출 측(Duplicate) 에서 담당
void UObject::CopyPropertiesFrom(UObject* Src)
{
    if (!Src) return;

	UClass* SrcClass = Src->GetClass();
	UClass* DstClass = GetClass();
	if (!SrcClass || !DstClass) return;

    TArray<FProperty*> SrcProps;
	SrcClass->GetAllProperties(SrcProps);

    for (FProperty* SrcProp : SrcProps)
    {
		if (!SrcProp) continue;

		FProperty* DstProp = DstClass->FindProperty(SrcProp->Name);
		if (!DstProp) continue;
		if (DstProp->Size != SrcProp->Size) continue;

		DstProp->CopyValue(this, Src);
		PostEditChangeProperty({ SrcProp->Name.c_str(), EPropertyChangeType::ValueSet});
	}
    /** 위 함수는 성능상 프로퍼티 개수 N에 대해 O(N²)이므로 개선의 여지가 있습니다. 
	 *  추후 N이 많아질 경우 FPropertyDescriptor에 해시 및 인덱스를 추가하여 O(N·logN)으로 개선할 수 있지만,
	 *  캐시 비용이 증가할 수 있으므로 보수적으로 접근하는 편이 좋을 것 같습니다. **/
}

void UObject::Serialize(FArchive& Ar)
{
	Ar << "Type" << GetClass()->ClassName;
    Ar << "ObjectName" << ObjectName;
}
