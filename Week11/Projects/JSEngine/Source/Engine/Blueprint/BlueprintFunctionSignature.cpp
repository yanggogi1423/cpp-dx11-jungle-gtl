#include "Blueprint/BlueprintFunctionSignature.h"

#include "Object/Function.h"
#include "Object/Property.h"

static FBlueprintPinSignature MakeExecPin(const FString& Name, EBlueprintPinDirection Direction)
{
	FBlueprintPinSignature Pin;
	Pin.Name = Name;
	Pin.DisplayName = Name;
	Pin.TypeName = "exec";
	Pin.Direction = Direction;
	Pin.Kind = EBlueprintPinKind::Exec;
	Pin.Property = nullptr;
	return Pin;
}

static FBlueprintPinSignature MakeDataPin(FProperty* Property, EBlueprintPinDirection Direction)
{
	FBlueprintPinSignature Pin;

	if (Property)
	{
		Pin.Name = Property->Name;
		Pin.DisplayName = Property->DisplayName.empty()
			? Property->Name
			: Property->DisplayName;
		Pin.TypeName = Property->TypeName;
		Pin.Property = Property;
	}

	Pin.Direction = Direction;
	Pin.Kind = EBlueprintPinKind::Data;
	return Pin;
}

FBlueprintNodeSignature MakeFunctionNodeSignature(const FFunction* Function)
{
	FBlueprintNodeSignature Signature;

	if (!Function)
	{
		return Signature;
	}

	Signature.Name = Function->Name;
	Signature.DisplayName = Function->DisplayName.empty()
		? Function->Name
		: Function->DisplayName;
	Signature.Category = Function->Category;

	const bool bCallable = Function->HasAnyFunctionFlags(static_cast<uint32>(EFunctionFlags::BlueprintCallable));

	if (bCallable)
	{
		Signature.Pins.push_back(MakeExecPin("Execute", EBlueprintPinDirection::Input));
		Signature.Pins.push_back(MakeExecPin("Then", EBlueprintPinDirection::Output));
	}

	for (int32 Index = 0; Index < Function->GetParameterCount(); ++Index)
	{
		FFunctionParameter* Parameter = Function->GetParameter(Index);
		if (!Parameter || !Parameter->Property) continue;

		Signature.Pins.push_back(
			MakeDataPin(Parameter->Property, EBlueprintPinDirection::Input));
	}

	if (Function->HasReturnValue())
	{
		FProperty* ReturnProperty = Function->GetReturnProperty();

		FBlueprintPinSignature ReturnPin = MakeDataPin(ReturnProperty, EBlueprintPinDirection::Output);

		ReturnPin.Name = "ReturnValue";
		ReturnPin.DisplayName = "Return Value";

		Signature.Pins.push_back(ReturnPin);
	}

	return Signature;
}
