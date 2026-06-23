#pragma once

#include "Profiling/MemoryStats.h"
#include "Object/FName.h"
#include "Core/Singleton.h"
#include "UI/EditorConsoleWidget.h"

#define DECLARE_CLASS(ClassName, ParentClass)                          \
    static const FTypeInfo s_TypeInfo;                                 \
    const FTypeInfo* GetTypeInfo() const override {                    \
        return &s_TypeInfo;                                            \
    }                                                                  \
	ClassName* Duplicate() override {                                  \
        ClassName* Duplicated = SafeDuplicate(this);                   \
        if (Duplicated) {                                              \
            Duplicated->DuplicateSubObjects();                         \
        }                                                              \
        return Duplicated;                                             \
    }
    
template <typename T>
T* SafeDuplicate(const T* Instance) {
    if constexpr (std::is_copy_constructible_v<T>) {
        return new T(*Instance);
    } else {
        // 복사 생성자가 없는 경우 처리 (nullptr 반환 혹은 Assert)
    	UE_LOG("T does not have copy constructor");
        return nullptr; 
    }
}

#define DEFINE_CLASS(ClassName, ParentClass)                           \
    const FTypeInfo ClassName::s_TypeInfo = {                          \
        #ClassName,                                                    \
        &ParentClass::s_TypeInfo,                                      \
        sizeof(ClassName)                                              \
    };

enum EClassFlags : uint32_t
{
	CF_None = 0,
	CF_Actor = 1 << 0,
	CF_Component = 1 << 1,
	CF_Camera = 1 << 2,
};

struct FTypeInfo
{
	const char* name;
	const FTypeInfo* Parent;
	size_t size;
	uint32_t ClassFlags = CF_None;

	bool IsA(const FTypeInfo* Other) const {
		for (const FTypeInfo* T = this; T; T = T->Parent) {
			if (T == Other) {
				return true;
			}
		}
		return false;
	}
};

class UObject
{
public:
	UObject();
	UObject(const UObject& Other);
	virtual ~UObject();

	uint32 GetUUID() const { return UUID; }
	uint32 GetInternalIndex() const { return InternalIndex; }
	void SetUUID(uint32 InUUID) { UUID = InUUID; }
	void SetInternalIndex(uint32 InIndex) { InternalIndex = InIndex; }

	static void* operator new(size_t Size)
	{
		void* Ptr = std::malloc(Size);
		if (Ptr)
		{
			MemoryStats::OnAllocated(static_cast<uint32>(Size));
		}
		return Ptr;
	}

	static void operator delete(void* Ptr, size_t Size)
	{
		if (Ptr)
		{
			MemoryStats::OnDeallocated(static_cast<uint32>(Size));
			std::free(Ptr);
		}
	}
	
	// 깊은 복사가 필요한 서브오브젝트를 복제 (하위 클래스에서 Override)
	virtual void DuplicateSubObjects() { }
	
	// 현재 오브젝트 복제
	virtual UObject* Duplicate()
	{
		UObject* NewObject = new UObject(*this);		// 얕은 복사로 새 객체 생성
		NewObject->DuplicateSubObjects();				// 서브 오브젝트는 깊은 복사로 별도 처리
		return NewObject;
	}

	// FName
	FString GetFName() const { return ObjectName; }
	void SetFName(const FString& InName) { ObjectName = InName; }

	// RTTI stuffs
	virtual const FTypeInfo* GetTypeInfo() const { return &s_TypeInfo; }

	template<typename T>
	bool IsA() const { return GetTypeInfo()->IsA(&T::s_TypeInfo); }

	static const FTypeInfo s_TypeInfo;

protected:
	FString ObjectName;

private:
	uint32 UUID;
	uint32 InternalIndex;
};

extern TArray<UObject*> GUObjectArray;

class UObjectManager : public TSingleton<UObjectManager>
{
	friend class TSingleton<UObjectManager>;

public:
	template<typename T>
	T* CreateObject()
	{
		static_assert(std::is_base_of<UObject, T>::value, "T must derive from UObject");
		T* Obj = new T();

		const char* ClassName = T::s_TypeInfo.name;
		uint32& Counter = NameCounters[ClassName];
		FString Name = FString(ClassName) + "_" + std::to_string(Counter++);
		Obj->SetFName(Name);

		return Obj;
	}

	void DestroyObject(UObject* Obj)
	{
		if (!Obj)
		{
			return;
		}
		delete Obj;
	}

	void ClearNameCounters()
	{
		NameCounters.clear();
	}

private:
	// UObject 인스턴스에 AActor_1, AActor_2 처럼 이름 붙여주는 용도
	TMap<FString, uint32> NameCounters;

public:
	UObject* FindByUUID(uint32 InUUID)
	{
		for (auto* Obj : GUObjectArray)
			if (Obj && Obj->GetUUID() == InUUID)
				return Obj;
		return nullptr;
	}

	UObject* FindByIndex(uint32 Index)
	{
		if (Index >= GUObjectArray.size()) return nullptr;
		return GUObjectArray[Index];
	}
};

template<typename T>
T* Cast(UObject* Obj)
{
	return (Obj && Obj->IsA<T>()) ? static_cast<T*>(Obj) : nullptr;
}

template<typename T>
const T* Cast(const UObject* Obj)
{
	return (Obj && Obj->IsA<T>()) ? static_cast<const T*>(Obj) : nullptr;
}