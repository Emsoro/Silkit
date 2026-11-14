#!/usr/bin/env python3
"""校验 Silkit 版本号在各处是否一致。

版本号以 CMakeLists.txt 的 project(... VERSION x.y.z) 为准，
其余位置必须与之相同。用法：

    python tools/check_version.py
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def cmake_version() -> str:
    match = re.search(r"project\(\s*Silkit\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", read("CMakeLists.txt"))
    if not match:
        raise SystemExit("无法从 CMakeLists.txt 读取 project(Silkit VERSION ...)")
    return match.group(1)


def collect() -> dict[str, str | None]:
    html = read("frontend/index.html")
    html_match = re.search(r'id="statusVersion"[^>]*>\s*v?([0-9]+\.[0-9]+\.[0-9]+)', html)
    app_js = read("frontend/js/app.js")
    app_match = re.search(r"info\.version\s*\|\|\s*'([0-9]+\.[0-9]+\.[0-9]+)'", app_js)
    mock_js = read("frontend/js/mock/mock.js")
    mock_match = re.search(r"version:\s*'([0-9]+\.[0-9]+\.[0-9]+)'", mock_js)
    readme = read("README.md")
    readme_match = re.search(r"当前版本：\*\*v([0-9]+\.[0-9]+\.[0-9]+)\*\*", readme)
    limit_match = re.search(r"## 已知限制（v([0-9]+\.[0-9]+\.[0-9]+)）", readme)

    return {
        "CMakeLists.txt (project VERSION)": cmake_version(),
        "vcpkg.json (version-string)": json.loads(read("vcpkg.json")).get("version-string"),
        "src/app/version.h (兜底宏)": _search(r'#define\s+SILKIT_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+)"', "src/app/version.h"),
        "frontend/index.html (状态栏)": html_match.group(1) if html_match else None,
        "frontend/js/app.js (回退值)": app_match.group(1) if app_match else None,
        "frontend/js/mock/mock.js (app.info)": mock_match.group(1) if mock_match else None,
        "README.md (当前版本)": readme_match.group(1) if readme_match else None,
        "README.md (已知限制标题)": limit_match.group(1) if limit_match else None,
    }


def _search(pattern: str, path: str) -> str | None:
    match = re.search(pattern, read(path))
    return match.group(1) if match else None


def main() -> int:
    expected = cmake_version()
    entries = collect()
    failures = 0

    for name, value in entries.items():
        if value == expected:
            print(f"  [OK]   {name}: {value}")
        else:
            failures += 1
            print(f"  [FAIL] {name}: {value!r}（期望 {expected}）")

    if failures:
        print(f"\n版本检查失败：{failures} 处与 CMakeLists.txt 的 {expected} 不一致")
        return 1

    print(f"\n版本检查通过：全部为 {expected}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
