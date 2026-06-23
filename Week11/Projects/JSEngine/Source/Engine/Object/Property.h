#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/String.h"
#include "Core/Containers/Map.h"

#include "Object/FName.h"

#include "Math/Color.h"
#include "Math/Vector2.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"

#include <functional>

class UClass;
class UObject;
class UScriptStruct;

enum class EPropertyFlags : uint32
{
	None = 0,
	EditAnywhere = 1 << 0,
	VisibleAnywhere = 1 << 1,
	Transient = 1 << 2,
};

class FProperty
{
public:
	virtual ~FProperty() = default;

public:
	virtual void CopyValue(void* DstObject, const void* SrcObject) const = 0;

	void* GetValueAddress(void* ObjectAddress) const { return GetValuePtr(ObjectAddress); }
	const void* GetValueAddress(const void* ObjectAddress) const { return GetValuePtr(ObjectAddress); }

	bool HasAnyPropertyFlags(uint32 InFlags) const { return (Flags & InFlags) != 0; }

	bool IsEditable() const;
	bool IsVisible() const;

protected:
	void* GetValuePtr(void* ObjectAddress) const;
	const void* GetValuePtr(const void* ObjectAddress) const;

public:
	FString Name;
	FString DisplayName;
	FString TypeName;
	
	FString Category;
	float ClampMin = 0.0f;
	float ClampMax = 0.0f;
	float Delta = 0.1f;
	bool bHasClampMin = false;
	bool bHasClampMax = false;

	uint32 Offset = 0;
	uint32 Size = 0;
	uint32 Flags = 0;
};

class FBoolProperty : public FProperty
{
public:
	bool GetValue(const void* Object) const;
	void SetValue(void* Object, bool Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FFloatProperty : public FProperty
{
public:
	float GetValue(const void* Object) const;
	void SetValue(void* Object, float Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FDoubleProperty : public FProperty
{
public:
	double GetValue(const void* Object) const;
	void SetValue(void* Object, double Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FIntProperty : public FProperty
{
public:
	int32 GetValue(const void* Object) const;
	void SetValue(void* Object, int32 Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FUIntProperty : public FProperty
{
public:
	uint32 GetValue(const void* Object) const;
	void SetValue(void* Object, uint32 Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FVectorProperty : public FProperty
{
public:
	FVector GetValue(const void* Object) const;
	void SetValue(void* Object, FVector Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FVector2Property : public FProperty
{
public:
	FVector2 GetValue(const void* Object) const;
	void SetValue(void* Object, FVector2 Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FVector4Property : public FProperty
{
public:
	FVector4 GetValue(const void* Object) const;
	void SetValue(void* Object, FVector4 Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FStringProperty : public FProperty
{
public:
	FString GetValue(const void* Object) const;
	void SetValue(void* Object, const FString& Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FNameProperty : public FProperty
{
public:
	FName GetValue(const void* Object) const;
	void SetValue(void* Object, const FName& Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FColorProperty : public FProperty
{
public:
	FColor GetValue(const void* Object) const;
	void SetValue(void* Object, FColor Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FEnumProperty : public FProperty
{
public:
	FString EnumName;

	int32 GetValue(const void* Object) const;
	void SetValue(void* Object, int32 Value) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FObjectProperty : public FProperty
{
public:
	UClass* PropertyClass = nullptr;
	FString ClassName;

	UObject* GetValue(const void* Object) const;
	void SetValue(void* Object, UObject* Value) const;

	UObject* GetValueFromAddress(const void* Address) const;
	void SetValueAtAddress(void* Address, UObject* Value) const;

	bool AcceptsClass(const UClass* Class) const;
	bool AcceptsObject(const UObject* Object) const;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FArrayProperty : public FProperty
{
public:
	~FArrayProperty() override;

	FProperty* Inner = nullptr;

	virtual int32 GetNum(const void* Object) const = 0;
	virtual void* GetElementPtr(void* Object, int32 Index) const = 0;
	virtual const void* GetElementPtr(const void* Object, int32 Index) const = 0;
	virtual void Resize(void* Object, int32 NewNum) const = 0;
	virtual void AddDefaulted(void* Object) const = 0;
	virtual void RemoveAt(void* Object, int32 Index) const = 0;
};

template<typename ElementType>
class TArrayProperty : public FArrayProperty
{
public:
	TArray<ElementType>& GetArray(void* Object) const
	{
		return *reinterpret_cast<TArray<ElementType>*>(GetValuePtr(Object));
	}

	const TArray<ElementType>& GetArray(const void* Object) const
	{
		return *reinterpret_cast<const TArray<ElementType>*>(GetValuePtr(Object));
	}

	int32 GetNum(const void* Object) const override
	{
		return static_cast<int32>(GetArray(Object).size());
	}

	void* GetElementPtr(void* Object, int32 Index) const override
	{
		TArray<ElementType>& Array = GetArray(Object);
		if (Index < 0 || Index >= static_cast<int32>(Array.size()))
		{
			return nullptr;
		}

		return &Array[Index];
	}

	const void* GetElementPtr(const void* Object, int32 Index) const override
	{
		const TArray<ElementType>& Array = GetArray(Object);
		if (Index < 0 || Index >= static_cast<int32>(Array.size()))
		{
			return nullptr;
		}

		return &Array[Index];
	}

	void Resize(void* Object, int32 NewNum) const override
	{
		GetArray(Object).resize(NewNum);
	}

	void CopyValue(void* DstObject, const void* SrcObject) const override
	{
		GetArray(DstObject) = GetArray(SrcObject);
	}

	void AddDefaulted(void* Object) const override
	{
		GetArray(Object).emplace_back(ElementType{});
	}

	void RemoveAt(void* Object, int32 Index) const override
	{
		TArray<ElementType>& Array = GetArray(Object);

		if (Index < 0 || Index >= static_cast<int32>(Array.size())) return;

		Array.erase(Array.begin() + Index);
	}
};

class FStructProperty : public FProperty
{
public:
	UScriptStruct* Struct = nullptr;
	FString StructName;

	void CopyValue(void* DstObject, const void* SrcObject) const override;
};

class FMapProperty : public FProperty
{
public:
	~FMapProperty() override;

	FProperty* KeyProp = nullptr;
	FProperty* ValueProp = nullptr;

	virtual int32 GetNum(const void* Object) const = 0;
	virtual void ForEachPair(void* Object, const std::function<void(const void* KeyPtr, void* ValuePtr)>& Callback) const = 0;
	virtual void ForEachPair(const void* Object, const std::function<void(const void* KeyPtr, const void* ValuePtr)>& Callback) const = 0;
};

template<typename KeyType, typename ValueType>
class TMapProperty : public FMapProperty
{
public:
	TMap<KeyType, ValueType>& GetMap(void* Object) const
	{
		return *reinterpret_cast<TMap<KeyType, ValueType>*>(GetValuePtr(Object));
	}

	const TMap<KeyType, ValueType>& GetMap(const void* Object) const
	{
		return *reinterpret_cast<const TMap<KeyType, ValueType>*>(GetValuePtr(Object));
	}

	int32 GetNum(const void* Object) const override
	{
		return static_cast<int32>(GetMap(Object).size());
	}

	void ForEachPair(void* Object, const std::function<void(const void* KeyPtr, void* ValuePtr)>& Callback) const override
	{
		for (auto& Pair : GetMap(Object))
		{
			Callback(&Pair.first, &Pair.second);
		}
	}

	void ForEachPair(const void* Object, const std::function<void(const void* KeyPtr, const void* ValuePtr)>& Callback) const override
	{
		for (const auto& Pair : GetMap(Object))
		{
			Callback(&Pair.first, &Pair.second);
		}
	}

	void CopyValue(void* DstObject, const void* SrcObject) const override
	{
		GetMap(DstObject) = GetMap(SrcObject);
	}
};
