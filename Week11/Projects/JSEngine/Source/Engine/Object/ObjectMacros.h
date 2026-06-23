#pragma once

#include "Object/ClassIntellisense.h"

class UClass;
class UScriptStruct;

#define UCLASS(...)
#define USTRUCT(...)
#define UENUM(...)
#define UPROPERTY(...)
#define UFUNCTION(...)

#define GENERATED_BODY(ClassName, ParentClass)		\
public:												\
	using ThisClass = ClassName;					\
	using Super = ParentClass;						\
	static UClass* StaticClass();					\
	virtual UClass* GetClass() const override		\
	{												\
		return ClassName::StaticClass();			\
	}												\
													\
	static void RegisterProperties(UClass* Class);	\
	static void RegisterFunctions(UClass* Class);	\

#define GENERATED_STRUCT_BODY(StructName)					\
public:														\
	static UScriptStruct* StaticStruct();					\
	static void RegisterProperties(UScriptStruct* Struct);	\
