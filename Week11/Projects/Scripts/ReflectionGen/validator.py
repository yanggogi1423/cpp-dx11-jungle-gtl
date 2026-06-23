from .models import ReflectionData
from .type_system import normalize_type, is_pointer_type, pointee_type, is_array_type, is_map_type, function_storage_type

SUPPORTED_FUNCTION_SPECIFIERS = {
    "CallInEditor",
    "BlueprintCallable",
    "BlueprintPure",
    "BlueprintEvent",
}

def validate_reflection_data(data: ReflectionData) -> None:
    check_duplicate_classes(data.classes)
    check_duplicate_enums(data.enums)
    check_duplicate_functions(data)
    check_duplicate_function_parameters(data)
    check_manual_macros(data.classes)
    check_function_specifiers(data)
    check_function_defaults(data)
    check_supported_reflection_types(data)

def check_duplicate_classes(classes) -> None:
    seen = set()

    for cls in classes:
        if cls.class_name in seen:
            raise RuntimeError(f"Duplicate reflected class: {cls.class_name} ({cls.header})")
        seen.add(cls.class_name)


def check_duplicate_enums(enums):
    seen = set()

    for enum in enums:
        if enum.name in seen:
            raise RuntimeError(f"Duplicate reflected enum: {enum.name} ({enum.header})")
        seen.add(enum.name)


def check_manual_macros(classes):
    for cls in classes:
        cpp = cls.header.with_suffix(".cpp")

        if not cpp.exists():
            continue

        text = cpp.read_text(encoding="utf-8", errors="ignore")

        forbidden = [
            f"DEFINE_CLASS({cls.class_name},",
            f"DEFINE_ABSTRACT_CLASS({cls.class_name},",
            f"REGISTER_FACTORY({cls.class_name})"
        ]

        for token in forbidden:
            if token in text:
                raise RuntimeError(f"Manual reflection macro found for {cls.class_name} in {cpp}. Remove it to allow auto-generation.")


def check_supported_reflection_types(data: ReflectionData):
    class_names = {cls.class_name for cls in data.classes}
    enum_names = {enum.name for enum in data.enums}
    struct_names = {struct.name for struct in data.structs}

    for cls in data.classes:
        for func in cls.functions:
            return_type = normalize_type(func.return_type)

            if not is_supported_function_return_type(return_type, class_names, enum_names, struct_names):
                raise RuntimeError(f"Unsupported function return type: {cls.class_name}::{func.name} ({func.return_type})")

            for param in func.parameters:
                param_type = normalize_type(param.type_name)

                if not is_supported_function_parameter_type(param_type, class_names, enum_names, struct_names):
                    raise RuntimeError(f"Unsupported function parameter type: {cls.class_name}::{func.name}({param.type_name} {param.name})")

def is_supported_function_value_type(type_name: str, enum_names: set[str], struct_names: set[str]) -> bool:
    type_name = normalize_type(type_name)

    builtin_types = {
        "bool",
        "int",
        "int32",
        "uint32",
        "float",
        "double",
        "FString",
        "FName",
        "FVector",
        "FVector2",
        "FVector4",
        "FColor",
    }

    if type_name in builtin_types:
        return True

    if type_name in enum_names:
        return True

    if type_name in struct_names:
        return True

    return False

def is_supported_function_parameter_type(type_name: str, class_names: set[str], enum_names: set[str], struct_names: set[str]) -> bool:
    type_name = normalize_type(type_name)
    storage_type = function_storage_type(type_name)

    if is_array_type(storage_type) or is_map_type(storage_type):
        return False

    if "&" in type_name:
        if storage_type == type_name:
            return False
        
        return is_supported_function_value_type(storage_type, enum_names, struct_names)

    if is_pointer_type(type_name):
        pointee = pointee_type(type_name)
        return pointee in class_names or pointee == "UObject"

    return is_supported_function_value_type(type_name, enum_names, struct_names)

def is_supported_function_return_type(type_name: str, class_names: set[str], enum_names: set[str], struct_names: set[str]) -> bool:
    type_name = normalize_type(type_name)
    
    if type_name == "void":
        return True

    if is_array_type(type_name) or is_map_type(type_name):
        return False

    storage_type = function_storage_type(type_name)

    if "&" in type_name and storage_type == type_name:
        return False

    if is_pointer_type(type_name):
        pointee = pointee_type(type_name)
        return pointee in class_names
    
    return is_supported_function_value_type(type_name, enum_names, struct_names)

def check_function_specifiers(data: ReflectionData) -> None:
    for cls in data.classes:
        seen_functions = set()

        for func in cls.functions:
            for specifier in func.specifiers:
                if specifier not in SUPPORTED_FUNCTION_SPECIFIERS:
                    raise RuntimeError(f"Unsupported function specifier: {cls.class_name}::{func.name} - {specifier}")

            if "BlueprintCallable" in func.specifiers and "BlueprintPure" in func.specifiers:
                raise RuntimeError(f"Function cannot be both BlueprintCallable and BlueprintPure: {cls.class_name}::{func.name}")

            if "BlueprintPure" in func.specifiers and func.return_type == "void":
                raise RuntimeError(f"BlueprintPure function cannot have void return type: {cls.class_name}::{func.name}")

            if "CallInEditor" in func.specifiers and func.parameters:
                raise RuntimeError(f"CallInEditor function cannot have parameters: {cls.class_name}::{func.name}")

            if "BlueprintEvent" in func.specifiers:
                if func.return_type != "void":
                    raise RuntimeError(f"BlueprintEvent function must have void return type: {cls.class_name}::{func.name}")

                for exclusive in ("BlueprintCallable", "BlueprintPure", "CallInEditor"):
                    if exclusive in func.specifiers:
                        raise RuntimeError(f"BlueprintEvent function cannot also be {exclusive}: {cls.class_name}::{func.name}")

def check_duplicate_functions(data: ReflectionData) -> None:
    for cls in data.classes:
        seen = set()

        for func in cls.functions:
            if func.name in seen:
                raise RuntimeError(f"Duplicate function name in class {cls.class_name}: {func.name} ({func.source_file}:{func.line})")

            seen.add(func.name)

            seen_params = set()
            for param in func.parameters:
                if param.name in seen_params:
                    raise RuntimeError(f"Duplicate parameter name in function {cls.class_name}::{func.name}: {param.name} ({func.source_file}:{func.line})")
                seen_params.add(param.name)

def check_duplicate_function_parameters(data: ReflectionData) -> None:
    for cls in data.classes:
        for func in cls.functions:
            seen = set()

            for param in func.parameters:
                if param.name in seen:
                    raise RuntimeError(
                        f"Duplicate UFUNCTION parameter name: "
                        f"{cls.class_name}::{func.name}::{param.name} "
                        f"at {func.source_file}:{func.line}"
                    )
                seen.add(param.name)

def check_function_defaults(data: ReflectionData) -> None:
    for cls in data.classes:
        for func in cls.functions:
            for param in func.parameters:
                if param.default is not None:
                    raise RuntimeError(
                        f"UFUNCTION default arguments are not supported yet: "
                        f"{cls.class_name}::{func.name}::{param.name} "
                        f"at {func.source_file}:{func.line}"
                    )
