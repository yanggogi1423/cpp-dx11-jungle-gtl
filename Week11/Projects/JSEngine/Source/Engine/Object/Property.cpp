#include "Object/Property.h"

#include "Object/Class.h"
#include "Object/Object.h"

#include "Core/Containers/Array.h"

bool FProperty::IsEditable() const
{
	return HasAnyPropertyFlags(static_cast<uint32>(EPropertyFlags::EditAnywhere));
}

bool FProperty::IsVisible() const
{
	return HasAnyPropertyFlags(
		static_cast<uint32>(EPropertyFlags::EditAnywhere) |
		static_cast<uint32>(EPropertyFlags::VisibleAnywhere));
}

void* FProperty::GetValuePtr(void* ObjectAddress) const
{
	return reinterpret_cast<void*>(reinterpret_cast<uint8*>(ObjectAddress) + Offset);
}

const void* FProperty::GetValuePtr(const void* ObjectAddress) const
{
	return reinterpret_cast<const void*>(reinterpret_cast<const uint8*>(ObjectAddress) + Offset);
}

bool FBoolProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<const bool*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FBoolProperty::SetValue(void* Object, bool Value) const
{
	*reinterpret_cast<bool*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FBoolProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

float FFloatProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<const float*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FFloatProperty::SetValue(void* Object, float Value) const
{
	*reinterpret_cast<float*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FFloatProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

double FDoubleProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<const double*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FDoubleProperty::SetValue(void* Object, double Value) const
{
	*reinterpret_cast<double*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FDoubleProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

int32 FIntProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<const int32*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FIntProperty::SetValue(void* Object, int32 Value) const
{
	*reinterpret_cast<int32*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FIntProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

uint32 FUIntProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<const uint32*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FUIntProperty::SetValue(void* Object, uint32 Value) const
{
	*reinterpret_cast<uint32*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FUIntProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

FVector FVectorProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<const FVector*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FVectorProperty::SetValue(void* Object, FVector Value) const
{
	*reinterpret_cast<FVector*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FVectorProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

FVector2 FVector2Property::GetValue(const void* Object) const
{
	return *reinterpret_cast<const FVector2*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FVector2Property::SetValue(void* Object, FVector2 Value) const
{
	*reinterpret_cast<FVector2*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FVector2Property::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

FVector4 FVector4Property::GetValue(const void* Object) const
{
	return *reinterpret_cast<const FVector4*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FVector4Property::SetValue(void* Object, FVector4 Value) const
{
	*reinterpret_cast<FVector4*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FVector4Property::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

FString FStringProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<const FString*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FStringProperty::SetValue(void* Object, const FString& Value) const
{
	*reinterpret_cast<FString*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FStringProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

FName FNameProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<const FName*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FNameProperty::SetValue(void* Object, const FName& Value) const
{
	*reinterpret_cast<FName*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FNameProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

FColor FColorProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<const FColor*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FColorProperty::SetValue(void* Object, FColor Value) const
{
	*reinterpret_cast<FColor*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FColorProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

int32 FEnumProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<const int32*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FEnumProperty::SetValue(void* Object, int32 Value) const
{
	*reinterpret_cast<int32*>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

void FEnumProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

UObject* FObjectProperty::GetValue(const void* Object) const
{
	return *reinterpret_cast<UObject* const*>(
		reinterpret_cast<const uint8*>(Object) + Offset);
}

void FObjectProperty::SetValue(void* Object, UObject* Value) const
{
	*reinterpret_cast<UObject**>(reinterpret_cast<uint8*>(Object) + Offset) = Value;
}

UObject* FObjectProperty::GetValueFromAddress(const void* Address) const
{
	return *reinterpret_cast<UObject* const*>(Address);
}

void FObjectProperty::SetValueAtAddress(void* Address, UObject* Value) const
{
	*reinterpret_cast<UObject**>(Address) = Value;
}

bool FObjectProperty::AcceptsClass(const UClass* Class) const
{
	if (!Class) return false;
	if (!PropertyClass) return false;

	return Class->IsChildOf(PropertyClass);
}

bool FObjectProperty::AcceptsObject(const UObject* Object) const
{
	if (!Object) return true;
	return AcceptsClass(Object->GetClass());
}

void FObjectProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	SetValue(DstObject, GetValue(SrcObject));
}

void FStructProperty::CopyValue(void* DstObject, const void* SrcObject) const
{
	if (!Struct) return;

	std::memcpy(GetValueAddress(DstObject), GetValueAddress(SrcObject), Size);
}

FArrayProperty::~FArrayProperty()
{
	delete Inner;
	Inner = nullptr;
}

FMapProperty::~FMapProperty()
{
	delete KeyProp;
	KeyProp = nullptr;

	delete ValueProp;
	ValueProp = nullptr;
}
