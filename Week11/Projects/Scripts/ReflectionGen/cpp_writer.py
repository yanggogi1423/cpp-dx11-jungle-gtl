from .models import ReflectionData
from .type_system import (
    normalize_type,
    is_pointer_type,
    pointee_type,
    is_array_type,
    array_inner_type,
    is_map_type,
    map_key_value_types,
    function_storage_type,
)


def cpp_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def cpp_float(value: str) -> str:
    return f"{float(value):.9g}f"


def property_flags(specifiers: list[str]) -> str:
    flags = []

    if "EditAnywhere" in specifiers:
        flags.append("static_cast<uint32>(EPropertyFlags::EditAnywhere)")
    if "VisibleAnywhere" in specifiers:
        flags.append("static_cast<uint32>(EPropertyFlags::VisibleAnywhere)")
    if "Transient" in specifiers:
        flags.append("static_cast<uint32>(EPropertyFlags::Transient)")

    return " | ".join(flags) if flags else "static_cast<uint32>(EPropertyFlags::None)"

def function_flags(specifiers: list[str]) -> str:
    flags = []

    if "CallInEditor" in specifiers:
        flags.append("static_cast<uint32>(EFunctionFlags::CallInEditor)")
    if "BlueprintCallable" in specifiers:
        flags.append("static_cast<uint32>(EFunctionFlags::BlueprintCallable)")
    if "BlueprintPure" in specifiers:
        flags.append("static_cast<uint32>(EFunctionFlags::BlueprintPure)")
    if "BlueprintEvent" in specifiers:
        flags.append("static_cast<uint32>(EFunctionFlags::BlueprintEvent)")

    return " | ".join(flags) if flags else "static_cast<uint32>(EFunctionFlags::None)"


def property_class(type_name: str, enum_names: set[str], struct_names: set[str]) -> str | None:
    type_name = normalize_type(type_name)

    mapping = {
        "bool": "FBoolProperty",
        "int": "FIntProperty",
        "int32": "FIntProperty",
        "uint32": "FUIntProperty",
        "float": "FFloatProperty",
        "double": "FDoubleProperty",
        "FString": "FStringProperty",
        "FName": "FNameProperty",
        "FVector": "FVectorProperty",
        "FVector2": "FVector2Property",
        "FVector4": "FVector4Property",
        "FColor": "FColorProperty",
    }

    if type_name in mapping:
        return mapping[type_name]

    if type_name in enum_names:
        return "FEnumProperty"

    if type_name in struct_names:
        return "FStructProperty"

    if is_array_type(type_name):
        return "FArrayProperty"

    if is_map_type(type_name):
        return "FMapProperty"

    if is_pointer_type(type_name):
        return "FObjectProperty"

    return None


def append_type_specific_property_code(
    lines: list[str],
    var_name: str,
    prop_class: str,
    type_name: str,
) -> None:
    if prop_class == "FEnumProperty":
        lines.append(f'    {var_name}->EnumName = "{cpp_string(type_name)}";')

    elif prop_class == "FStructProperty":
        lines.append(f'    {var_name}->StructName = "{cpp_string(type_name)}";')
        lines.append(f"    {var_name}->Struct = {type_name}::StaticStruct();")

    elif prop_class == "FObjectProperty":
        object_class = pointee_type(type_name)
        lines.append(f"    {var_name}->PropertyClass = {object_class}::StaticClass();")
        lines.append(f'    {var_name}->ClassName = "{cpp_string(object_class)}";')


def generate_property_code(
    owner_name: str,
    add_target_name: str,
    prop,
    enum_names: set[str],
    struct_names: set[str],
) -> str:
    type_name = normalize_type(prop.type_name)
    prop_class = property_class(type_name, enum_names, struct_names)

    if not prop_class:
        raise ValueError(f"Unsupported property type: {owner_name}::{prop.name} ({prop.type_name})")

    var_name = f"Prop_{prop.name}"
    display_name = prop.metadata.get("DisplayName", prop.name)
    category = prop.metadata.get("Category", "")
    clamp_min = prop.metadata.get("ClampMin")
    clamp_max = prop.metadata.get("ClampMax")
    delta = prop.metadata.get("Delta")

    new_expr = prop_class
    if prop_class == "FArrayProperty":
        inner_type = array_inner_type(type_name)
        new_expr = f"TArrayProperty<{inner_type}>"
    elif prop_class == "FMapProperty":
        key_type, value_type = map_key_value_types(type_name)
        new_expr = f"TMapProperty<{key_type}, {value_type}>"

    lines = [
        f"    auto* {var_name} = new {new_expr}();",
        f'    {var_name}->Name = "{cpp_string(prop.name)}";',
        f'    {var_name}->DisplayName = "{cpp_string(display_name)}";',
        f'    {var_name}->TypeName = "{cpp_string(type_name)}";',
        f'    {var_name}->Category = "{cpp_string(category)}";',
    ]

    if clamp_min is not None:
        lines.append(f"    {var_name}->ClampMin = {cpp_float(clamp_min)};")
        lines.append(f"    {var_name}->bHasClampMin = true;")

    if clamp_max is not None:
        lines.append(f"    {var_name}->ClampMax = {cpp_float(clamp_max)};")
        lines.append(f"    {var_name}->bHasClampMax = true;")

    if delta is not None:
        lines.append(f"    {var_name}->Delta = {cpp_float(delta)};")

    append_type_specific_property_code(lines, var_name, prop_class, type_name)

    if prop_class == "FArrayProperty":
        inner_type = array_inner_type(type_name)
        inner_class = property_class(inner_type, enum_names, struct_names)
        inner_var_name = f"Inner_{prop.name}"

        if not inner_class:
            raise ValueError(f"Unsupported array inner type: {owner_name}::{prop.name} ({inner_type})")

        lines.append(f"    auto* {inner_var_name} = new {inner_class}();")
        lines.append(f'    {inner_var_name}->Name = "{cpp_string(prop.name)}_Inner";')
        lines.append(f'    {inner_var_name}->DisplayName = "Element";')
        lines.append(f'    {inner_var_name}->TypeName = "{cpp_string(inner_type)}";')
        lines.append(f"    {inner_var_name}->Size = sizeof({inner_type});")

        append_type_specific_property_code(lines, inner_var_name, inner_class, inner_type)

        lines.append(f"    {var_name}->Inner = {inner_var_name};")
        lines.append("")

    if prop_class == "FMapProperty":
        key_type, value_type = map_key_value_types(type_name)
        key_class = property_class(key_type, enum_names, struct_names)
        value_class = property_class(value_type, enum_names, struct_names)

        if not key_class or not value_class:
            raise ValueError(f"Unsupported map key/value type: {owner_name}::{prop.name} ({type_name})")

        key_var = f"Key_{prop.name}"
        value_var = f"Value_{prop.name}"

        lines.append(f"    auto* {key_var} = new {key_class}();")
        lines.append(f'    {key_var}->Name = "{cpp_string(prop.name)}_Key";')
        lines.append(f'    {key_var}->DisplayName = "Key";')
        lines.append(f'    {key_var}->TypeName = "{cpp_string(key_type)}";')
        lines.append(f"    {key_var}->Size = sizeof({key_type});")
        append_type_specific_property_code(lines, key_var, key_class, key_type)

        lines.append(f"    auto* {value_var} = new {value_class}();")
        lines.append(f'    {value_var}->Name = "{cpp_string(prop.name)}_Value";')
        lines.append(f'    {value_var}->DisplayName = "Value";')
        lines.append(f'    {value_var}->TypeName = "{cpp_string(value_type)}";')
        lines.append(f"    {value_var}->Size = sizeof({value_type});")
        append_type_specific_property_code(lines, value_var, value_class, value_type)

        lines.append(f"    {var_name}->KeyProp = {key_var};")
        lines.append(f"    {var_name}->ValueProp = {value_var};")
        lines.append("")

    lines.extend(
        [
            f"    {var_name}->Offset = offsetof({owner_name}, {prop.name});",
            f"    {var_name}->Size = sizeof({type_name});",
            f"    {var_name}->Flags = {property_flags(prop.specifiers)};",
            f"    {add_target_name}->AddProperty({var_name});",
        ]
    )

    return "\n".join(lines) + "\n"


def generate_static_class_code(cls) -> str:
    class_name = cls.class_name
    super_class = cls.super_class
    is_abstract = "Abstract" in cls.specifiers

    constructor = "nullptr" if is_abstract else f"[]() -> UObject* {{ return new {class_name}(); }}"

    class_flags = []
    if is_abstract:
        class_flags.append("static_cast<uint32>(EClassFlags::Abstract)")

    flags_expr = " | ".join(class_flags) if class_flags else "static_cast<uint32>(EClassFlags::None)"

    return f"""\
UClass* {class_name}::StaticClass()
{{
    static UClass Class(
        "{class_name}",
        {super_class}::StaticClass(),
        sizeof({class_name}),
        {constructor}
    );

    Class.ClassFlags = {flags_expr};

    static bool bPropertiesRegistered = false;
    if (!bPropertiesRegistered)
    {{
        bPropertiesRegistered = true;
        {class_name}::RegisterProperties(&Class);
        {class_name}::RegisterFunctions(&Class);
    }}

    return &Class;
}}
"""


def generate_register_class_properties_code(cls, enum_names: set[str], struct_names: set[str]) -> str:
    class_name = cls.class_name

    lines = [
        f"void {class_name}::RegisterProperties(UClass* Class)",
        "{",
        "",
    ]

    for prop in cls.properties:
        lines.append(generate_property_code(class_name, "Class", prop, enum_names, struct_names))

    lines.append("}")
    lines.append("")
    return "\n".join(lines)

def generate_parameter_property_code(
    func_var_name: str,
    owner_name: str,
    func_name: str,
    param,
    enum_names: set[str],
    struct_names: set[str],
) -> str:
    type_name = function_storage_type(param.type_name)
    prop_class = property_class(type_name, enum_names, struct_names)

    if not prop_class:
        raise ValueError(f"Unsupported parameter type: {owner_name}::{func_name}::{param.name} ({param.type_name})")

    var_name = f"Param_{func_name}_{param.name}"

    lines = [
        f"    auto* {var_name} = new {prop_class}();",
        f'    {var_name}->Name = "{cpp_string(param.name)}";',
        f'    {var_name}->DisplayName = "{cpp_string(param.name)}";',
        f'    {var_name}->TypeName = "{cpp_string(type_name)}";',
        f"    {var_name}->Size = sizeof({type_name});",
    ]

    append_type_specific_property_code(lines, var_name, prop_class, type_name)

    param_info_var_name = f"ParamInfo_{func_name}_{param.name}"

    lines.append(f"    auto* {param_info_var_name} = new FFunctionParameter();")
    lines.append(f"    {param_info_var_name}->Property = {var_name};")
    lines.append(f"    {param_info_var_name}->Flags = static_cast<uint32>(EFunctionParameterFlags::Input);")
    lines.append(f"    {func_var_name}->Parameters.push_back({param_info_var_name});")

    return "\n".join(lines)

def generate_return_property_code(
    func_var_name: str,
    owner_name: str,
    func_name: str,
    return_type: str,
    enum_names: set[str],
    struct_names: set[str],
) -> str:
    type_name = normalize_type(return_type)

    if type_name == "void":
        return ""

    prop_class = property_class(type_name, enum_names, struct_names)
    if not prop_class:
        raise ValueError(f"Unsupported return type: {owner_name}::{func_name} ({return_type})")

    var_name = f"Return_{func_name}"
    return_info_var_name = f"ReturnInfo_{func_name}"

    lines = [
        f"    auto* {var_name} = new {prop_class}();",
        f'    {var_name}->Name = "ReturnValue";',
        f'    {var_name}->DisplayName = "Return Value";',
        f'    {var_name}->TypeName = "{cpp_string(type_name)}";',
        f"    {var_name}->Size = sizeof({type_name});",
    ]

    append_type_specific_property_code(lines, var_name, prop_class, type_name)

    lines.append(f"    auto* {return_info_var_name} = new FFunctionParameter();")
    lines.append(f"    {return_info_var_name}->Property = {var_name};")
    lines.append(f"    {return_info_var_name}->Flags = static_cast<uint32>(EFunctionParameterFlags::Return);")
    lines.append(f"    {func_var_name}->ReturnParameter = {return_info_var_name};")

    return "\n".join(lines)

def generate_function_code(
    class_name: str,
    func,
    enum_names: set[str],
    struct_names: set[str]
) -> str:
    var_name = f"Func_{func.name}"
    display_name = func.metadata.get("DisplayName", func.name)
    category = func.metadata.get("Category", "")

    return_type = normalize_type(func.return_type)
    arg_types = [normalize_type(param.type_name) for param in func.parameters]
    template_args = ", ".join([class_name, return_type] + arg_types)

    function_class = "TConstFunction" if func.is_const else "TFunction"

    lines = [
        f"    auto* {var_name} = new {function_class}<{template_args}>();",
        f'    {var_name}->Name = "{cpp_string(func.name)}";',
        f'    {var_name}->DisplayName = "{cpp_string(display_name)}";',
        f'    {var_name}->Category = "{cpp_string(category)}";',
        f"    {var_name}->Flags = {function_flags(func.specifiers)};",
        f"    {var_name}->Method = &{class_name}::{func.name};",
    ]

    for param in func.parameters:
        lines.append(
            generate_parameter_property_code(
                var_name,
                class_name,
                func.name,
                param,
                enum_names,
                struct_names,
            )
        )

    return_code = generate_return_property_code(
        var_name,
        class_name,
        func.name,
        return_type,
        enum_names,
        struct_names
    )

    if return_code:
        lines.append(return_code)
    
    lines.append(f"    Class->AddFunction({var_name});")
    return "\n".join(lines) + "\n"

def generate_register_class_functions_code(
    cls,
    enum_names: set[str],
    struct_names: set[str]
) -> str:
    class_name = cls.class_name

    lines = [
        f"void {class_name}::RegisterFunctions(UClass* Class)",
        "{",
        "",
    ]

    for func in cls.functions:
        lines.append(generate_function_code(class_name, func, enum_names, struct_names))

    lines.append("}")
    lines.append("")
    return "\n".join(lines)

def generate_factory_registration_code(cls) -> str:
    class_name = cls.class_name
    is_abstract = "Abstract" in cls.specifiers

    if is_abstract:
        return ""

    return f"""\
namespace
{{
    struct {class_name}_RegisterFactory
    {{
        {class_name}_RegisterFactory()
        {{
            FObjectFactory::Get().Register(
                "{class_name}",
                {class_name}::StaticClass()
            );
        }}
    }};

    {class_name}_RegisterFactory G{class_name}_RegisterFactory;
}}
"""


def generate_class_code(cls, enum_names: set[str], struct_names: set[str]) -> str:
    return (
        generate_static_class_code(cls)
        + "\n"
        + generate_register_class_properties_code(cls, enum_names, struct_names)
        + "\n"
        + generate_register_class_functions_code(cls, enum_names, struct_names)
        + "\n"
        + generate_factory_registration_code(cls)
    )


def generate_static_struct_code(struct) -> str:
    struct_name = struct.name

    return f"""\
UScriptStruct* {struct_name}::StaticStruct()
{{
    static UScriptStruct Struct;
    Struct.Name = "{cpp_string(struct_name)}";
    Struct.Size = sizeof({struct_name});

    static bool bPropertiesRegistered = false;
    if (!bPropertiesRegistered)
    {{
        bPropertiesRegistered = true;
        {struct_name}::RegisterProperties(&Struct);
        FStructRegistry::Get().RegisterStruct(&Struct);
    }}

    return &Struct;
}}
"""


def generate_register_struct_properties_code(struct, enum_names: set[str], struct_names: set[str]) -> str:
    struct_name = struct.name

    lines = [
        f"void {struct_name}::RegisterProperties(UScriptStruct* Struct)",
        "{",
        "",
    ]

    for prop in struct.properties:
        lines.append(generate_property_code(struct_name, "Struct", prop, enum_names, struct_names))

    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def generate_struct_registration_code(struct) -> str:
    struct_name = struct.name

    return f"""\
namespace
{{
    struct {struct_name}_RegisterStruct
    {{
        {struct_name}_RegisterStruct()
        {{
            {struct_name}::StaticStruct();
        }}
    }};

    {struct_name}_RegisterStruct G{struct_name}_RegisterStruct;
}}
"""


def generate_struct_code(struct, enum_names: set[str], struct_names: set[str]) -> str:
    return (
        generate_static_struct_code(struct)
        + "\n"
        + generate_register_struct_properties_code(struct, enum_names, struct_names)
        + "\n"
        + generate_struct_registration_code(struct)
    )


def generate_enum_registration_code(enum) -> str:
    enum_name = enum.name

    lines = [
        "namespace",
        "{",
        f"    struct {enum_name}_RegisterEnum",
        "    {",
        f"        {enum_name}_RegisterEnum()",
        "        {",
        "            UEnum* Enum = new UEnum();",
        f'            Enum->Name = "{cpp_string(enum_name)}";',
    ]

    for value in enum.values:
        value_name = value.name
        lines.append(
            f'            Enum->Values.push_back({{ "{cpp_string(value_name)}", static_cast<int64>({enum_name}::{value_name}) }});'
        )

    lines += [
        "            FEnumRegistry::Get().RegisterEnum(Enum);",
        "        }",
        "    };",
        "",
        f"    {enum_name}_RegisterEnum G{enum_name}_RegisterEnum;",
        "}",
        "",
    ]

    return "\n".join(lines)


def generate_file(data: ReflectionData) -> str:
    includes = []
    enum_names = {enum.name for enum in data.enums}
    struct_names = {struct.name for struct in data.structs}

    for enum in data.enums:
        includes.append(f'#include "{enum.include}"')

    for struct in data.structs:
        includes.append(f'#include "{struct.include}"')

    for cls in data.classes:
        includes.append(f'#include "{cls.include}"')

    struct_codes = [
        generate_struct_code(struct, enum_names, struct_names)
        for struct in data.structs
    ]

    class_codes = [
        generate_class_code(cls, enum_names, struct_names)
        for cls in data.classes
    ]

    include_block = "\n".join(sorted(set(includes)))
    enum_code_block = "\n".join(generate_enum_registration_code(enum) for enum in data.enums)
    struct_code_block = "\n".join(struct_codes)
    class_code_block = "\n".join(class_codes)

    return f"""// Auto-generated by GenerateReflection.py. Do not edit manually.

{include_block}
#include "Object/Class.h"
#include "Object/Enum.h"
#include "Object/EnumRegistry.h"
#include "Object/ObjectFactory.h"
#include "Object/Property.h"
#include "Object/Function.h"
#include "Object/ScriptStruct.h"
#include "Object/StructRegistry.h"

{enum_code_block}
{struct_code_block}
{class_code_block}
"""
