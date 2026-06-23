#pragma once

#ifdef __INTELLISENSE__

enum EClassSpecifiers
{
	Abstract,
};

enum EPropertySpecifiers
{
	EditAnywhere,
	VisibleAnywhere,
	Transient,
};

enum EMetadataSpecifiers
{
	Category,
	DisplayName,
	ClampMin,
	ClampMax,
	Delta,
};

enum EFunctionSpecifiers
{
	CallInEditor,
	BlueprintCallable,
	BlueprintPure,
	BlueprintEvent,
};

#endif
