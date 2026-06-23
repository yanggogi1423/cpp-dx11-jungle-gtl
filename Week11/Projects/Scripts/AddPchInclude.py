from __future__ import annotations

import argparse
from pathlib import Path


PCH_INCLUDE = '#include "Pch.h"'
DEFAULT_TARGETS = (
    "JSEngine/main.cpp",
    "JSEngine/Pch.cpp",
    "JSEngine/Source",
)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig", errors="ignore")


def detect_newline(text: str) -> str:
    return "\r\n" if "\r\n" in text else "\n"


def has_pch_include(text: str) -> bool:
    return PCH_INCLUDE in text


def add_pch_include(text: str) -> str:
    newline = detect_newline(text)
    prefix = "\ufeff" if text.startswith("\ufeff") else ""
    body = text[1:] if prefix else text

    if body.startswith(PCH_INCLUDE):
        return text

    return f"{prefix}{PCH_INCLUDE}{newline}{body}"


def collect_cpp_files(root: Path, targets: list[str]) -> list[Path]:
    files: list[Path] = []
    seen: set[Path] = set()

    for target in targets:
        path = (root / target).resolve()
        if path.is_dir():
            candidates = sorted(path.rglob("*.cpp"))
        elif path.is_file() and path.suffix.lower() == ".cpp":
            candidates = [path]
        else:
            continue

        for candidate in candidates:
            parts = {part.lower() for part in candidate.parts}
            if (
                "thirdparty" in parts
                or "build" in parts
                or "bin" in parts
                or "intermediate" in parts
                or "generated" in parts
            ):
                continue

            resolved = candidate.resolve()
            if resolved not in seen:
                seen.add(resolved)
                files.append(resolved)

    return files


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Add #include "Pch.h" as the first line of project .cpp files.'
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="Repository root. Defaults to this script's parent repo.",
    )
    parser.add_argument(
        "--target",
        action="append",
        dest="targets",
        help="File or directory to scan. Can be passed multiple times.",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Write changes. Without this, only prints what would change.",
    )
    args = parser.parse_args()

    root = args.root.resolve()
    targets = args.targets or list(DEFAULT_TARGETS)
    cpp_files = collect_cpp_files(root, targets)

    changed: list[Path] = []
    skipped: list[Path] = []

    for path in cpp_files:
        text = read_text(path)
        if has_pch_include(text):
            skipped.append(path)
            continue

        changed.append(path)
        if args.apply:
            path.write_text(add_pch_include(text), encoding="utf-8", newline="")

    mode = "Updated" if args.apply else "Would update"
    print(f"{mode}: {len(changed)} file(s)")
    for path in changed:
        print(f"  {path.relative_to(root)}")

    print(f"Already had PCH include: {len(skipped)} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
