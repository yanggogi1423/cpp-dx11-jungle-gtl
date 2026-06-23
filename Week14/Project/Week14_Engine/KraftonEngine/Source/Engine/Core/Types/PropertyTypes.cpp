#include "Core/Types/PropertyTypes.h"

#include "Math/Rotator.h"
#include "Math/Vector.h"
#include "Object/Reflection/UStruct.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace
{
	FString ToLowerAscii(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});
		return Value;
	}

	bool IsTruthyMetadataValue(const FString& Value)
	{
		const FString Lower = ToLowerAscii(Value);
		return Lower.empty() || (Lower != "false" && Lower != "0" && Lower != "no");
	}

	bool HasAnimatableMetadata(const FProperty& Property)
	{
		for (const char* Key : { "animatable", "sequencer", "sequence" })
		{
			auto It = Property.Metadata.find(Key);
			if (It != Property.Metadata.end() && IsTruthyMetadataValue(It->second))
			{
				return true;
			}
		}
		return false;
	}

	bool IsValueChannel(const FString& ChannelName)
	{
		return ChannelName.empty() || ToLowerAscii(ChannelName) == "value";
	}

	bool MatchesChannel(const FString& ChannelName, std::initializer_list<const char*> Names)
	{
		const FString Lower = ToLowerAscii(ChannelName);
		for (const char* Name : Names)
		{
			if (Lower == ToLowerAscii(Name ? FString(Name) : FString()))
			{
				return true;
			}
		}
		return false;
	}

	bool IsSequencerSupportedType(EPropertyType Type)
	{
		switch (Type)
		{
		case EPropertyType::Bool:
		case EPropertyType::ByteBool:
		case EPropertyType::Int:
		case EPropertyType::Float:
		case EPropertyType::Vec3:
		case EPropertyType::Vec4:
		case EPropertyType::Rotator:
		case EPropertyType::Color4:
			return true;
		default:
			return false;
		}
	}
}

const char* FPropertyValue::GetName() const
{
	return Property && Property->Name ? Property->Name : "";
}

const char* FPropertyValue::GetDisplayName() const
{
	return Property && Property->DisplayName ? Property->DisplayName : GetName();
}

const char* FPropertyValue::GetCategory() const
{
	return Property && Property->Category ? Property->Category : "";
}

EPropertyType FPropertyValue::GetType() const
{
	return Property ? Property->GetType() : EPropertyType::Bool;
}

float FPropertyValue::GetMin() const
{
	return Property ? Property->GetMin() : 0.0f;
}

float FPropertyValue::GetMax() const
{
	return Property ? Property->GetMax() : 0.0f;
}

float FPropertyValue::GetSpeed() const
{
	return Property ? Property->GetSpeed() : 0.1f;
}

UStruct* FPropertyValue::GetStructType() const
{
	return Property ? Property->GetStructType() : nullptr;
}

const FEnum* FPropertyValue::GetEnumType() const
{
	return Property ? Property->GetEnumType() : nullptr;
}

const TMap<FString, FString>& FPropertyValue::GetMetadata() const
{
	static const TMap<FString, FString> EmptyMetadata;
	return Property ? Property->Metadata : EmptyMetadata;
}

void* FPropertyValue::GetValuePtr() const
{
	return Property ? Property->GetValuePtrFor(ContainerPtr) : nullptr;
}

void FPropertyValue::GetStructChildren(TArray<FPropertyValue>& OutProps) const
{
	OutProps.clear();
	UStruct* StructType = GetStructType();
	void* ValuePtr = GetValuePtr();
	if (!StructType || !ValuePtr)
	{
		return;
	}

	TArray<const FProperty*> ChildProperties;
	StructType->GetPropertyRefs(ChildProperties);
	for (const FProperty* ChildProperty : ChildProperties)
	{
		if (!ChildProperty || !ChildProperty->GetValuePtrFor(ValuePtr))
		{
			continue;
		}

		OutProps.push_back(ChildProperty->ToValue(ValuePtr, Object));
	}
}

void FProperty::Serialize(UObject* Object, FArchive& Ar) const
{
	FPropertySerializeContext Context;
	Context.Owner = Object;
	Context.bIsVersionedTaggedLoad = Ar.IsVersionedTaggedLoad();
	SerializeValue(GetValuePtrFor(Object), Ar, Context);
}

void FProperty::SerializeValue(void* ValuePtr, FArchive& Ar, const FPropertySerializeContext& Context) const
{
	(void)Context;
	SerializeValue(ValuePtr, Ar);
}

void FProperty::Serialize(void* Container, FArchive& Ar) const
{
	SerializeValue(GetValuePtrFor(Container), Ar);
}

bool FProperty::IsSequencerScalar() const
{
	if (!Name || (Flags & PF_ReadOnly) != 0 || (Flags & PF_Transient) != 0)
	{
		return false;
	}

	if ((Flags & PF_Animatable) == 0 && !HasAnimatableMetadata(*this))
	{
		return false;
	}

	return IsSequencerSupportedType(GetType());
}

bool FProperty::ReadScalarChannelValue(const UObject* Container, const FString& ChannelName, float& OutValue) const
{
	if (!IsSequencerScalar() || !Container)
	{
		return false;
	}

	void* ValuePtr = GetValuePtrFor(const_cast<UObject*>(Container));
	if (!ValuePtr)
	{
		return false;
	}

	switch (GetType())
	{
	case EPropertyType::Bool:
		if (!IsValueChannel(ChannelName)) return false;
		OutValue = *static_cast<bool*>(ValuePtr) ? 1.0f : 0.0f;
		return true;
	case EPropertyType::ByteBool:
		if (!IsValueChannel(ChannelName)) return false;
		OutValue = *static_cast<uint8*>(ValuePtr) != 0 ? 1.0f : 0.0f;
		return true;
	case EPropertyType::Int:
		if (!IsValueChannel(ChannelName)) return false;
		OutValue = static_cast<float>(*static_cast<int32*>(ValuePtr));
		return true;
	case EPropertyType::Float:
		if (!IsValueChannel(ChannelName)) return false;
		OutValue = *static_cast<float*>(ValuePtr);
		return true;
	case EPropertyType::Vec3:
	{
		const FVector& Value = *static_cast<FVector*>(ValuePtr);
		if (MatchesChannel(ChannelName, { "x", "r" })) { OutValue = Value.X; return true; }
		if (MatchesChannel(ChannelName, { "y", "g" })) { OutValue = Value.Y; return true; }
		if (MatchesChannel(ChannelName, { "z", "b" })) { OutValue = Value.Z; return true; }
		return false;
	}
	case EPropertyType::Rotator:
	{
		const FRotator& Value = *static_cast<FRotator*>(ValuePtr);
		if (MatchesChannel(ChannelName, { "pitch", "x" })) { OutValue = Value.Pitch; return true; }
		if (MatchesChannel(ChannelName, { "yaw", "y" })) { OutValue = Value.Yaw; return true; }
		if (MatchesChannel(ChannelName, { "roll", "z" })) { OutValue = Value.Roll; return true; }
		return false;
	}
	case EPropertyType::Vec4:
	case EPropertyType::Color4:
	{
		const FVector4& Value = *static_cast<FVector4*>(ValuePtr);
		if (MatchesChannel(ChannelName, { "x", "r" })) { OutValue = Value.X; return true; }
		if (MatchesChannel(ChannelName, { "y", "g" })) { OutValue = Value.Y; return true; }
		if (MatchesChannel(ChannelName, { "z", "b" })) { OutValue = Value.Z; return true; }
		if (MatchesChannel(ChannelName, { "w", "a" })) { OutValue = Value.W; return true; }
		return false;
	}
	default:
		return false;
	}
}

bool FProperty::WriteScalarChannelValue(UObject* Container, const FString& ChannelName, float NewValue) const
{
	if (!IsSequencerScalar() || !Container)
	{
		return false;
	}

	void* ValuePtr = GetValuePtrFor(Container);
	if (!ValuePtr)
	{
		return false;
	}

	switch (GetType())
	{
	case EPropertyType::Bool:
		if (!IsValueChannel(ChannelName)) return false;
		*static_cast<bool*>(ValuePtr) = NewValue >= 0.5f;
		return true;
	case EPropertyType::ByteBool:
		if (!IsValueChannel(ChannelName)) return false;
		*static_cast<uint8*>(ValuePtr) = NewValue >= 0.5f ? 1 : 0;
		return true;
	case EPropertyType::Int:
		if (!IsValueChannel(ChannelName)) return false;
		*static_cast<int32*>(ValuePtr) = static_cast<int32>(std::round(NewValue));
		return true;
	case EPropertyType::Float:
		if (!IsValueChannel(ChannelName)) return false;
		*static_cast<float*>(ValuePtr) = NewValue;
		return true;
	case EPropertyType::Vec3:
	{
		FVector& Value = *static_cast<FVector*>(ValuePtr);
		if (MatchesChannel(ChannelName, { "x", "r" })) { Value.X = NewValue; return true; }
		if (MatchesChannel(ChannelName, { "y", "g" })) { Value.Y = NewValue; return true; }
		if (MatchesChannel(ChannelName, { "z", "b" })) { Value.Z = NewValue; return true; }
		return false;
	}
	case EPropertyType::Rotator:
	{
		FRotator& Value = *static_cast<FRotator*>(ValuePtr);
		if (MatchesChannel(ChannelName, { "pitch", "x" })) { Value.Pitch = NewValue; return true; }
		if (MatchesChannel(ChannelName, { "yaw", "y" })) { Value.Yaw = NewValue; return true; }
		if (MatchesChannel(ChannelName, { "roll", "z" })) { Value.Roll = NewValue; return true; }
		return false;
	}
	case EPropertyType::Vec4:
	case EPropertyType::Color4:
	{
		FVector4& Value = *static_cast<FVector4*>(ValuePtr);
		if (MatchesChannel(ChannelName, { "x", "r" })) { Value.X = NewValue; return true; }
		if (MatchesChannel(ChannelName, { "y", "g" })) { Value.Y = NewValue; return true; }
		if (MatchesChannel(ChannelName, { "z", "b" })) { Value.Z = NewValue; return true; }
		if (MatchesChannel(ChannelName, { "w", "a" })) { Value.W = NewValue; return true; }
		return false;
	}
	default:
		return false;
	}
}
