#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"

#include "Object/Object.h"

#include <type_traits>
#include <utility>

class FProperty;

enum class EFunctionFlags : uint32
{
	None = 0,
	CallInEditor = 1 << 0,
	BlueprintCallable = 1 << 1,
	BlueprintPure = 1 << 2,
	BlueprintEvent = 1 << 3,
};

enum class EFunctionParameterFlags : uint32
{
	None = 0,
	Input = 1 << 0,
	Output = 1 << 1,
	Return = 1 << 2,
};

struct FFunctionParameter
{
	FProperty* Property = nullptr;
	uint32 Flags = 0;

	bool HasAnyParameterFlags(uint32 InFlags) const { return (Flags & InFlags) != 0; }
};

struct FFunctionArgument
{
	FFunctionParameter* Parameter = nullptr;
	void* Address = nullptr;
};

struct FFunctionReturnValue
{
	FProperty* Property = nullptr;
	void* Address = nullptr;
};

struct FFunctionCallContext
{
	UObject* Object = nullptr;
	FFunctionReturnValue ReturnValue;
	TArray<FFunctionArgument> Arguments;
};

class FFunction
{
public:
	virtual ~FFunction();

	virtual bool Invoke(FFunctionCallContext& Context) const = 0;

	bool Invoke(UObject* Object) const
	{
		FFunctionCallContext Context;
		Context.Object = Object;
		return Invoke(Context);
	}

	bool HasAnyFunctionFlags(uint32 InFlags) const { return (Flags & InFlags) != 0; }

	int32 GetParameterCount() const;
	FFunctionParameter* GetParameter(int32 Index) const;
	FFunctionParameter* FindParameter(const FString& Name) const;

	bool HasReturnValue() const;
	FFunctionParameter* GetReturnParameter() const;
	FProperty* GetReturnProperty() const;

protected:
	bool ValidateCallContext(const FFunctionCallContext& Context, int32 ExpectedArgumentCount) const;
	bool ValidateReturnValue(const FFunctionCallContext& Context, bool bHasReturnValue) const;

public:
	FString Name;
	FString DisplayName;
	FString Category;
	uint32 Flags = 0;

	FFunctionParameter* ReturnParameter = nullptr;
	TArray<FFunctionParameter*> Parameters;
};

template <typename T>
using TFunctionArgStorageType = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename ClassType, typename ReturnType, typename... ArgTypes>
class TFunction : public FFunction
{
public:
	using MethodPtr = ReturnType (ClassType::*)(ArgTypes...);

	MethodPtr Method = nullptr;

	bool Invoke(FFunctionCallContext& Context) const override
	{
		ClassType* TypedObject = Cast<ClassType>(Context.Object);
		if (!TypedObject || !Method) return false;
		if (!ValidateCallContext(Context, static_cast<int32>(sizeof...(ArgTypes)))) return false;
		if (!ValidateReturnValue(Context, !std::is_void_v<ReturnType>)) return false;

		InvokeImpl(TypedObject, Context, std::index_sequence_for<ArgTypes...>{});
		return true;
	}

private:
	template <size_t... Indices>
	void InvokeImpl(ClassType* Object, FFunctionCallContext& Context, std::index_sequence<Indices...>) const
	{
		if constexpr (std::is_void_v<ReturnType>)
		{
			(Object->*Method)(
				(*reinterpret_cast<TFunctionArgStorageType<ArgTypes>*>(Context.Arguments[Indices].Address))...);
		}
		else
		{
			ReturnType Result = (Object->*Method)(
				(*reinterpret_cast<TFunctionArgStorageType<ArgTypes>*>(Context.Arguments[Indices].Address))...);

			if (Context.ReturnValue.Address)
			{
				*reinterpret_cast<ReturnType*>(Context.ReturnValue.Address) = Result;
			}
		}
	}
};

template <typename ClassType, typename ReturnType, typename... ArgTypes>
class TConstFunction : public FFunction
{
public:
	using MethodPtr = ReturnType (ClassType::*)(ArgTypes...) const;

	MethodPtr Method = nullptr;

	bool Invoke(FFunctionCallContext& Context) const override
	{
		const ClassType* TypedObject = Cast<const ClassType>(Context.Object);
		if (!TypedObject || !Method) return false;
		if (!ValidateCallContext(Context, static_cast<int32>(sizeof...(ArgTypes)))) return false;
		if (!ValidateReturnValue(Context, !std::is_void_v<ReturnType>)) return false;

		InvokeImpl(TypedObject, Context, std::index_sequence_for<ArgTypes...>{});
		return true;
	}
	
private:
	template <size_t... Indices>
	void InvokeImpl(const ClassType* Object, FFunctionCallContext& Context, std::index_sequence<Indices...>) const
	{
		if constexpr (std::is_void_v<ReturnType>)
		{
			(Object->*Method)(
				(*reinterpret_cast<TFunctionArgStorageType<ArgTypes>*>(Context.Arguments[Indices].Address))...);
		}
		else
		{
			ReturnType Result = (Object->*Method)(
				(*reinterpret_cast<TFunctionArgStorageType<ArgTypes>*>(Context.Arguments[Indices].Address))...);

			if (Context.ReturnValue.Address)
			{
				*reinterpret_cast<ReturnType*>(Context.ReturnValue.Address) = Result;
			}
		}
	}
};
