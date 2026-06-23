#pragma once

#include <functional>
#include <utility>
#include "Object/Object.h"
#include "Core/Singleton.h"

#define REGISTER_FACTORY(ClassName)																\
namespace {																						\
	 struct ClassName##_RegisterFactory {														\
		ClassName##_RegisterFactory() {															\
				FObjectFactory::Get().Register(													\
					#ClassName,																	\
					ClassName::StaticClass()													\
				);																				\
		}																						\
	};																							\
	ClassName##_RegisterFactory G##ClassName##_RegisterFactory;									\
}

struct FObjectFactoryEntry
{
	const UClass* Class = nullptr;
};

// Different from UFactory class
class FObjectFactory : public TSingleton<FObjectFactory>
{
	friend class TSingleton<FObjectFactory>;

public:
	void Register(const char* TypeName, const UClass* Class)
	{
		Registry[TypeName] = FObjectFactoryEntry{ Class };
	}

	UObject* Create(const std::string& TypeName)
	{
		auto Spawner = Registry.find(TypeName);	// Do NOT use array accessor [] here. it will insert a new key if not found.
		if (Spawner == Registry.end() || !Spawner->second.Class || !Spawner->second.Class->Constructor)
		{
			return nullptr;
		}

		return Spawner->second.Class->Constructor();
	}

	void GetRegisteredTypeInfos(TArray<const UClass*>& OutClasses) const {
		for (const auto& [TypeName, Entry] : Registry)
		{
			if (Entry.Class)
			{
				OutClasses.push_back(Entry.Class);
			}
		}
	}

private:
	TMap<std::string, FObjectFactoryEntry> Registry;
};
