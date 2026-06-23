"""
스크립트 파일이 위치한 폴더 기준으로,
지정된 하위 폴더들 안의 모든 PNG/TIF/TIFF 파일을
가로 세로 1/2 크기로 줄이고 같은 파일명으로 덮어씁니다.

사용법:
    resize_half.py 를 Auto 폴더에 넣고 실행:
    python resize_half.py
"""

from pathlib import Path
from PIL import Image

# ── 처리할 하위 폴더 목록 ──────────────────────────────────────────────────
TARGET_FOLDERS = [
    "BLD_Bridge_A"
]

TARGET_EXTENSIONS = {".png", ".tif", ".tiff"}

# ─────────────────────────────────────────────────────────────────────────────

def resize_half(path: Path) -> None:
    with Image.open(path) as img:
        original_size = img.size
        new_size = (img.width // 2, img.height // 2)
        resized = img.resize(new_size, Image.LANCZOS)
        resized.save(path)
    print(f"  [완료] {path.name}  {original_size[0]}x{original_size[1]} → {new_size[0]}x{new_size[1]}")


def main():
    # 이 스크립트가 있는 폴더 (Auto/)
    base_dir = Path(__file__).parent

    total = 0
    skipped = 0

    for folder_name in TARGET_FOLDERS:
        folder = base_dir / folder_name

        if not folder.exists():
            print(f"[없음] 폴더를 찾을 수 없습니다: {folder_name}")
            continue

        files = [f for f in folder.iterdir() if f.is_file() and f.suffix.lower() in TARGET_EXTENSIONS]

        if not files:
            print(f"[빈 폴더] 처리할 파일 없음: {folder_name}")
            skipped += 1
            continue

        print(f"\n[폴더] {folder_name}  ({len(files)}개 파일)")
        for file in sorted(files):
            try:
                resize_half(file)
                total += 1
            except Exception as e:
                print(f"  [오류] {file.name}: {e}")

    print(f"\n────────────────────────────────")
    print(f"처리 완료: {total}개 파일 / 폴더 없음·빈 폴더: {skipped}개")


if __name__ == "__main__":
    main()
