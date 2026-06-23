from dataclasses import dataclass, field
from pathlib import Path

@dataclass
class ReflectedProperty:
    name: str
    type_name: str
    default: str | None
    specifiers: list[str]
    metadata: dict[str, str]
    source_file: Path
    line: int
    
@dataclass
class ReflectedParameter:
    name: str
    type_name: str
    default: str | None = None

@dataclass
class ReflectedFunction:
    name: str
    return_type: str
    specifiers: list[str]
    metadata: dict[str, str]
    source_file: Path
    line: int
    parameters: list[ReflectedParameter] = field(default_factory=list)
    is_const: bool = False

@dataclass
class ReflectedClass:
    class_name: str
    super_class: str
    include: str
    header: Path
    properties: list[ReflectedProperty] = field(default_factory=list)
    functions: list[ReflectedFunction] = field(default_factory=list)
    specifiers: list[str] = field(default_factory=list)
    metadata: dict[str, str] = field(default_factory=dict)

@dataclass
class ReflectedEnumValue:
    name: str
    value: int

@dataclass
class ReflectedEnum:
    name: str
    underlying: str
    include: str
    header: Path
    values: list[ReflectedEnumValue]
    specifiers: list[str] = field(default_factory=list)
    metadata: dict[str, str] = field(default_factory=dict)

@dataclass
class ReflectedStruct:
    name: str
    include: str
    header: Path
    properties: list[ReflectedProperty] = field(default_factory=list)
    specifiers: list[str] = field(default_factory=list)
    metadata: dict[str, str] = field(default_factory=dict)

@dataclass
class ReflectionData:
    classes: list[ReflectedClass]
    structs: list[ReflectedStruct]
    enums: list[ReflectedEnum]
