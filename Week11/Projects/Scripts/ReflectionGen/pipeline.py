from .models import ReflectedClass, ReflectedEnum, ReflectionData
from .parser import parse_header_file
from .collector import collect_header_files
from .validator import validate_reflection_data

def build_reflection_data() -> ReflectionData:
    classes = []
    structs = []
    enums = []

    for header in collect_header_files():
        parsed_classes, parsed_structs, parsed_enums = parse_header_file(header)
        classes.extend(parsed_classes)
        structs.extend(parsed_structs)
        enums.extend(parsed_enums)

    data = ReflectionData(classes=classes, structs=structs, enums=enums)
    validate_reflection_data(data)
    return data