from pathlib import Path
from .config import SOURCE_DIR
from .parser import parse_header_file
from .validator import check_manual_macros

def collect_header_files() -> list[Path]:
    return [
        path for path in SOURCE_DIR.rglob("*.h")
        if "Generated" not in path.parts
    ]
