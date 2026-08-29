#!/usr/bin/env python3
"""Apply SafeLink Test 6B to the established P4 laboratory project."""

import argparse
import hashlib
from pathlib import Path
import shutil


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True, type=Path)
    args = parser.parse_args()
    project = args.project.resolve()
    main_dir = project / "main"
    target = main_dir / "transport_config_main.c"
    cmake = main_dir / "CMakeLists.txt"
    source = Path(__file__).with_name("transport_config_main.test6b.c")
    for path in (target, cmake, source):
        if not path.is_file():
            raise FileNotFoundError(path)

    backup = target.with_suffix(target.suffix + ".safelink-test6a-backup")
    if not backup.exists():
        shutil.copy2(target, backup)
    shutil.copy2(source, target)

    cmake_backup = cmake.with_suffix(cmake.suffix + ".safelink-test6a-backup")
    if not cmake_backup.exists():
        shutil.copy2(cmake, cmake_backup)
    text = cmake.read_text(encoding="utf-8")
    marker = "PRIV_REQUIRES"
    if marker not in text:
        raise RuntimeError("PRIV_REQUIRES not found in main/CMakeLists.txt")
    required = ("fatfs", "esp_driver_sdmmc", "sdmmc", "esp_wifi",
                "esp_timer", "esp_http_client")
    tokens = text.replace("\n", " ").split()
    additions = [component for component in required if component not in tokens]
    if additions:
        text = text.replace(marker, marker + " " + " ".join(additions), 1)
        cmake.write_text(text, encoding="utf-8")

    print("SafeLink Test 6B applied successfully")
    print(f"Project:       {project}")
    print(f"Test 6A backup:{backup}")
    print(f"Test 6B SHA256: {digest(target)}")
    print("SafeLink driver, sdkconfig, partitions and credentials are unchanged.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
