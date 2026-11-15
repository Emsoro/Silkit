#!/usr/bin/env python3
"""Silkit E2E 执行入口（CODEPLAN §12.9）。

默认测试源（2026-11-15 实测可用）：
  MySQL 8.4.13  C:\\mysql-8.4.13-winx64\\bin\\mysql.exe  127.0.0.1:3306 root/123456 employees
  PostgreSQL 18.7  系统安装的 psql.exe                 127.0.0.1:5432 postgres/123456
  SQLite        data/silkit_test.db（由 tools/make_test_db.py 生成，走 CTest 单测）

用法：
  python tools/e2e_runner.py                      # 探测两个源，跑 integration_db + e2e_real_db
  python tools/e2e_runner.py --full               # 额外跑全部 CTest
  python tools/e2e_runner.py --read-only          # 关闭所有写路径（不建/删影子表）
  python tools/e2e_runner.py --seed-pg-employees  # 可选：在 PG 建 employees 镜像库后重跑
  python tools/e2e_runner.py --pg-db employees    # 指定 PG 库名

写路径说明：默认会开启 SILKIT_INTEGRATION_WRITE / SILKIT_E2E_WRITE，
integration_db 会建删 silkit_m5_probe / silkit_m6_* / silkit_m6_db_probe，
e2e_real_db 会建删 silkit_e2e_csv_probe；均为独立影子对象，不改动业务表。
不想触碰任何数据时加 --read-only。
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MYSQL_DEFAULT = Path(r"C:\mysql-8.4.13-winx64\bin\mysql.exe")
PSQL_DEFAULT = Path(r"C:\Program Files\PostgreSQL\18\bin\psql.exe")
SEED_SQL = ROOT / "tests" / "e2e" / "seed_pg_employees.sql"

LOG: list[str] = []


def note(message: str) -> None:
    print(message, flush=True)
    LOG.append(message)


def run(argv: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(argv, capture_output=True, text=True, encoding="utf-8",
                              errors="replace", env=env, timeout=180)
    except FileNotFoundError:
        return subprocess.CompletedProcess(argv, 127, "", f"找不到可执行文件: {argv[0]}")
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(argv, 124, "", "执行超时（180s）")


def locate(explicit: str | None, default: Path, fallback_name: str) -> str:
    if explicit:
        return explicit
    if default.exists():
        return str(default)
    found = shutil.which(fallback_name)
    if found:
        return found
    for candidate in sorted(Path(r"C:\Program Files\PostgreSQL").glob("*/bin/psql.exe")):
        return str(candidate)
    return fallback_name


def mysql_env(password: str) -> dict[str, str]:
    env = os.environ.copy()
    env["MYSQL_PWD"] = password  # 避免密码出现在命令行/日志里
    return env


def pg_env(password: str) -> dict[str, str]:
    env = os.environ.copy()
    env["PGPASSWORD"] = password
    env["PGCLIENTENCODING"] = "UTF8"
    return env


def mysql_query(exe: str, args: argparse.Namespace, sql: str, database: str) -> tuple[bool, str]:
    argv = [exe, "-h", args.mysql_host, "-P", args.mysql_port, "-u", args.mysql_user,
            "-D", database, "-N", "-B", "-e", sql]
    result = run(argv, mysql_env(args.mysql_password))
    return result.returncode == 0, (result.stdout or result.stderr).strip()


def psql_query(exe: str, args: argparse.Namespace, sql: str, database: str) -> tuple[bool, str]:
    argv = [exe, "-h", args.pg_host, "-p", args.pg_port, "-U", args.pg_user,
            "-d", database, "-tAc", sql]
    result = run(argv, pg_env(args.pg_password))
    return result.returncode == 0, (result.stdout or result.stderr).strip()


def psql_exec(exe: str, args: argparse.Namespace, database: str,
              extra: list[str]) -> tuple[bool, str]:
    argv = [exe, "-h", args.pg_host, "-p", args.pg_port, "-U", args.pg_user,
            "-d", database, "-v", "ON_ERROR_STOP=1", *extra]
    result = run(argv, pg_env(args.pg_password))
    return result.returncode == 0, (result.stdout or result.stderr).strip()


def seed_pg_employees(exe: str, args: argparse.Namespace) -> bool:
    ok, value = psql_query(exe, args,
                           "SELECT 1 FROM pg_database WHERE datname = 'employees'", "postgres")
    if not ok:
        note(f"[WARN] 无法检查 PG 数据库列表：{value[:200]}")
        return False
    if value.strip() == "1":
        note("[OK]   PostgreSQL employees 库已存在，跳过建库与重建（不覆盖既有数据）")
        return True
    ok, out = psql_exec(exe, args, "postgres", ["-c", "CREATE DATABASE employees"])
    if not ok:
        note(f"[WARN] 创建 PG employees 库失败：{out[:300]}")
        return False
    note("[OK]   已创建 PostgreSQL employees 数据库")
    ok, out = psql_exec(exe, args, "employees", ["-f", str(SEED_SQL)])
    if not ok:
        note(f"[WARN] 导入 employees 夹具失败：{out[:500]}")
        return False
    note("[OK]   已导入 employees 精简镜像（9 部门 / 12 员工 / 18 薪资行）")
    return True


def msbuild_env() -> dict[str, str]:
    """MSBuild 的 CL 任务用 .NET 的 ProcessStartInfo.EnvironmentVariables（大小写不敏感），
    环境里同时存在 HTTPS_PROXY 与 https_proxy 时会抛 MSB6001「已添加项」。
    仅保留大写代理变量，避免大小写重复键。
    """
    env = os.environ.copy()
    for key in ("http_proxy", "https_proxy", "all_proxy", "no_proxy"):
        if key in env and key.upper() in env:
            env.pop(key, None)
    return env


def build_targets(args: argparse.Namespace) -> bool:
    cmake = shutil.which("cmake")
    if not cmake:
        note("[WARN] PATH 中无 cmake，跳过构建（假定产物已是最新）")
        return True
    for target in ("e2e_real_db", "integration_db"):
        result = run([cmake, "--build", str(args.build_dir), "--config", args.config,
                      "--target", target], msbuild_env())
        if result.returncode != 0:
            note(f"[FAIL] 构建 {target} 失败：\n{(result.stdout + result.stderr)[-1500:]}")
            return False
        note(f"[OK]   已构建 {target} ({args.config})")
    return True


def write_report(path: Path, args: argparse.Namespace, sources: dict[str, str],
                 output: str, returncode: int) -> None:
    # 只统计 E2E-xx / SSL-xx 编号行；单元测试输出里的 "[PASS] 93 checks" 不计入。
    counts = {key: len(re.findall(rf"\[{key}\]\s+(?:E2E|SSL)-\d+", output))
              for key in ("PASS", "FAIL", "SKIP")}
    lines = [
        "# Silkit E2E 执行报告",
        "",
        f"- 时间：{dt.datetime.now().isoformat(timespec='seconds')}",
        f"- 构建：`{args.build_dir}` / {args.config}",
        f"- 写路径：{'关闭 (--read-only)' if args.read_only else '开启（影子表，业务表不动）'}",
        f"- 结果：PASS {counts['PASS']} / FAIL {counts['FAIL']} / SKIP {counts['SKIP']} · "
        f"进程退出码 {returncode}",
        "",
        "## 测试源",
        "",
    ]
    for key, value in sources.items():
        lines.append(f"- {key}：{value}")
    lines += ["", "## 用例输出", "", "```text", output.rstrip()[-20000:], "```", ""]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")
    note(f"[OK]   报告已写入 {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Silkit E2E runner")
    parser.add_argument("--mysql-host", default="127.0.0.1")
    parser.add_argument("--mysql-port", default="3306")
    parser.add_argument("--mysql-user", default="root")
    parser.add_argument("--mysql-password", default="123456")
    parser.add_argument("--mysql-db", default="employees")
    parser.add_argument("--mysql-bin", default=None)
    parser.add_argument("--mysql-nossl-host", default=None,
                        help="可选：不声明 CLIENT_SSL 的 MySQL 协议服务端，启用 SSL-01~03")
    parser.add_argument("--mysql-nossl-port", default="3307")
    parser.add_argument("--mysql-nossl-user", default="root")
    parser.add_argument("--mysql-nossl-password", default="")
    parser.add_argument("--pg-host", default="127.0.0.1")
    parser.add_argument("--pg-port", default="5432")
    parser.add_argument("--pg-user", default="postgres")
    parser.add_argument("--pg-password", default="123456")
    parser.add_argument("--pg-db", default="")
    parser.add_argument("--pg-schema", default="public")
    parser.add_argument("--psql-bin", default=None)
    parser.add_argument("--build-dir", default=str(ROOT / "build"))
    parser.add_argument("--config", default="Release", choices=["Release", "Debug"])
    parser.add_argument("--report", default="")
    parser.add_argument("--full", action="store_true", help="跑全部 CTest，而不只是两个真实库目标")
    parser.add_argument("--read-only", action="store_true", help="关闭所有写路径")
    parser.add_argument("--no-build", action="store_true", help="跳过增量构建")
    parser.add_argument("--seed-pg-employees", action="store_true",
                        help="可选：在 PG 创建 employees 镜像库（需 CREATE DATABASE 权限）")
    args = parser.parse_args()
    args.build_dir = Path(args.build_dir)

    mysql_exe = locate(args.mysql_bin, MYSQL_DEFAULT, "mysql")
    pg_exe = locate(args.psql_bin, PSQL_DEFAULT, "psql")
    note("Silkit E2E runner · " + dt.date.today().isoformat())

    sources: dict[str, str] = {}
    env = os.environ.copy()

    # MySQL 预检
    mysql_ok, mysql_version = mysql_query(mysql_exe, args, "SELECT VERSION()", args.mysql_db)
    if mysql_ok:
        env.update({
            "SILKIT_MYSQL_HOST": args.mysql_host,
            "SILKIT_MYSQL_PORT": args.mysql_port,
            "SILKIT_MYSQL_USER": args.mysql_user,
            "SILKIT_MYSQL_PASSWORD": args.mysql_password,
            "SILKIT_MYSQL_DATABASE": args.mysql_db,
        })
        sources["MySQL"] = (f"{mysql_version} · {args.mysql_host}:{args.mysql_port} · "
                            f"{args.mysql_user}@{args.mysql_db} · {mysql_exe}")
        note(f"[OK]   MySQL 可用：{mysql_version} · {args.mysql_db}")
    else:
        note(f"[WARN] MySQL 不可用，跳过该引擎：{mysql_version[:200]}")

    # 可选：不声明 CLIENT_SSL 的 MySQL 协议服务端（如 sqltiny），回归 SSL 模式降级。
    nossl_host = args.mysql_nossl_host or os.environ.get("SILKIT_MYSQL_NOSSL_HOST", "")
    if nossl_host:
        env.update({
            "SILKIT_MYSQL_NOSSL_HOST": nossl_host,
            "SILKIT_MYSQL_NOSSL_PORT": args.mysql_nossl_port,
            "SILKIT_MYSQL_NOSSL_USER": args.mysql_nossl_user,
            "SILKIT_MYSQL_NOSSL_PASSWORD": args.mysql_nossl_password,
        })
        sources["MySQL(no-TLS)"] = (f"{nossl_host}:{args.mysql_nossl_port} · "
                                    f"prefer/disable/require 模式回归")
        note(f"[OK]   MySQL(no-TLS) 已启用：{nossl_host}:{args.mysql_nossl_port}")

    # PostgreSQL 预检：employees 存在则优先，否则退回 silkit_test
    pg_ok, pg_version = psql_query(pg_exe, args, "SELECT version()", "postgres")
    if pg_ok:
        if args.seed_pg_employees:
            seed_pg_employees(pg_exe, args)
        has_employees, flag = psql_query(
            pg_exe, args, "SELECT 1 FROM pg_database WHERE datname = 'employees'", "postgres")
        pg_db = args.pg_db
        if not pg_db:
            pg_db = "employees" if has_employees and flag.strip() == "1" else "silkit_test"
        if pg_db == "silkit_test":
            note("[WARN] PG 上找不到 employees 库，本轮退回 silkit_test；"
                 "如需对称数据可加 --seed-pg-employees")
        db_ok, probe = psql_query(pg_exe, args, "SELECT current_database()", pg_db)
        if db_ok:
            env.update({
                "SILKIT_PG_HOST": args.pg_host,
                "SILKIT_PG_PORT": args.pg_port,
                "SILKIT_PG_USER": args.pg_user,
                "SILKIT_PG_PASSWORD": args.pg_password,
                "SILKIT_PG_DATABASE": pg_db,
                "SILKIT_PG_SCHEMA": args.pg_schema,
            })
            sources["PostgreSQL"] = (f"{pg_version.split(',')[0]} · "
                                     f"{args.pg_host}:{args.pg_port} · "
                                     f"{args.pg_user}@{pg_db} · {pg_exe}")
            note(f"[OK]   PostgreSQL 可用：{pg_db} · {pg_version.split(',')[0]}")
        else:
            note(f"[WARN] PG 库 {pg_db} 不可连，跳过该引擎：{probe[:200]}")
    else:
        note(f"[WARN] PostgreSQL 不可用，跳过该引擎：{pg_version[:200]}")

    if not sources:
        note("[FAIL] 两个测试源都不可用，E2E 终止")
        return 2

    if not args.read_only:
        env["SILKIT_INTEGRATION_WRITE"] = "1"
        env["SILKIT_E2E_WRITE"] = "1"
    else:
        env.pop("SILKIT_INTEGRATION_WRITE", None)
        env.pop("SILKIT_E2E_WRITE", None)

    if not args.no_build and not build_targets(args):
        return 3

    ctest = shutil.which("ctest")
    if not ctest:
        cmake = shutil.which("cmake")
        if cmake:
            sibling = Path(cmake).with_name("ctest.exe")
            if sibling.exists():
                ctest = str(sibling)
    if not ctest:
        note("[FAIL] 找不到 ctest")
        return 4

    selector = r"integration_db|e2e_real_db" if not args.full else r".*"
    # -V：即便全部通过也保留 E2E-xx 逐项输出，供报告与 CODEPLAN §12.4 核对。
    argv = [ctest, "--test-dir", str(args.build_dir), "-C", args.config,
            "-R", selector, "--output-on-failure", "-V"]
    note(f"[RUN]  {' '.join(argv)}")
    result = run(argv, env)
    output = (result.stdout or "") + (result.stderr or "")
    print(output, end="")
    note(f"[DONE] CTest 退出码 {result.returncode}")

    report = Path(args.report) if args.report else args.build_dir / "e2e-report.md"
    write_report(report, args, sources, output, result.returncode)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
