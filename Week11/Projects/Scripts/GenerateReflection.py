from pathlib import Path
import sys

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from ReflectionGen.pipeline import build_reflection_data
from ReflectionGen.cpp_writer import generate_file
from ReflectionGen.config import OUTPUT_DIR, OUTPUT_FILE

def main():
    data = build_reflection_data()
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(generate_file(data), encoding="utf-8", newline="\n")

    print(f"Generated {len(data.classes)} reflected classes:")
    for cls in data.classes:
        print(f"  {cls.class_name} <- {cls.include}")

if __name__ == "__main__":
    main()