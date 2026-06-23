#include "Object/Function.h"

#include "Object/Property.h"

FFunction::~FFunction()
{
	if (ReturnParameter)
	{
		delete ReturnParameter->Property;
		delete ReturnParameter;
	}

	for (FFunctionParameter* Parameter : Parameters)
	{
		if (Parameter)
		{
			delete Parameter->Property;
			delete Parameter;
		}
	}

	Parameters.clear();
}

int32 FFunction::GetParameterCount() const
{
	return static_cast<int32>(Parameters.size());
}

FFunctionParameter* FFunction::GetParameter(int32 Index) const
{
	if (Index < 0 || Index >= static_cast<int32>(Parameters.size())) return nullptr;

	return Parameters[Index];
}

FFunctionParameter* FFunction::FindParameter(const FString& Name) const
{
	for (FFunctionParameter* Parameter : Parameters)
	{
		if (!Parameter || !Parameter->Property) continue;

		if (Parameter->Property->Name == Name)
		{
			return Parameter;
		}
	}

	return nullptr;
}

bool FFunction::HasReturnValue() const
{
	return ReturnParameter && ReturnParameter->Property;
}

FFunctionParameter* FFunction::GetReturnParameter() const
{
	return ReturnParameter;
}

FProperty* FFunction::GetReturnProperty() const
{
	return ReturnParameter ? ReturnParameter->Property : nullptr;
}

bool FFunction::ValidateCallContext(const FFunctionCallContext& Context, int32 ExpectedArgumentCount) const
{
	if (!Context.Object) return false;
	if (Context.Arguments.size() != ExpectedArgumentCount) return false;
	if (Parameters.size() != ExpectedArgumentCount) return false;

	for (int32 Index = 0; Index < ExpectedArgumentCount; ++Index)
	{
		const FFunctionArgument& Argument = Context.Arguments[Index];
		FFunctionParameter* ExpectedParameter = Parameters[Index];

		if (!ExpectedParameter || !ExpectedParameter->Property) return false;
		if (!Argument.Address) return false;
		if (!Argument.Parameter) return false;
		if (Argument.Parameter != ExpectedParameter) return false;
		if (!ExpectedParameter->HasAnyParameterFlags(static_cast<uint32>(EFunctionParameterFlags::Input))) return false;
	}

	return true;
}

bool FFunction::ValidateReturnValue(const FFunctionCallContext& Context, bool bHasReturnValue) const
{
	if (!bHasReturnValue)
	{
		return true;
	}

	if (!Context.ReturnValue.Address) return false;
	if (!Context.ReturnValue.Property) return false;
	if (!ReturnParameter || !ReturnParameter->Property) return false;
	if (Context.ReturnValue.Property != ReturnParameter->Property) return false;

	return true;
}
