#!/usr/bin/env python3
"""Silkit 主题对比度检查（WCAG 2.1）。

扫描 frontend/css/themes/*.css 中的语义 token，校验关键前景/背景配对：
  - 正文 ≥ 7:1
  - 次要文本 / 语义色 / 语法色 / 主色文字 ≥ 4.5:1
  - 占位文本 / 输入描边 ≥ 3:1

无第三方依赖，可直接在 CI 里运行；未达标时退出码为 1。

用法：
    python tools/contrast_check.py
    python tools/contrast_check.py -v
    python tools/contrast_check.py frontend/css/themes/bloom.css
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, Iterable, List, NamedTuple, Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent
THEME_DIR = REPO_ROOT / "frontend" / "css" / "themes"
DEFAULT_ORDER = ["bloom", "noir", "lilac", "mocha", "mint"]

TOKEN_RE = re.compile(r"--([a-z0-9-]+)\s*:\s*(#[0-9a-fA-F]{6})\b")


class Check(NamedTuple):
    foreground: str
    background: str
    minimum: float
    label: str


CHECKS: Tuple[Check, ...] = (
    Check("text", "bg-1", 7.0, "正文 / 面板"),
    Check("text", "bg-0", 7.0, "正文 / 窗口底"),
    Check("text", "bg-2", 7.0, "正文 / 表头"),
    Check("text", "bg-3", 7.0, "正文 / 选中底"),
    Check("text-dim", "bg-1", 4.5, "次要文本 / 面板"),
    Check("text-dim", "bg-2", 4.5, "次要文本 / 表头"),
    Check("text-faint", "bg-1", 3.0, "占位/禁用 / 面板"),
    Check("accent", "bg-1", 4.5, "主色文字 / 面板"),
    Check("accent", "bg-0", 4.5, "主色文字 / 窗口底"),
    Check("accent", "accent-soft", 4.5, "主色文字 / 淡底"),
    Check("on-accent", "accent", 4.5, "主色上的文字"),
    Check("accent-2", "bg-1", 4.5, "辅助色 / 面板"),
    Check("success", "bg-1", 4.5, "成功色 / 面板"),
    Check("warning", "bg-1", 4.5, "警告色 / 面板"),
    Check("danger", "bg-1", 4.5, "错误色 / 面板"),
    Check("info", "bg-1", 4.5, "提示色 / 面板"),
    Check("border-strong", "bg-0", 3.0, "输入描边 / 窗口底"),
    Check("syntax-keyword", "bg-1", 4.5, "语法·关键字"),
    Check("syntax-string", "bg-1", 4.5, "语法·字符串"),
    Check("syntax-number", "bg-1", 4.5, "语法·数字"),
    Check("syntax-comment", "bg-1", 4.5, "语法·注释"),
    Check("syntax-function", "bg-1", 4.5, "语法·函数"),
    Check("syntax-type", "bg-1", 4.5, "语法·类型"),
    Check("syntax-operator", "bg-1", 4.5, "语法·运算符"),
    Check("syntax-null", "bg-1", 4.5, "语法·NULL"),
)


def parse_tokens(css: str) -> Dict[str, str]:
    return {match.group(1): match.group(2).upper() for match in TOKEN_RE.finditer(css)}


def hex_to_rgb(value: str) -> Tuple[int, int, int]:
    value = value.lstrip("#")
    if len(value) == 3:
        value = "".join(ch * 2 for ch in value)
    return int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16)


def linearize(channel: int) -> float:
    srgb = channel / 255.0
    return srgb / 12.92 if srgb <= 0.04045 else ((srgb + 0.055) / 1.055) ** 2.4


def relative_luminance(color: str) -> float:
    red, green, blue = hex_to_rgb(color)
    return 0.2126 * linearize(red) + 0.7152 * linearize(green) + 0.0722 * linearize(blue)


def contrast_ratio(foreground: str, background: str) -> float:
    lighter = max(relative_luminance(foreground), relative_luminance(background))
    darker = min(relative_luminance(foreground), relative_luminance(background))
    return (lighter + 0.05) / (darker + 0.05)


def theme_files(explicit: Iterable[str]) -> List[Path]:
    paths = [Path(item) for item in explicit]
    if not paths:
        paths = [THEME_DIR / (theme + ".css") for theme in DEFAULT_ORDER]
        # 允许以后新增主题而无需改脚本
        known = {path.resolve() for path in paths}
        for extra in sorted(THEME_DIR.glob("*.css")):
            if extra.resolve() not in known:
                paths.append(extra)
    return paths


def check_theme(path: Path, verbose: bool) -> Tuple[int, int]:
    if not path.is_file():
        print(f"  找不到主题文件: {path}")
        return 0, 1

    tokens = parse_tokens(path.read_text(encoding="utf-8"))
    if not tokens:
        print(f"  {path.name}: 未解析到颜色 token")
        return 0, 1

    failures = 0
    checks = 0
    print(f"\n{path.stem}")

    for check in CHECKS:
        foreground = tokens.get(check.foreground)
        background = tokens.get(check.background)
        if foreground is None or background is None:
            # 某些主题可能没有语法色（例如后续精简主题），跳过而不算失败
            if verbose:
                print(f"  SKIP  {check.label}: 缺少 --{check.foreground} 或 --{check.background}")
            continue

        checks += 1
        ratio = contrast_ratio(foreground, background)
        passed = ratio + 1e-9 >= check.minimum
        if not passed:
            failures += 1

        if verbose or not passed:
            status = "PASS" if passed else "FAIL"
            print(f"  {status}  {ratio:5.2f}:1  ≥ {check.minimum:3.1f}  {check.label}"
                  f"  ({foreground} on {background})")

    print(f"  {checks - failures}/{checks} 项通过")
    return checks, failures


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Silkit 主题对比度检查（WCAG 2.1）")
    parser.add_argument("files", nargs="*", help="主题 CSS 文件；默认检查 frontend/css/themes/*.css")
    parser.add_argument("-v", "--verbose", action="store_true", help="输出全部检查项")
    args = parser.parse_args(argv)

    paths = theme_files(args.files)
    print("Silkit 主题对比度检查")
    print(f"主题目录: {THEME_DIR}")

    total_checks = 0
    total_failures = 0
    for path in paths:
        checks, failures = check_theme(path, args.verbose)
        total_checks += checks
        total_failures += failures

    print()
    if total_failures == 0:
        print(f"[OK] {len(paths)} 套主题、{total_checks} 项配对全部达标")
        return 0

    print(f"[FAIL] {total_failures}/{total_checks} 项未达标，请调整对应 token 后重试")
    return 1


if __name__ == "__main__":
    sys.exit(main())
