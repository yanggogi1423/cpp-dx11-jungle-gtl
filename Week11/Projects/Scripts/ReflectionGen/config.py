from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIR = ROOT / "JSEngine" / "Source"
OUTPUT_DIR = ROOT / "JSEngine" / "Source" / "Generated"
OUTPUT_FILE = OUTPUT_DIR / "GeneratedReflection.cpp"
