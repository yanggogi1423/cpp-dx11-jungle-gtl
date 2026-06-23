from pathlib import Path
from .config import SOURCE_DIR
from .models import ReflectedClass, ReflectedEnum, ReflectedProperty, ReflectedEnumValue, ReflectedStruct, ReflectedFunction, ReflectedParameter
from .macro_args import parse_macro_args
import re

def parse_header_file(path: Path) -> tuple[list[ReflectedClass], list[ReflectedStruct], list[ReflectedEnum]]:
    source = path.read_text(encoding="utf-8", errors="ignore")
    include = str(path.relative_to(SOURCE_DIR)).replace("\\", "/")

    enums = parse_enums(source, path, include)
    structs = parse_structs(source, path, include)
    classes = parse_classes(source, path, include)
    return classes, structs, enums

def parse_properties(body: str, source: str, header: Path) -> list[ReflectedProperty]:
    property_pattern = re.compile(
        r'UPROPERTY\s*\((?P<args>.*?)\)\s*'
        r'(?P<type>[A-Za-z_][\w:<>,\s\*&]*?)\s+'
        r'(?P<name>[A-Za-z_]\w*)'
        r'\s*(?P<default>(?:=\s*[^;]+|\{[^;]*\}))?\s*;',
        re.DOTALL
    )

    properties: list[ReflectedProperty] = []

    for m in property_pattern.finditer(body):
        specifiers, metadata = parse_macro_args(m.group("args"))

        default_text = m.group("default")
        if default_text:
            default_text = default_text.strip()
            if default_text.startswith("="):
                default_text = default_text[1:].strip()

        properties.append(
            ReflectedProperty(
                name=m.group("name"),
                type_name=" ".join(m.group("type").split()),
                default=default_text,
                specifiers=specifiers,
                metadata=metadata,
                source_file=header,
                line=source.count("\n", 0, m.start()) + 1,
            )
        )
    
    return properties

def split_params(params_text: str) -> list[str]:
    result = []
    depth = 0
    start = 0

    for i, ch in enumerate(params_text):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == "," and depth == 0:
            result.append(params_text[start:i].strip())
            start = i + 1
    
    result.append(params_text[start:].strip())
    return result

def split_type_and_name(param: str) -> tuple[str, str]:
    m = re.match(r'(?P<type>.+?)\s+(?P<name>[A-Za-z_]\w*)$', param.strip())
    if not m:
        raise RuntimeError(f"Invalid UFUNCTION parameter: {param}")

    return m.group("type").strip(), m.group("name").strip()

def parse_function_parameters(params_text: str) -> list[ReflectedParameter]:
    params_text = params_text.strip()
    if not params_text or params_text == "void":
        return []

    params = []
    for raw_params in split_params(params_text):
        param = raw_params.strip()
        if not param:
            continue

        default = None
        if "=" in param:
            param, default = param.split("=", 1)
            param = param.strip()
            default = default.strip()

        type_name, name = split_type_and_name(param)

        params.append(
            ReflectedParameter(
                name=name,
                type_name=" ".join(type_name.split()),
                default=default
            )
        )
    
    return params

def parse_functions(body: str, source: str, header: Path) -> list[ReflectedFunction]:
    function_pattern = re.compile(
        r'UFUNCTION\s*\((?P<args>.*?)\)\s*'
        r'(?P<return_type>[A-Za-z_][\w:<>,\s\*&]*?)\s+'
        r'(?P<name>[A-Za-z_]\w*)\s*'
        r'\((?P<params>.*?)\)\s*(?P<const>const\s*)?(?:(?:override|final)\s*)*;',
        re.DOTALL,
    )

    functions: list[ReflectedFunction] = []

    for m in function_pattern.finditer(body):
        specifiers, metadata = parse_macro_args(m.group("args"))

        functions.append(
            ReflectedFunction(
                name=m.group("name"),
                return_type=" ".join(m.group("return_type").split()),
                specifiers=specifiers,
                metadata=metadata,
                source_file=header,
                line=source.count("\n", 0, m.start()) + 1,
                parameters=parse_function_parameters(m.group("params")),
                is_const=bool(m.group("const")),
            )
        )
    
    return functions

def parse_classes(source, header, include):
    class_pattern = re.compile(
        r'UCLASS\s*\((?P<class_args>.*?)\)\s*'
        r'class\s+(?P<class_name>\w+)\s*:\s*public\s+(?P<super_class>\w+)\s*'
        r'\{(?P<body>.*?)^\s*\};',
        re.DOTALL | re.MULTILINE
    )

    classes = []
    for m in class_pattern.finditer(source):
        class_specifiers, class_metadata = parse_macro_args(m.group("class_args"))
        body = m.group("body")

        classes.append(ReflectedClass(
            class_name=m.group("class_name"),
            super_class=m.group("super_class"),
            include=include,
            header=header,
            properties=parse_properties(body, source, header),
            functions=parse_functions(body, source, header),
            specifiers=class_specifiers,
            metadata=class_metadata,
        ))

    return classes

def parse_structs(source: str, header: Path, include: str) -> list[ReflectedStruct]:
    struct_pattern = re.compile(
        r'USTRUCT\s*\((?P<args>.*?)\)\s*'
        r'struct\s+(?P<struct_name>\w+)\s*'
        r'\{(?P<body>.*?)^\s*\};',
        re.DOTALL | re.MULTILINE
    )

    structs: list[ReflectedStruct] = []

    for m in struct_pattern.finditer(source):
        specifiers, metadata = parse_macro_args(m.group("args"))
        body = m.group("body")

        structs.append(
            ReflectedStruct(
                name=m.group("struct_name"),
                include=include,
                header=header,
                properties=parse_properties(body, source, header),
                specifiers=specifiers,
                metadata=metadata,
            )
        )

    return structs

def parse_header(source: str):
    class_match = re.search(
        r'UCLASS\s*\((?P<class_args>[^)]*)\)\s*'
        r'class\s+(?P<class_name>\w+)\s*:\s*public\s+(?P<super_class>\w+)\s*'
        r'\{(?P<body>.*?)^\s*\};',
        source,
        re.DOTALL | re.MULTILINE
    )

    if not class_match:
        return None

    class_specifiers, class_metadata = parse_macro_args(class_match.group("class_args"))

    class_name = class_match.group("class_name")
    super_class = class_match.group("super_class")
    body = class_match.group("body")

    return {
        "class_name": class_name,
        "super_class": super_class,
        "class_specifiers": class_specifiers,
        "class_metadata": class_metadata,
        "body": body,
    }

def parse_enums(source: str, header: Path, include: str) -> list[ReflectedEnum]:
    enum_pattern = re.compile(
        r'UENUM\s*\((?P<args>.*?)\)\s*'
        r'enum\s+class\s+(?P<enum_name>\w+)'
        r'\s*(?::\s*(?P<underlying>[A-Za-z_]\w*))?\s*'
        r'\{(?P<body>.*?)\};',
        re.DOTALL
    )

    enums = []

    for m in enum_pattern.finditer(source):
        specifiers, metadata = parse_macro_args(m.group("args"))

        values = []
        for value in parse_enum_values(m.group("body")):
            values.append(ReflectedEnumValue(name=value["name"], value=value["value"]))

        enums.append(
            ReflectedEnum(
                name=m.group("enum_name"),
                underlying=m.group("underlying") or "int32",
                include=include,
                header=header,
                values=values,
                specifiers=specifiers,
                metadata=metadata
            )
        )

    return enums

def parse_enum_values(body: str):
    values = []
    current = 0

    for raw in body.split(","):
        token = raw.strip()
        if not token:
            continue

        token = token.split("//")[0].strip()
        if not token:
            continue

        if "=" in token:
            name, value = token.split("=", 1)
            name = name.strip()
            current = int(value.strip(), 0)
        else:
            name = token

        values.append({"name": name, "value": current})
        current += 1
    
    return values
