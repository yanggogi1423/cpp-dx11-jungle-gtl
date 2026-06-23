#include "LuaScriptManager.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include "Animation/ActorSequence.h"
#include "Component/ActorComponent.h"
#include "Component/ActorSequenceComponent.h"
#include "Component/Primitive/BillboardComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logging/Log.h"
#include "Core/Property/ArrayProperty.h"
#include "Core/Property/BoolProperty.h"
#include "Core/Property/ClassProperty.h"
#include "Core/Property/EnumProperty.h"
#include "Core/Property/NameProperty.h"
#include "Core/Property/NumericProperty.h"
#include "Core/Property/ObjectProperty.h"
#include "Core/Property/SoftObjectProperty.h"
#include "Core/Property/StringProperty.h"
#include "Core/Property/StructProperty.h"
#include "Diagnostics/ActorSequenceDiagnostics.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Camera/SequenceCameraShake.h"
#include "GameFramework/Pawn/Pawn.h"
#include "Materials/MaterialManager.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"
#include "Object/Object.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/Reflection/UClass.h"
#include "Object/Reflection/UStruct.h"
#include "Runtime/Engine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <random>
#include <sstream>

namespace
{
    struct FLuaReflectedEventOverride
    {
        TWeakObjectPtr<UObject> Target;
        sol::protected_function Callback;
    };

    TMap<FString, FLuaReflectedEventOverride> GLuaReflectedEventOverrides;

    FString MakeLuaReflectedEventKey(const UObject* Object, const FFunction& Function)
    {
        if (!Object)
        {
            return {};
        }
        return std::to_string(Object->GetUUID()) + "::" + Function.GetSignature();
    }

    void CompactLuaReflectedEventOverrides()
    {
        for (auto It = GLuaReflectedEventOverrides.begin(); It != GLuaReflectedEventOverrides.end();)
        {
            if (!It->second.Target.IsValid() || !It->second.Callback.valid())
            {
                It = GLuaReflectedEventOverrides.erase(It);
                continue;
            }
            ++It;
        }
    }
}
namespace
{
    const char* LuaPropertyTypeName(EPropertyType Type)
    {
        switch (Type)
        {
        case EPropertyType::Bool:
            return "Bool";
        case EPropertyType::ByteBool:
            return "ByteBool";
        case EPropertyType::Int:
            return "Int";
        case EPropertyType::Float:
            return "Float";
        case EPropertyType::Vec3:
            return "Vec3";
        case EPropertyType::Vec4:
            return "Vec4";
        case EPropertyType::Rotator:
            return "Rotator";
        case EPropertyType::String:
            return "String";
        case EPropertyType::Name:
            return "Name";
        case EPropertyType::ObjectRef:
            return "ObjectRef";
        case EPropertyType::Color4:
            return "Color4";
        case EPropertyType::ClassRef:
            return "ClassRef";
        case EPropertyType::Enum:
            return "Enum";
        case EPropertyType::Struct:
            return "Struct";
        case EPropertyType::SoftObjectRef:
            return "SoftObjectRef";
        case EPropertyType::Array:
            return "Array";
        default:
            return "Unknown";
        }
    }

    bool LuaReadNumber(const sol::object& Object, double& OutValue)
    {
        if (!Object.valid() || Object == sol::nil || Object.get_type() != sol::type::number)
        {
            return false;
        }
        OutValue = Object.as<double>();
        return true;
    }

    bool LuaReadFloatField(const sol::table& Table, const char* Name, int Index, float& OutValue)
    {
        double      Number = 0.0;
        sol::object Named  = Table[Name];
        if (LuaReadNumber(Named, Number))
        {
            OutValue = static_cast<float>(Number);
            return true;
        }
        sol::object Indexed = Table[Index];
        if (LuaReadNumber(Indexed, Number))
        {
            OutValue = static_cast<float>(Number);
            return true;
        }
        return false;
    }

    bool LuaObjectToVector(const sol::object& Object, FVector& OutVector)
    {
        if (!Object.valid() || Object == sol::nil)
        {
            return false;
        }
        if (Object.is<FVector>())
        {
            OutVector = Object.as<FVector>();
            return true;
        }
        if (Object.get_type() != sol::type::table)
        {
            return false;
        }
        sol::table Table = Object.as<sol::table>();
        float      X     = 0.0f;
        float      Y     = 0.0f;
        float      Z     = 0.0f;
        LuaReadFloatField(Table, "X", 1, X);
        LuaReadFloatField(Table, "Y", 2, Y);
        LuaReadFloatField(Table, "Z", 3, Z);
        OutVector = FVector(X, Y, Z);
        return true;
    }

    bool LuaObjectToVector4(const sol::object& Object, FVector4& OutVector)
    {
        if (!Object.valid() || Object == sol::nil)
        {
            return false;
        }
        if (Object.is<FVector4>())
        {
            OutVector = Object.as<FVector4>();
            return true;
        }
        if (Object.get_type() != sol::type::table)
        {
            return false;
        }
        sol::table Table = Object.as<sol::table>();
        float      X     = 0.0f;
        float      Y     = 0.0f;
        float      Z     = 0.0f;
        float      W     = 0.0f;
        LuaReadFloatField(Table, "X", 1, X);
        LuaReadFloatField(Table, "Y", 2, Y);
        LuaReadFloatField(Table, "Z", 3, Z);
        if (!LuaReadFloatField(Table, "W", 4, W))
        {
            LuaReadFloatField(Table, "A", 4, W);
        }
        OutVector = FVector4(X, Y, Z, W);
        return true;
    }

    sol::table LuaVector4ToTable(sol::this_state State, const FVector4& Value)
    {
        sol::state_view Lua(State);
        sol::table      Table = Lua.create_table();
        Table["X"]            = Value.X;
        Table["Y"]            = Value.Y;
        Table["Z"]            = Value.Z;
        Table["W"]            = Value.W;
        Table["R"]            = Value.R;
        Table["G"]            = Value.G;
        Table["B"]            = Value.B;
        Table["A"]            = Value.A;
        return Table;
    }

    sol::object LuaValueToObject(sol::this_state State, const FProperty& Property, void* ValuePtr);
    bool        LuaObjectToValue(
        const FProperty&   Property,
        void*              ValuePtr,
        const sol::object& Object,
        FString*           OutError = nullptr
    );

    bool LuaObjectToEnumValue(const FEnum* EnumType, const sol::object& Object, int64& OutValue)
    {
        if (!Object.valid() || Object == sol::nil)
        {
            return false;
        }
        if (Object.get_type() == sol::type::number)
        {
            OutValue = static_cast<int64>(Object.as<double>());
            return true;
        }
        if (Object.get_type() == sol::type::string)
        {
            FString Name = Object.as<FString>();
            if (!EnumType)
            {
                return false;
            }
            for (uint32 Index = 0; Index < EnumType->GetCount(); ++Index)
            {
                const char* EntryName = EnumType->GetNameAt(Index);
                if (EntryName && Name == EntryName)
                {
                    OutValue = EnumType->GetValueAt(Index);
                    return true;
                }
            }
        }
        return false;
    }

    sol::object LuaStructToTable(sol::this_state State, const FStructProperty& Property, void* ValuePtr)
    {
        sol::state_view Lua(State);
        sol::table      Table      = Lua.create_table();
        UStruct*        StructType = Property.GetStructType();
        if (!StructType || !ValuePtr)
        {
            return sol::make_object(Lua, Table);
        }
        TArray<const FProperty*> Children;
        StructType->GetPropertyRefs(Children);
        for (const FProperty* Child : Children)
        {
            if (!Child || !Child->Name)
            {
                continue;
            }
            Table[Child->Name] = LuaValueToObject(State, *Child, Child->GetValuePtrFor(ValuePtr));
        }
        return sol::make_object(Lua, Table);
    }

    bool LuaTableToStruct(const FStructProperty& Property, void* ValuePtr, const sol::object& Object, FString* OutError)
    {
        if (!ValuePtr)
        {
            if (OutError) *OutError = "null struct storage";
            return false;
        }
        if (Object.get_type() != sol::type::table)
        {
            if (OutError) *OutError = "expected table for struct";
            return false;
        }
        UStruct* StructType = Property.GetStructType();
        if (!StructType)
        {
            if (OutError) *OutError = "struct type is not registered";
            return false;
        }
        sol::table               Table = Object.as<sol::table>();
        TArray<const FProperty*> Children;
        StructType->GetPropertyRefs(Children);
        for (const FProperty* Child : Children)
        {
            if (!Child || !Child->Name)
            {
                continue;
            }
            sol::object Field = Table[Child->Name];
            if (!Field.valid() || Field == sol::nil)
            {
                continue;
            }
            if (!LuaObjectToValue(*Child, Child->GetValuePtrFor(ValuePtr), Field, OutError))
            {
                return false;
            }
        }
        return true;
    }

    sol::object LuaArrayToTable(sol::this_state State, const FArrayProperty& Property, void* ValuePtr)
    {
        sol::state_view                  Lua(State);
        sol::table                       Table = Lua.create_table();
        const FArrayProperty::FArrayOps* Ops   = Property.GetArrayOps();
        const FProperty*                 Inner = Property.GetInnerProperty();
        if (!ValuePtr || !Ops || !Ops->GetNum || !Ops->GetElementPtr || !Inner)
        {
            return sol::make_object(Lua, Table);
        }
        const size_t Count = Ops->GetNum(ValuePtr);
        for (size_t Index = 0; Index < Count; ++Index)
        {
            Table[static_cast<int>(Index + 1)] = LuaValueToObject(State, *Inner, Ops->GetElementPtr(ValuePtr, Index));
        }
        return sol::make_object(Lua, Table);
    }

    bool LuaTableToArray(const FArrayProperty& Property, void* ValuePtr, const sol::object& Object, FString* OutError)
    {
        if (Object.get_type() != sol::type::table)
        {
            if (OutError) *OutError = "expected table for array";
            return false;
        }
        const FArrayProperty::FArrayOps* Ops   = Property.GetArrayOps();
        const FProperty*                 Inner = Property.GetInnerProperty();
        if (!ValuePtr || !Ops || !Ops->Resize || !Ops->GetElementPtr || !Inner)
        {
            if (OutError) *OutError = "array reflection ops are missing";
            return false;
        }
        sol::table   Table = Object.as<sol::table>();
        const size_t Count = static_cast<size_t>(Table.size());
        Ops->Resize(ValuePtr, Count);
        for (size_t Index = 0; Index < Count; ++Index)
        {
            sol::object Element = Table[static_cast<int>(Index + 1)];
            if (!LuaObjectToValue(*Inner, Ops->GetElementPtr(ValuePtr, Index), Element, OutError))
            {
                return false;
            }
        }
        return true;
    }

    sol::object LuaValueToObject(sol::this_state State, const FProperty& Property, void* ValuePtr)
    {
        sol::state_view Lua(State);
        if (!ValuePtr)
        {
            return sol::make_object(Lua, sol::nil);
        }

        switch (Property.GetType())
        {
        case EPropertyType::Bool:
            return sol::make_object(Lua, *static_cast<bool*>(ValuePtr));
        case EPropertyType::ByteBool:
            return sol::make_object(Lua, *static_cast<uint8*>(ValuePtr) != 0);
        case EPropertyType::Int:
            return sol::make_object(Lua, *static_cast<int32*>(ValuePtr));
        case EPropertyType::Float:
            return sol::make_object(Lua, *static_cast<float*>(ValuePtr));
        case EPropertyType::String:
            return sol::make_object(Lua, *static_cast<FString*>(ValuePtr));
        case EPropertyType::Name:
            return sol::make_object(Lua, static_cast<FName*>(ValuePtr)->ToString());
        case EPropertyType::Vec3:
            return sol::make_object(Lua, *static_cast<FVector*>(ValuePtr));
        case EPropertyType::Rotator:
            return sol::make_object(Lua, static_cast<FRotator*>(ValuePtr)->ToVector());
        case EPropertyType::Vec4:
        case EPropertyType::Color4:
            return sol::make_object(Lua, LuaVector4ToTable(State, *static_cast<FVector4*>(ValuePtr)));
        case EPropertyType::Enum:
        {
            const FEnum* EnumType  = Property.GetEnumType();
            int64        EnumValue = 0;
            std::memcpy(&EnumValue, ValuePtr, std::min<size_t>(Property.Size, sizeof(EnumValue)));
            if (EnumType)
            {
                const char* Name = EnumType->GetNameByValue(EnumValue);
                if (Name && std::strcmp(Name, "Unknown") != 0)
                {
                    return sol::make_object(Lua, FString(Name));
                }
            }
            return sol::make_object(Lua, EnumValue);
        }
        case EPropertyType::ObjectRef:
        {
            const FObjectProperty* ObjectProperty = Property.AsObjectProperty();
            return sol::make_object(
                Lua,
                ObjectProperty ? ObjectProperty->GetObjectValueFromValuePtr(ValuePtr) : nullptr
            );
        }
        case EPropertyType::ClassRef:
        {
            const FClassProperty* ClassProperty = Property.AsClassProperty();
            UClass*               Class         = ClassProperty ? ClassProperty->GetClassValueFromValuePtr(ValuePtr) : nullptr;
            return Class ? sol::make_object(Lua, FString(Class->GetName())) : sol::make_object(Lua, sol::nil);
        }
        case EPropertyType::SoftObjectRef:
        {
            const FSoftObjectProperty* SoftProperty = Property.AsSoftObjectProperty();
            return SoftProperty ? sol::make_object(Lua, SoftProperty->GetPathFromValuePtr(ValuePtr))
                    : sol::make_object(Lua, sol::nil);
        }
        case EPropertyType::Struct:
        {
            const FStructProperty* StructProperty = Property.AsStructProperty();
            return StructProperty ? LuaStructToTable(State, *StructProperty, ValuePtr)
                    : sol::make_object(Lua, sol::nil);
        }
        case EPropertyType::Array:
        {
            const FArrayProperty* ArrayProperty = Property.AsArrayProperty();
            return ArrayProperty ? LuaArrayToTable(State, *ArrayProperty, ValuePtr) : sol::make_object(Lua, sol::nil);
        }
        default:
            return sol::make_object(Lua, sol::nil);
        }
    }

    bool LuaObjectToValue(const FProperty& Property, void* ValuePtr, const sol::object& Object, FString* OutError)
    {
        if (!ValuePtr)
        {
            if (OutError) *OutError = "null value storage";
            return false;
        }
        if (!Object.valid() || Object == sol::nil)
        {
            if (Property.GetType() == EPropertyType::ObjectRef)
            {
                if (const FObjectProperty* ObjectProperty = Property.AsObjectProperty())
                {
                    ObjectProperty->SetObjectValueFromValuePtr(ValuePtr, nullptr);
                    return true;
                }
            }
            if (Property.GetType() == EPropertyType::ClassRef)
            {
                if (const FClassProperty* ClassProperty = Property.AsClassProperty())
                {
                    ClassProperty->SetClassValueFromValuePtr(ValuePtr, nullptr);
                    return true;
                }
            }
            if (OutError) *OutError = FString("nil is not assignable to ") + LuaPropertyTypeName(Property.GetType());
            return false;
        }

        switch (Property.GetType())
        {
        case EPropertyType::Bool:
            if (!Object.is<bool>())
            {
                if (OutError) *OutError = "expected bool";
                return false;
            }
            *static_cast<bool*>(ValuePtr) = Object.as<bool>();
            return true;
        case EPropertyType::ByteBool:
            if (!Object.is<bool>())
            {
                if (OutError) *OutError = "expected bool";
                return false;
            }
            *static_cast<uint8*>(ValuePtr) = Object.as<bool>() ? 1 : 0;
            return true;
        case EPropertyType::Int:
            if (Object.get_type() != sol::type::number)
            {
                if (OutError) *OutError = "expected number";
                return false;
            }
            *static_cast<int32*>(ValuePtr) = static_cast<int32>(Object.as<double>());
            return true;
        case EPropertyType::Float:
            if (Object.get_type() != sol::type::number)
            {
                if (OutError) *OutError = "expected number";
                return false;
            }
            *static_cast<float*>(ValuePtr) = static_cast<float>(Object.as<double>());
            return true;
        case EPropertyType::String:
            if (Object.get_type() != sol::type::string)
            {
                if (OutError) *OutError = "expected string";
                return false;
            }
            *static_cast<FString*>(ValuePtr) = Object.as<FString>();
            return true;
        case EPropertyType::Name:
            if (Object.get_type() != sol::type::string)
            {
                if (OutError) *OutError = "expected string";
                return false;
            }
            *static_cast<FName*>(ValuePtr) = FName(Object.as<FString>());
            return true;
        case EPropertyType::Vec3:
        {
            FVector Vector;
            if (!LuaObjectToVector(Object, Vector))
            {
                if (OutError) *OutError = "expected Vector or {X,Y,Z}";
                return false;
            }
            *static_cast<FVector*>(ValuePtr) = Vector;
            return true;
        }
        case EPropertyType::Rotator:
        {
            FVector Vector;
            if (!LuaObjectToVector(Object, Vector))
            {
                if (OutError) *OutError = "expected Vector or {X,Y,Z}";
                return false;
            }
            *static_cast<FRotator*>(ValuePtr) = FRotator(Vector);
            return true;
        }
        case EPropertyType::Vec4:
        case EPropertyType::Color4:
        {
            FVector4 Vector;
            if (!LuaObjectToVector4(Object, Vector))
            {
                if (OutError) *OutError = "expected {X,Y,Z,W}";
                return false;
            }
            *static_cast<FVector4*>(ValuePtr) = Vector;
            return true;
        }
        case EPropertyType::Enum:
        {
            int64 EnumValue = 0;
            if (!LuaObjectToEnumValue(Property.GetEnumType(), Object, EnumValue))
            {
                if (OutError) *OutError = "expected enum name or value";
                return false;
            }
            std::memcpy(ValuePtr, &EnumValue, std::min<size_t>(Property.Size, sizeof(EnumValue)));
            return true;
        }
        case EPropertyType::ObjectRef:
        {
            if (!Object.is<UObject*>())
            {
                if (OutError) *OutError = "expected Object";
                return false;
            }
            UObject*               SourceObject   = Object.as<UObject*>();
            const FObjectProperty* ObjectProperty = Property.AsObjectProperty();
            if (!ObjectProperty)
            {
                if (OutError) *OutError = "object property ops missing";
                return false;
            }
            if (SourceObject && !IsValid(SourceObject))
            {
                if (OutError) *OutError = "object is no longer valid";
                return false;
            }
            if (UClass* AllowedClass = ObjectProperty->GetAllowedClassType())
            {
                if (SourceObject && !SourceObject->GetClass()->IsA(AllowedClass))
                {
                    if (OutError) *OutError = "object class is not assignable to parameter";
                    return false;
                }
            }
            ObjectProperty->SetObjectValueFromValuePtr(ValuePtr, SourceObject);
            return true;
        }
        case EPropertyType::ClassRef:
        {
            const FClassProperty* ClassProperty = Property.AsClassProperty();
            if (!ClassProperty)
            {
                if (OutError) *OutError = "class property ops missing";
                return false;
            }
            if (Object.get_type() != sol::type::string)
            {
                if (OutError) *OutError = "expected class name string";
                return false;
            }
            UClass* Class = UClass::FindByName(Object.as<FString>().c_str());
            if (!Class)
            {
                if (OutError) *OutError = "class was not found";
                return false;
            }
            if (UClass* AllowedClass = ClassProperty->GetAllowedClassType())
            {
                if (!Class->IsA(AllowedClass))
                {
                    if (OutError) *OutError = "class is not assignable to parameter";
                    return false;
                }
            }
            ClassProperty->SetClassValueFromValuePtr(ValuePtr, Class);
            return true;
        }
        case EPropertyType::SoftObjectRef:
        {
            const FSoftObjectProperty* SoftProperty = Property.AsSoftObjectProperty();
            if (!SoftProperty)
            {
                if (OutError) *OutError = "soft object property ops missing";
                return false;
            }
            if (Object.get_type() != sol::type::string)
            {
                if (OutError) *OutError = "expected asset path string";
                return false;
            }
            SoftProperty->SetPathFromValuePtr(ValuePtr, Object.as<FString>());
            return true;
        }
        case EPropertyType::Struct:
        {
            const FStructProperty* StructProperty = Property.AsStructProperty();
            return StructProperty ? LuaTableToStruct(*StructProperty, ValuePtr, Object, OutError) : false;
        }
        case EPropertyType::Array:
        {
            const FArrayProperty* ArrayProperty = Property.AsArrayProperty();
            return ArrayProperty ? LuaTableToArray(*ArrayProperty, ValuePtr, Object, OutError) : false;
        }
        default:
            if (OutError) *OutError = "unsupported reflected property type for Lua";
            return false;
        }
    }

    bool LuaCanSkipMissingArgument(const FProperty& Parameter)
    {
        if ((Parameter.Flags & PF_OutParm) != 0 && (Parameter.Flags & PF_ConstParm) == 0)
        {
            return true;
        }
        return Parameter.Metadata.find("defaultvalue") != Parameter.Metadata.end();
    }

    bool LuaTryPrepareFunctionCall(const FFunction& Function, sol::variadic_args Args, void* Storage, FString& OutError)
    {
        const TArray<FProperty*>& Parameters  = Function.GetParameters();
        const size_t              LuaArgCount = Args.size();
        size_t                    LuaArgIndex = 0;

        for (const FProperty* Parameter : Parameters)
        {
            if (!Parameter)
            {
                continue;
            }

            const bool bOutOnly = (Parameter->Flags & PF_OutParm) != 0 && (Parameter->Flags & PF_ConstParm) == 0;
            if (bOutOnly && LuaArgIndex >= LuaArgCount)
            {
                continue;
            }

            if (LuaArgIndex >= LuaArgCount)
            {
                if (LuaCanSkipMissingArgument(*Parameter))
                {
                    continue;
                }
                OutError = FString("missing Lua argument for parameter '") + (Parameter->Name ? Parameter->Name : "") +
                        "'";
                return false;
            }

            sol::object Arg = Args[static_cast<int>(LuaArgIndex)];
            FString     ConvertError;
            if (!LuaObjectToValue(*Parameter, Parameter->GetValuePtrFor(Storage), Arg, &ConvertError))
            {
                OutError = FString("parameter '") + (Parameter->Name ? Parameter->Name : "") + "': " + ConvertError;
                return false;
            }
            ++LuaArgIndex;
        }

        if (LuaArgIndex < LuaArgCount)
        {
            OutError = "too many Lua arguments";
            return false;
        }
        return true;
    }

    sol::object LuaCollectFunctionResult(sol::this_state State, const FFunction& Function, void* Storage)
    {
        sol::state_view          Lua(State);
        const FProperty*         ReturnProperty = Function.GetReturnProperty();
        TArray<const FProperty*> OutParameters;
        for (const FProperty* Parameter : Function.GetParameters())
        {
            if (Parameter && (Parameter->Flags & PF_OutParm) != 0 && (Parameter->Flags & PF_ConstParm) == 0)
            {
                OutParameters.push_back(Parameter);
            }
        }

        if (!ReturnProperty && OutParameters.empty())
        {
            return sol::make_object(Lua, true);
        }
        if (ReturnProperty && OutParameters.empty())
        {
            return LuaValueToObject(State, *ReturnProperty, ReturnProperty->GetValuePtrFor(Storage));
        }

        sol::table Result = Lua.create_table();
        if (ReturnProperty)
        {
            Result["ReturnValue"] = LuaValueToObject(State, *ReturnProperty, ReturnProperty->GetValuePtrFor(Storage));
        }
        for (const FProperty* Parameter : OutParameters)
        {
            Result[(Parameter->Name ? Parameter->Name : "")] = LuaValueToObject(
                State,
                *Parameter,
                Parameter->GetValuePtrFor(Storage)
            );
        }
        return sol::make_object(Lua, Result);
    }

    const FFunction* LuaFindFunctionByNameOrSignature(UStruct* TargetStruct, const FString& FunctionNameOrSignature)
    {
        if (!TargetStruct || FunctionNameOrSignature.empty())
        {
            return nullptr;
        }

        if (FunctionNameOrSignature.find('(') != FString::npos)
        {
            return TargetStruct->FindFunctionBySignature(FunctionNameOrSignature.c_str(), true);
        }

        return TargetStruct->FindFunctionByName(FunctionNameOrSignature.c_str(), true);
    }

    bool LuaCopyFunctionResultFromObject(
        sol::this_state    State,
        const FFunction&   Function,
        void*              Storage,
        const sol::object& ResultObject,
        FString&           OutError
    )
    {
        (void)State;
        const bool bResultIsTable = ResultObject.valid() && ResultObject.get_type() == sol::type::table;

        if (const FProperty* ReturnProperty = Function.GetReturnProperty())
        {
            sol::object ReturnValue = bResultIsTable ? ResultObject.as<sol::table>()["ReturnValue"].get<sol::object>()
                    : ResultObject;
            if (ReturnValue.valid() && ReturnValue != sol::nil)
            {
                FString ConvertError;
                if (!LuaObjectToValue(
                    *ReturnProperty,
                    ReturnProperty->GetValuePtrFor(Storage),
                    ReturnValue,
                    &ConvertError
                ))
                {
                    OutError = FString("return value: ") + ConvertError;
                    return false;
                }
            }
        }

        if (bResultIsTable)
        {
            sol::table ResultTable = ResultObject.as<sol::table>();
            for (const FProperty* Parameter : Function.GetParameters())
            {
                if (!Parameter || (Parameter->Flags & PF_OutParm) == 0 || (Parameter->Flags & PF_ConstParm) != 0)
                {
                    continue;
                }

                sol::object OutValue = ResultTable[Parameter->Name ? Parameter->Name : ""].get<sol::object>();
                if (!OutValue.valid() || OutValue == sol::nil)
                {
                    continue;
                }

                FString ConvertError;
                if (!LuaObjectToValue(*Parameter, Parameter->GetValuePtrFor(Storage), OutValue, &ConvertError))
                {
                    OutError = FString("out parameter '") + (Parameter->Name ? Parameter->Name : "") + "': " +
                            ConvertError;
                    return false;
                }
            }
        }

        return true;
    }

    enum class ELuaEventOverrideResult : uint8
    {
        NotBound,
        Invoked,
        Failed,
    };

    ELuaEventOverrideResult LuaTryInvokeReflectedEventOverride(
        sol::this_state  State,
        UObject*         Instance,
        const FFunction& Function,
        void*            Storage,
        FString&         OutError
    )
    {
        if (!IsValid(Instance) || !Function.HasAnyFunctionFlags(FUNC_Event))
        {
            return ELuaEventOverrideResult::NotBound;
        }

        CompactLuaReflectedEventOverrides();
        auto It = GLuaReflectedEventOverrides.find(MakeLuaReflectedEventKey(Instance, Function));
        if (It == GLuaReflectedEventOverrides.end() || It->second.Target.Get() != Instance || !It->second.Callback.valid())
        {
            return ELuaEventOverrideResult::NotBound;
        }

        TArray<sol::object> LuaArgs;
        for (const FProperty* Parameter : Function.GetParameters())
        {
            if (!Parameter)
            {
                continue;
            }
            const bool bOutOnly = (Parameter->Flags & PF_OutParm) != 0 && (Parameter->Flags & PF_ConstParm) == 0;
            if (bOutOnly)
            {
                continue;
            }
            LuaArgs.push_back(LuaValueToObject(State, *Parameter, Parameter->GetValuePtrFor(Storage)));
        }

        FScopedGarbageCollectionBlocker GCBlocker;
        sol::protected_function_result  Result = It->second.Callback(sol::as_args(LuaArgs));
        if (!Result.valid())
        {
            sol::error Err = Result;
            OutError       = Err.what();
            return ELuaEventOverrideResult::Failed;
        }

        sol::object ResultObject = Result.get<sol::object>();
        if (!LuaCopyFunctionResultFromObject(State, Function, Storage, ResultObject, OutError))
        {
            return ELuaEventOverrideResult::Failed;
        }

        return ELuaEventOverrideResult::Invoked;
    }

    sol::object LuaInvokeReflectedFunctionBySignature(
        sol::this_state    State,
        UObject*           Instance,
        UClass*            StaticClass,
        const FString&     Signature,
        sol::variadic_args Args
    )
    {
        FScopedGarbageCollectionBlocker GCBlocker;
        sol::state_view                 Lua(State);
        if (Instance && !IsValid(Instance))
        {
            UE_LOG("[LuaReflection] Reflection.CallSignature failed: target object is no longer valid");
            return sol::make_object(Lua, sol::nil);
        }
        UStruct* TargetStruct = Instance ? Instance->GetClass() : StaticClass;
        if (!TargetStruct)
        {
            UE_LOG("[LuaReflection] Reflection.CallSignature failed: target has no reflected class");
            return sol::make_object(Lua, sol::nil);
        }

        const FFunction* Function = TargetStruct->FindFunctionBySignature(Signature.c_str(), true);
        if (!Function)
        {
            UE_LOG("[LuaReflection] Reflection.CallSignature failed: function not found: %s", Signature.c_str());
            return sol::make_object(Lua, sol::nil);
        }
        if (!Instance && !Function->IsStatic())
        {
            UE_LOG(
                "[LuaReflection] Reflection.CallSignature failed: non-static function requires object instance: %s",
                Signature.c_str()
            );
            return sol::make_object(Lua, sol::nil);
        }

        void* Storage = Function->CreateParameterStorage();
        if (!Storage)
        {
            UE_LOG(
                "[LuaReflection] Reflection.CallSignature failed: failed to allocate parameter storage: %s",
                Signature.c_str()
            );
            return sol::make_object(Lua, sol::nil);
        }

        FString PrepareError;
        if (!LuaTryPrepareFunctionCall(*Function, Args, Storage, PrepareError))
        {
            Function->DestroyParameterStorage(Storage);
            UE_LOG("[LuaReflection] Reflection.CallSignature failed: %s: %s", Signature.c_str(), PrepareError.c_str());
            return sol::make_object(Lua, sol::nil);
        }

        FString                       EventError;
        const ELuaEventOverrideResult EventResult = LuaTryInvokeReflectedEventOverride(
            State,
            Instance,
            *Function,
            Storage,
            EventError
        );
        if (EventResult == ELuaEventOverrideResult::Failed)
        {
            Function->DestroyParameterStorage(Storage);
            UE_LOG(
                "[LuaReflection] Reflection.CallSignature event override failed: %s: %s",
                Signature.c_str(),
                EventError.c_str()
            );
            return sol::make_object(Lua, sol::nil);
        }

        if (EventResult == ELuaEventOverrideResult::NotBound)
        {
            const bool bInvoked = Function->IsStatic()
                    ? Function->Invoke(nullptr, Storage, nullptr)
                    : Instance->ProcessEvent(Function, Storage, nullptr);
            if (!bInvoked)
            {
                Function->DestroyParameterStorage(Storage);
                UE_LOG("[LuaReflection] Reflection.CallSignature failed: native invoke failed: %s", Signature.c_str());
                return sol::make_object(Lua, sol::nil);
            }
        }

        sol::object Result = LuaCollectFunctionResult(State, *Function, Storage);
        Function->DestroyParameterStorage(Storage);
        return Result;
    }

    bool LuaBindReflectedEventOverride(
        UObject*                Object,
        const FString&          FunctionNameOrSignature,
        sol::protected_function Callback
    )
    {
        if (!IsValid(Object) || !Object->GetClass() || !Callback.valid())
        {
            return false;
        }

        const FFunction* Function = LuaFindFunctionByNameOrSignature(Object->GetClass(), FunctionNameOrSignature);
        if (!Function || !Function->HasAnyFunctionFlags(FUNC_Event))
        {
            UE_LOG("[LuaReflection] BindEvent failed: reflected event not found: %s", FunctionNameOrSignature.c_str());
            return false;
        }

        FLuaReflectedEventOverride Override;
        Override.Target                                                          = Object;
        Override.Callback                                                        = std::move(Callback);
        GLuaReflectedEventOverrides[MakeLuaReflectedEventKey(Object, *Function)] = std::move(Override);
        CompactLuaReflectedEventOverrides();
        return true;
    }

    bool LuaUnbindReflectedEventOverride(UObject* Object, const FString& FunctionNameOrSignature)
    {
        if (!IsValid(Object) || !Object->GetClass())
        {
            return false;
        }

        const FFunction* Function = LuaFindFunctionByNameOrSignature(Object->GetClass(), FunctionNameOrSignature);
        if (!Function)
        {
            return false;
        }

        return GLuaReflectedEventOverrides.erase(MakeLuaReflectedEventKey(Object, *Function)) > 0;
    }

    bool LuaHasReflectedEventOverride(UObject* Object, const FString& FunctionNameOrSignature)
    {
        if (!IsValid(Object) || !Object->GetClass())
        {
            return false;
        }

        const FFunction* Function = LuaFindFunctionByNameOrSignature(Object->GetClass(), FunctionNameOrSignature);
        if (!Function)
        {
            return false;
        }

        CompactLuaReflectedEventOverrides();
        auto It = GLuaReflectedEventOverrides.find(MakeLuaReflectedEventKey(Object, *Function));
        return It != GLuaReflectedEventOverrides.end() && It->second.Target.Get() == Object && It->second.Callback.valid();
    }

    sol::object LuaInvokeReflectedFunction(
        sol::this_state    State,
        UObject*           Instance,
        UClass*            StaticClass,
        const FString&     FunctionName,
        sol::variadic_args Args
    )
    {
        FScopedGarbageCollectionBlocker GCBlocker;
        sol::state_view                 Lua(State);
        if (Instance && !IsValid(Instance))
        {
            UE_LOG("[LuaReflection] Reflection.Call failed: target object is no longer valid");
            return sol::make_object(Lua, sol::nil);
        }
        UStruct* TargetStruct = Instance ? Instance->GetClass() : StaticClass;
        if (!TargetStruct)
        {
            UE_LOG("[LuaReflection] Reflection.Call failed: target has no reflected class");
            return sol::make_object(Lua, sol::nil);
        }

        TArray<const FFunction*> Functions;
        TargetStruct->FindFunctionsByName(FunctionName.c_str(), Functions, true);
        if (Functions.empty())
        {
            UE_LOG("[LuaReflection] Reflection.Call failed: function not found: %s", FunctionName.c_str());
            return sol::make_object(Lua, sol::nil);
        }

        FString LastError;
        for (const FFunction* Function : Functions)
        {
            if (!Function)
            {
                continue;
            }
            if (!Instance && !Function->IsStatic())
            {
                LastError = "non-static function requires object instance";
                continue;
            }

            void* Storage = Function->CreateParameterStorage();
            if (!Storage)
            {
                LastError = "failed to allocate parameter storage";
                continue;
            }

            FString PrepareError;
            if (!LuaTryPrepareFunctionCall(*Function, Args, Storage, PrepareError))
            {
                Function->DestroyParameterStorage(Storage);
                LastError = PrepareError;
                continue;
            }

            FString                       EventError;
            const ELuaEventOverrideResult EventResult = LuaTryInvokeReflectedEventOverride(
                State,
                Instance,
                *Function,
                Storage,
                EventError
            );
            if (EventResult == ELuaEventOverrideResult::Failed)
            {
                Function->DestroyParameterStorage(Storage);
                LastError = FString("event override failed: ") + EventError;
                continue;
            }

            if (EventResult == ELuaEventOverrideResult::NotBound)
            {
                const bool bInvoked = Function->IsStatic()
                        ? Function->Invoke(nullptr, Storage, nullptr)
                        : Instance->ProcessEvent(Function, Storage, nullptr);
                if (!bInvoked)
                {
                    Function->DestroyParameterStorage(Storage);
                    LastError = "native invoke failed";
                    continue;
                }
            }

            sol::object Result = LuaCollectFunctionResult(State, *Function, Storage);
            Function->DestroyParameterStorage(Storage);
            return Result;
        }

        UE_LOG(
            "[LuaReflection] Reflection.Call failed: no overload matched for %s: %s",
            FunctionName.c_str(),
            LastError.c_str()
        );
        return sol::make_object(Lua, sol::nil);
    }

    const FProperty* LuaFindProperty(UObject* Object, const FString& PropertyName)
    {
        if (!IsValid(Object) || !Object->GetClass())
        {
            return nullptr;
        }
        TArray<const FProperty*> Properties;
        Object->GetClass()->GetPropertyRefs(Properties);
        for (const FProperty* Property : Properties)
        {
            if (Property && Property->Name && PropertyName == Property->Name)
            {
                return Property;
            }
        }
        return nullptr;
    }

    sol::object LuaGetReflectedProperty(sol::this_state State, UObject* Object, const FString& PropertyName)
    {
        sol::state_view  Lua(State);
        const FProperty* Property = LuaFindProperty(Object, PropertyName);
        if (!Property)
        {
            return sol::make_object(Lua, sol::nil);
        }
        return LuaValueToObject(State, *Property, Property->GetValuePtrFor(Object));
    }

    bool LuaSetReflectedProperty(UObject* Object, const FString& PropertyName, sol::object Value)
    {
        const FProperty* Property = LuaFindProperty(Object, PropertyName);
        if (!IsValid(Object) || !Property)
        {
            return false;
        }
        FString    Error;
        const bool bOk = LuaObjectToValue(*Property, Property->GetValuePtrFor(Object), Value, &Error);
        if (!bOk)
        {
            UE_LOG(
                "[LuaReflection] SetProperty failed: %s.%s: %s",
                Object->GetClass()->GetName(),
                PropertyName.c_str(),
                Error.c_str()
            );
        }
        return bOk;
    }

    sol::table LuaDescribeProperty(sol::this_state State, const FProperty& Property)
    {
        sol::state_view Lua(State);
        sol::table      Desc = Lua.create_table();
        Desc["Name"]         = Property.Name ? Property.Name : "";
        Desc["DisplayName"]  = Property.DisplayName ? Property.DisplayName : (Property.Name ? Property.Name : "");
        Desc["Category"]     = Property.Category ? Property.Category : "";
        Desc["Type"]         = LuaPropertyTypeName(Property.GetType());
        Desc["Flags"]        = Property.Flags;
        Desc["OwnerClass"]   = Property.OwnerClassName ? Property.OwnerClassName : "";
        if (const FArrayProperty* ArrayProperty = Property.AsArrayProperty())
        {
            Desc["ElementType"] = LuaPropertyTypeName(ArrayProperty->GetElementType());
        }
        if (const FEnum* EnumType = Property.GetEnumType())
        {
            Desc["Enum"] = EnumType->GetName();
        }
        if (UStruct* StructType = Property.GetStructType())
        {
            Desc["Struct"] = StructType->GetName();
        }
        return Desc;
    }

    sol::table LuaDescribeFunction(sol::this_state State, const FFunction& Function)
    {
        sol::state_view Lua(State);
        sol::table      Desc = Lua.create_table();
        Desc["Name"]         = Function.GetName();
        Desc["Signature"]    = Function.GetSignature();
        Desc["DisplayName"]  = Function.GetDisplayName();
        Desc["Category"]     = Function.GetCategory();
        Desc["Flags"]        = Function.GetFlags();
        Desc["Const"]        = Function.IsConst();
        Desc["Static"]       = Function.IsStatic();
        Desc["OwnerClass"]   = Function.OwnerClassName ? Function.OwnerClassName : "";
        sol::table Params    = Lua.create_table();
        int        Index     = 1;
        for (const FProperty* Parameter : Function.GetParameters())
        {
            if (Parameter)
            {
                Params[Index++] = LuaDescribeProperty(State, *Parameter);
            }
        }
        Desc["Parameters"] = Params;
        if (const FProperty* ReturnProperty = Function.GetReturnProperty())
        {
            Desc["Return"] = LuaDescribeProperty(State, *ReturnProperty);
        }
        return Desc;
    }
}

void FLuaScriptManager::ClearReflectedEventOverrides()
{
    GLuaReflectedEventOverrides.clear();
}

namespace
{
    FString LuaTableStringAny(const sol::table& Table, std::initializer_list<const char*> Keys, const FString& DefaultValue = FString())
    {
        for (const char* Key : Keys)
        {
            sol::object Value = Table.get<sol::object>(Key);
            if (Value.valid() && Value != sol::nil && Value.get_type() == sol::type::string)
            {
                return Value.as<FString>();
            }
        }

        return DefaultValue;
    }

    float LuaTableFloatAny(const sol::table& Table, std::initializer_list<const char*> Keys, float DefaultValue)
    {
        for (const char* Key : Keys)
        {
            sol::object Value = Table.get<sol::object>(Key);
            if (Value.valid() && Value != sol::nil && Value.get_type() == sol::type::number)
            {
                return Value.as<float>();
            }
        }

        return DefaultValue;
    }
}

void FLuaScriptManager::RegisterReflectionBindings(sol::state& Lua)
{
    Lua.new_usertype<UClass>(
        "UClass",
        "GetName",
        [](const UClass& Class)
        {
            return FString(Class.GetName() ? Class.GetName() : "");
        },
        "GetSuperClass",
        &UClass::GetSuperClass,
        "GetFlags",
        &UClass::GetClassFlags,
        "IsA",
        [](const UClass& Class, UClass* Other)
        {
            return Other && Class.IsA(Other);
        },
        "HasAnyClassFlags",
        &UClass::HasAnyClassFlags
    );

    sol::table Class = Lua.create_named_table("Class");
    Class.set_function(
        "Find",
        [](const FString& ClassName) -> UClass*
        {
            return UClass::FindByName(ClassName.c_str());
        }
    );
    Class.set_function(
        "Exists",
        [](const FString& ClassName)
        {
            return UClass::FindByName(ClassName.c_str()) != nullptr;
        }
    );
    Class.set_function(
        "All",
        [](sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            int32           Idx    = 1;
            for (UClass* C : UClass::GetAllClasses()) if (C) Result[Idx++] = C;
            return Result;
        }
    );
    Class.set_function(
        "GetFunctions",
        [](const FString& ClassName, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            UClass*         C      = UClass::FindByName(ClassName.c_str());
            if (!C) return Result;
            TArray<const FFunction*> Functions;
            C->GetFunctionRefs(Functions);
            int32 Idx = 1;
            for (const FFunction* F : Functions) if (F) Result[Idx++] = LuaDescribeFunction(State, *F);
            return Result;
        }
    );
    Class.set_function(
        "GetProperties",
        [](const FString& ClassName, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            UClass*         C      = UClass::FindByName(ClassName.c_str());
            if (!C) return Result;
            TArray<const FProperty*> Props;
            C->GetPropertyRefs(Props);
            int32 Idx = 1;
            for (const FProperty* P : Props) if (P) Result[Idx++] = LuaDescribeProperty(State, *P);
            return Result;
        }
    );

    Lua.new_usertype<UObject>(
        "Object",
        "GetName",
        [](UObject& Object)
        {
            return Object.GetName();
        },
        "GetClass",
        [](UObject& Object) -> UClass*
        {
            return IsValid(&Object) ? Object.GetClass() : nullptr;
        },
        "GetClassName",
        [](UObject& Object)
        {
            return IsValid(&Object) && Object.GetClass() ? FString(Object.GetClass()->GetName()) : FString();
        },
        "IsA",
        [](UObject& Object, const FString& ClassName)
        {
            UClass* C = UClass::FindByName(ClassName.c_str());
            return IsValid(&Object) && Object.GetClass() && C && Object.GetClass()->IsA(C);
        },
        "AsActor",
        [](UObject& Object) -> AActor*
        {
            return IsValid(&Object) ? Cast<AActor>(&Object) : nullptr;
        },
        "AsPawn",
        [](UObject& Object) -> APawn*
        {
            return IsValid(&Object) ? Cast<APawn>(&Object) : nullptr;
        },
        "AsPrimitiveComponent",
        [](UObject& Object) -> UPrimitiveComponent*
        {
            return IsValid(&Object) ? Cast<UPrimitiveComponent>(&Object) : nullptr;
        },
        "AsSceneComponent",
        [](UObject& Object) -> USceneComponent*
        {
            return IsValid(&Object) ? Cast<USceneComponent>(&Object) : nullptr;
        },
        "AsComponent",
        [](UObject& Object) -> UActorComponent*
        {
            return IsValid(&Object) ? Cast<UActorComponent>(&Object) : nullptr;
        },
        "AsActorSequenceComponent",
        [](UObject& Object) -> UActorSequenceComponent*
        {
            return IsValid(&Object) ? Cast<UActorSequenceComponent>(&Object) : nullptr;
        },
        "GetUUID",
        &UObject::GetUUID,
        "IsValid",
        [](UObject* Object)
        {
            return IsValid(Object);
        },
        "CallFunction",
        [](UObject& Object, const FString& FunctionName, sol::variadic_args Args, sol::this_state State)
        {
            if (!IsValid(&Object))
            {
                return sol::make_object(sol::state_view(State), sol::nil);
            }
            return LuaInvokeReflectedFunction(State, &Object, nullptr, FunctionName, Args);
        },
        "CallFunctionSignature",
        [](UObject& Object, const FString& Signature, sol::variadic_args Args, sol::this_state State)
        {
            if (!IsValid(&Object))
            {
                return sol::make_object(sol::state_view(State), sol::nil);
            }
            return LuaInvokeReflectedFunctionBySignature(State, &Object, nullptr, Signature, Args);
        },
        "BindEvent",
        [](UObject& Object, const FString& FunctionNameOrSignature, sol::protected_function Callback)
        {
            return IsValid(&Object) && LuaBindReflectedEventOverride(&Object, FunctionNameOrSignature, std::move(Callback));
        },
        "UnbindEvent",
        [](UObject& Object, const FString& FunctionNameOrSignature)
        {
            return IsValid(&Object) && LuaUnbindReflectedEventOverride(&Object, FunctionNameOrSignature);
        },
        "HasEventBinding",
        [](UObject& Object, const FString& FunctionNameOrSignature)
        {
            return IsValid(&Object) && LuaHasReflectedEventOverride(&Object, FunctionNameOrSignature);
        },
        "GetProperty",
        [](UObject& Object, const FString& PropertyName, sol::this_state State)
        {
            return IsValid(&Object) ? LuaGetReflectedProperty(State, &Object, PropertyName) : sol::make_object(sol::state_view(State), sol::nil);
        },
        "SetProperty",
        [](UObject& Object, const FString& PropertyName, sol::object Value)
        {
            return IsValid(&Object) && LuaSetReflectedProperty(&Object, PropertyName, Value);
        },
        "GetFunctions",
        [](UObject& Object, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            if (!IsValid(&Object) || !Object.GetClass())
            {
                return Result;
            }
            TArray<const FFunction*> Functions;
            Object.GetClass()->GetFunctionRefs(Functions);
            int Index = 1;
            for (const FFunction* Function : Functions)
            {
                if (Function)
                {
                    Result[Index++] = LuaDescribeFunction(State, *Function);
                }
            }
            return Result;
        },
        "GetProperties",
        [](UObject& Object, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            if (!IsValid(&Object) || !Object.GetClass())
            {
                return Result;
            }
            TArray<const FProperty*> Properties;
            Object.GetClass()->GetPropertyRefs(Properties);
            int Index = 1;
            for (const FProperty* Property : Properties)
            {
                if (Property)
                {
                    Result[Index++] = LuaDescribeProperty(State, *Property);
                }
            }
            return Result;
        }
    );

    Lua.new_usertype<UActorComponent>(
        "ActorComponent",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetOwner",
        &UActorComponent::GetOwner,
        "IsActive",
        &UActorComponent::IsActive,
        "SetActive",
        &UActorComponent::SetActive,
        "Activate",
        &UActorComponent::Activate,
        "Deactivate",
        &UActorComponent::Deactivate,
        "SetVisibility",
        [](UActorComponent& Component, bool bVisible)
        {
            if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(&Component))
            {
                Primitive->SetVisibility(bVisible);
            }
        },
        "SetVisible",
        [](UActorComponent& Component, bool bVisible)
        {
            if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(&Component))
            {
                Primitive->SetVisibility(bVisible);
            }
        },
        "IsVisible",
        [](UActorComponent& Component)
        {
            if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(&Component))
            {
                return Primitive->IsVisible();
            }
            return false;
        },
        "SetMaterialPath",
        [](UActorComponent& Component, const FString& MaterialPath)
        {
            if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(&Component))
            {
                Billboard->SetMaterial(FMaterialManager::Get().GetOrCreateMaterial(MaterialPath));
                return true;
            }
            return false;
        },
        "SetTickWhenPaused",
        [](UActorComponent& Component, bool bTickWhenPaused)
        {
            Component.PrimaryComponentTick.bTickEvenWhenPaused = bTickWhenPaused;
        },
        "IsTickWhenPaused",
        [](const UActorComponent& Component)
        {
            return Component.PrimaryComponentTick.bTickEvenWhenPaused;
        },
        "HasTag",
        [](UActorComponent& Component, const FString& Tag)
        {
            return Component.HasTag(FName(Tag));
        },
        "AddTag",
        [](UActorComponent& Component, const FString& Tag)
        {
            Component.AddTag(FName(Tag));
        },
        "RemoveTag",
        [](UActorComponent& Component, const FString& Tag)
        {
            Component.RemoveTag(FName(Tag));
        },
        "GetTags",
        [](UActorComponent& Component, sol::this_state State) -> sol::table
        {
            sol::state_view L(State);
            sol::table Result = L.create_table();
            int Index = 1;
            for (const FName& Tag : Component.GetTags())
            {
                Result[Index++] = Tag.ToString();
            }
            return Result;
        },
        "SetTags",
        [](UActorComponent& Component, sol::table Tags)
        {
            TArray<FName> Names;
            for (auto& Entry : Tags)
            {
                sol::object Value = Entry.second;
                if (Value.is<std::string>())
                {
                    Names.push_back(FName(FString(Value.as<std::string>())));
                }
            }
            Component.SetTags(Names);
        },
        "GetPersistentGuid",
        [](UActorComponent& Component)
        {
            return Component.EnsurePersistentGuid();
        }
    );

    Lua.new_usertype<UActorSequence>(
        "ActorSequence",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetStartTime",
        &UActorSequence::GetStartTime,
        "GetDuration",
        &UActorSequence::GetDuration,
        "GetEndTime",
        &UActorSequence::GetEndTime,
        "SetStartTime",
        &UActorSequence::SetStartTime,
        "SetDuration",
        &UActorSequence::SetDuration,
        "SetPlaybackRange",
        &UActorSequence::SetPlaybackRange,
        "Clear",
        &UActorSequence::Clear,
        "ExportToJsonString",
        &UActorSequence::ExportToJsonString,
        "ImportFromJsonString",
        &UActorSequence::ImportFromJsonString
    );

    Lua.new_usertype<UActorSequencePlayer>(
        "ActorSequencePlayer",
        sol::base_classes,
        sol::bases<UObject>(),
        "Play",
        [](UActorSequencePlayer& Player, sol::optional<bool> bResetTime)
        {
            Player.Play(bResetTime.value_or(true));
        },
        "Pause",
        &UActorSequencePlayer::Pause,
        "Stop",
        [](UActorSequencePlayer& Player, sol::optional<bool> bRestoreBaseValues)
        {
            Player.Stop(bRestoreBaseValues.value_or(true));
        },
        "SetCurrentTime",
        &UActorSequencePlayer::SetCurrentTime,
        "GetCurrentTime",
        [](UActorSequencePlayer& Player)
        {
            return (Player.GetCurrentTime)();
        },
        "IsPlaying",
        [](UActorSequencePlayer& Player)
        {
            return Player.IsPlaying();
        },
        "IsPaused",
        [](UActorSequencePlayer& Player)
        {
            return Player.IsPaused();
        }
    );

    Lua.new_usertype<UActorSequenceComponent>(
        "ActorSequenceComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "Play",
        &UActorSequenceComponent::Play,
        "Pause",
        &UActorSequenceComponent::Pause,
        "Stop",
        &UActorSequenceComponent::Stop,
        "SetTickWhenPaused",
        &UActorSequenceComponent::SetTickWhenPaused,
        "IsTickWhenPaused",
        &UActorSequenceComponent::IsTickWhenPaused,
        "GetSequence",
        &UActorSequenceComponent::GetSequence,
        "GetSequencePlayer",
        &UActorSequenceComponent::GetSequencePlayer,
        "AddFloatTrack",
        [](UActorSequenceComponent& Component, sol::table Desc)
        {
            const FString Target = LuaTableStringAny(
                Desc,
                {
                    "TargetObjectName",
                    "Target",
                    "target",
                    "Component",
                    "component",
                    "TargetComponentGuid",
                    "ComponentGuid",
                    "component_guid"
                },
                "Owner");
            const FString Property = LuaTableStringAny(
                Desc,
                {
                    "PropertyName",
                    "Property",
                    "property"
                });
            if (Property.empty())
            {
                UE_LOG("[Lua][ActorSequence] AddFloatTrack rejected: missing property name");
                return false;
            }

            const FString Channel = LuaTableStringAny(
                Desc,
                {
                    "ChannelName",
                    "Channel",
                    "channel"
                },
                "Value");
            const float StartTime = LuaTableFloatAny(
                Desc,
                {
                    "StartTime",
                    "start_time",
                    "start"
                },
                0.0f);
            const float Duration = LuaTableFloatAny(
                Desc,
                {
                    "Duration",
                    "duration"
                },
                1.0f);
            const FString CurveAssetPath = LuaTableStringAny(
                Desc,
                {
                    "CurveAssetPath",
                    "Curve",
                    "curve",
                    "curve_path"
                });

            return Component.AddFloatTrack(Target, Property, Channel, StartTime, Duration, CurveAssetPath);
        }
    );

    sol::table Reflection = Lua.create_named_table("Reflection");
    Reflection.set_function(
        "Call",
        [](UObject* Object, const FString& FunctionName, sol::variadic_args Args, sol::this_state State)
        {
            return LuaInvokeReflectedFunction(State, Object, nullptr, FunctionName, Args);
        }
    );
    Reflection.set_function(
        "CallSignature",
        [](UObject* Object, const FString& Signature, sol::variadic_args Args, sol::this_state State)
        {
            return LuaInvokeReflectedFunctionBySignature(State, Object, nullptr, Signature, Args);
        }
    );
    Reflection.set_function(
        "CallStatic",
        [](const FString& ClassName, const FString& FunctionName, sol::variadic_args Args, sol::this_state State)
        {
            UClass* Class = UClass::FindByName(ClassName.c_str());
            return LuaInvokeReflectedFunction(State, nullptr, Class, FunctionName, Args);
        }
    );
    Reflection.set_function(
        "CallStaticSignature",
        [](const FString& ClassName, const FString& Signature, sol::variadic_args Args, sol::this_state State)
        {
            UClass* Class = UClass::FindByName(ClassName.c_str());
            return LuaInvokeReflectedFunctionBySignature(State, nullptr, Class, Signature, Args);
        }
    );
    Reflection.set_function(
        "BindEvent",
        [](UObject* Object, const FString& FunctionNameOrSignature, sol::protected_function Callback)
        {
            return LuaBindReflectedEventOverride(Object, FunctionNameOrSignature, std::move(Callback));
        }
    );
    Reflection.set_function(
        "UnbindEvent",
        [](UObject* Object, const FString& FunctionNameOrSignature)
        {
            return LuaUnbindReflectedEventOverride(Object, FunctionNameOrSignature);
        }
    );
    Reflection.set_function(
        "HasEventBinding",
        [](UObject* Object, const FString& FunctionNameOrSignature)
        {
            return LuaHasReflectedEventOverride(Object, FunctionNameOrSignature);
        }
    );
    Reflection.set_function(
        "GetProperty",
        [](UObject* Object, const FString& PropertyName, sol::this_state State)
        {
            return LuaGetReflectedProperty(State, Object, PropertyName);
        }
    );
    Reflection.set_function(
        "SetProperty",
        [](UObject* Object, const FString& PropertyName, sol::object Value)
        {
            return LuaSetReflectedProperty(Object, PropertyName, Value);
        }
    );
    Reflection.set_function(
        "GetFunctions",
        [](UObject* Object, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            if (!IsValid(Object) || !Object->GetClass())
            {
                return Result;
            }
            TArray<const FFunction*> Functions;
            Object->GetClass()->GetFunctionRefs(Functions);
            int Index = 1;
            for (const FFunction* Function : Functions)
            {
                if (Function)
                {
                    Result[Index++] = LuaDescribeFunction(State, *Function);
                }
            }
            return Result;
        }
    );
    // LuaBlueprint Cast ?몃뱶??? IsA ?듦낵 ??媛숈? 媛앹껜 諛섑솚, ?ㅽ뙣 ??nil.
    // ?ㅼ젣 Unreal Blueprint Cast ? ?숈씪???섎? (?깃났/?ㅽ뙣 遺꾧린).
    Reflection.set_function(
        "Cast",
        [](UObject* Object, const FString& ClassName) -> UObject*
        {
            if (!IsValid(Object)) return nullptr;
            UClass* Target = UClass::FindByName(ClassName.c_str());
            if (!Target) return nullptr;
            UClass* Source = Object->GetClass();
            if (!Source) return nullptr;
            return Source->IsA(Target) ? Object : nullptr;
        }
    );
    Reflection.set_function(
        "GetStaticFunctions",
        [](const FString& ClassName, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            UClass*         Class  = UClass::FindByName(ClassName.c_str());
            if (!Class)
            {
                return Result;
            }
            TArray<const FFunction*> Functions;
            Class->GetFunctionRefs(Functions);
            int Index = 1;
            for (const FFunction* Function : Functions)
            {
                if (Function && Function->IsStatic())
                {
                    Result[Index++] = LuaDescribeFunction(State, *Function);
                }
            }
            return Result;
        }
    );
    Reflection.set_function(
        "GetProperties",
        [](UObject* Object, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            if (!IsValid(Object) || !Object->GetClass())
            {
                return Result;
            }
            TArray<const FProperty*> Properties;
            Object->GetClass()->GetPropertyRefs(Properties);
            int Index = 1;
            for (const FProperty* Property : Properties)
            {
                if (Property)
                {
                    Result[Index++] = LuaDescribeProperty(State, *Property);
                }
            }
            return Result;
        }
    );
}


