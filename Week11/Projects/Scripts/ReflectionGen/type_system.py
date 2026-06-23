def normalize_type(type_name: str) -> str:
    type_name = " ".join(type_name.split())
    type_name = type_name.replace(" *", "*")
    type_name = type_name.replace("* ", "*")
    type_name = type_name.replace(" &", "&")
    type_name = type_name.replace("& ", "&")
    return type_name

def is_pointer_type(type_name: str) -> bool:
    return normalize_type(type_name).endswith("*")

def pointee_type(type_name: str) -> str:
    normalized = normalize_type(type_name)
    if not normalized.endswith("*"):
        return ""
    return normalized[:-1].strip()

def is_array_type(type_name: str) -> bool:
    type_name = normalize_type(type_name)
    return type_name.startswith("TArray<") and type_name.endswith(">")

def array_inner_type(type_name: str) -> str:
    type_name = normalize_type(type_name)
    if not is_array_type(type_name):
        return ""
    return normalize_type(type_name[len("TArray<"):-1])

def is_map_type(type_name: str) -> bool:
    type_name = normalize_type(type_name)
    return type_name.startswith("TMap<") and type_name.endswith(">")

def split_template_args(arg_text: str) -> list[str]:
    args = []
    depth = 0
    start = 0

    for i, ch in enumerate(arg_text):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == "," and depth == 0:
            args.append(normalize_type(arg_text[start:i]))
            start = i + 1

    args.append(normalize_type(arg_text[start:]))
    return args

def map_key_value_types(type_name: str) -> tuple[str, str]:
    type_name = normalize_type(type_name)
    inner = type_name[len("TMap<"):-1]
    args = split_template_args(inner)
    if len(args) != 2:
        return "", ""
    return args[0], args[1]

def function_storage_type(type_name: str) -> str:
    type_name = normalize_type(type_name)

    if type_name.startswith("const ") and type_name.endswith("&"):
        return normalize_type(type_name[len("const "):-1])

    return type_name
