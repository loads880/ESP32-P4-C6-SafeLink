#!/usr/bin/env python3
"""Apply SafeLink Test 4 to the exact SafeLink Test 3 P4 project."""

from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path

EXPECTED_MAIN_SHA256 = "f6dee5cd7bc5c2a7e648e5c0bb5650f6a40a0fbbfb24378ba0d3726050e08d1b"
EXPECTED_DRIVER_SHA256 = "cae89b892b4b051441ff268eb8ff82c0ec073d77aa8f94d872060023df77271e"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def backup_once(path: Path) -> Path:
    backup = path.with_name(path.name + ".safelink-test3-backup")
    if not backup.exists():
        shutil.copy2(path, backup)
    return backup


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True, type=Path,
                        help="P4 project directory, for example p4-safelink-test1")
    args = parser.parse_args()
    project = args.project.resolve()
    kit = Path(__file__).resolve().parent

    main_source = project / "main" / "transport_config_main.c"
    main_cmake = project / "main" / "CMakeLists.txt"
    driver = (project / "managed_components" / "espressif__esp_hosted" /
              "host" / "drivers" / "transport" / "sdio" / "sdio_drv.c")
    kconfig = project / "main" / "Kconfig.projbuild"
    old_config = project / "sdkconfig.safelink-eco2-final"
    new_config = project / "sdkconfig.safelink-eco2-test4"

    required = [main_source, main_cmake, driver, old_config]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise FileNotFoundError("Required project files missing:\n" + "\n".join(missing))

    actual_main = sha256(main_source)
    actual_driver = sha256(driver)
    if actual_main != EXPECTED_MAIN_SHA256:
        raise RuntimeError(
            "Main application is not the Test 3 checkpoint.\n"
            f"Expected {EXPECTED_MAIN_SHA256}\nActual   {actual_main}")
    if actual_driver != EXPECTED_DRIVER_SHA256:
        raise RuntimeError(
            "SDIO driver is not the proven Test 3 SafeLink driver.\n"
            f"Expected {EXPECTED_DRIVER_SHA256}\nActual   {actual_driver}")

    cmake_text = main_cmake.read_text(encoding="utf-8")
    old_dependency = "PRIV_REQUIRES esp_wifi console nvs_flash"
    new_dependency = "PRIV_REQUIRES esp_wifi console nvs_flash esp_http_client esp_timer"
    if old_dependency not in cmake_text and new_dependency not in cmake_text:
        raise RuntimeError("Unexpected main/CMakeLists.txt; no files were changed")

    backup_once(main_source)
    backup_once(main_cmake)
    if kconfig.exists():
        backup_once(kconfig)

    shutil.copy2(kit / "transport_config_main.test4.c", main_source)
    if new_dependency not in cmake_text:
        main_cmake.write_text(cmake_text.replace(old_dependency, new_dependency),
                              encoding="utf-8")
    shutil.copy2(kit / "Kconfig.projbuild.test4", kconfig)
    if not new_config.exists():
        shutil.copy2(old_config, new_config)

    print("SafeLink Test 4 applied successfully")
    print(f"Project:       {project}")
    print(f"Main backup:   {main_source}.safelink-test3-backup")
    print(f"Driver SHA256: {sha256(driver)} (unchanged)")
    print(f"New sdkconfig: {new_config}")
    print("Next: run menuconfig and enter the four SafeLink Test 4 settings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
