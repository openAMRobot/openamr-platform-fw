#!/usr/bin/env python3
"""Build and test each portable host project, and optionally run cppcheck."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE_SUFFIXES = {".c", ".h"}
WARNING_RE = re.compile(r"(?:^|[\s:])warning(?:\s+[A-Za-z]+\d+)?\s*:", re.IGNORECASE)
CTEST_RESULT_RE = re.compile(r"^\d+% tests passed,.*$", re.MULTILINE)
PROTECTED_SOURCE_DIRS = tuple(
    ROOT / name for name in (".github", "boards", "configs", "docs", "firmware", "tests", "tools")
)
# Per-test CTest timeout. A test that never returns must fail, not stall the run.
DEFAULT_TEST_TIMEOUT_S = 60
EXCLUDED_FIRMWARE_DIRS = {"bsp", "cmsis", "core", "drivers", "middlewares", "vendor"}


class RunnerError(Exception):
    """A command-line or execution environment error."""


def display_path(path: Path) -> str:
    """Display repository paths relative to ROOT, regardless of the caller's cwd."""
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def discover_projects() -> list[Path]:
    """Return direct tests/host child projects in deterministic name order."""
    projects = [
        path.parent
        for path in (ROOT / "tests" / "host").glob("*/CMakeLists.txt")
        if path.is_file()
    ]
    return sorted(projects, key=lambda path: path.name)


def is_portable_firmware_source(path: Path) -> bool:
    """Exclude STM32 vendor trees and Core sources from portable firmware."""
    relative = path.relative_to(ROOT / "firmware")
    for part in relative.parts[:-1]:
        folded = part.casefold()
        if folded in EXCLUDED_FIRMWARE_DIRS or "stm32" in folded:
            return False
    return True


def static_sources() -> list[Path]:
    """Collect host-test and portable firmware C sources and headers."""
    sources: list[Path] = []
    host_root = ROOT / "tests" / "host"
    if host_root.is_dir():
        sources.extend(
            path for path in host_root.rglob("*")
            if path.is_file() and path.suffix in SOURCE_SUFFIXES
        )

    firmware_root = ROOT / "firmware"
    if firmware_root.is_dir():
        sources.extend(
            path for path in firmware_root.rglob("*")
            if path.is_file()
            and path.suffix in SOURCE_SUFFIXES
            and is_portable_firmware_source(path)
        )
    return sorted(sources, key=lambda path: path.relative_to(ROOT).as_posix())


def is_within(path: Path, directory: Path) -> bool:
    return path == directory or directory in path.parents


def resolve_build_root(value: str | os.PathLike[str]) -> Path:
    """Resolve a build root without creating, deleting, or cleaning anything."""
    supplied = Path(value).expanduser()
    build_root = (ROOT / supplied).resolve() if not supplied.is_absolute() else supplied.resolve()
    if build_root == ROOT:
        raise RunnerError("--build-dir must not be the repository root")
    if build_root.exists() and not build_root.is_dir():
        raise RunnerError(f"--build-dir is not a directory: {display_path(build_root)}")
    if any(is_within(build_root, source_dir) for source_dir in PROTECTED_SOURCE_DIRS):
        raise RunnerError("--build-dir must not be inside a source directory")

    # Also protect source trees outside the usual repository source directories.
    for candidate in (build_root, *build_root.parents):
        if candidate == ROOT or not candidate.is_dir():
            continue
        if (candidate / "CMakeLists.txt").is_file() or (candidate / ".git").exists():
            raise RunnerError(f"--build-dir must not be a source directory: {candidate}")
    return build_root


def require_tool(name: str) -> str:
    tool = shutil.which(name)
    if tool is None:
        raise RunnerError(f"required tool '{name}' was not found in PATH")
    return tool


def run_command(
    command: list[str],
    *,
    log_path: Path | None = None,
) -> tuple[int, str]:
    """Stream combined output, optionally saving the same bytes as text to a log."""
    printable = " ".join(shlex.quote(part) for part in command)
    print(f"+ {printable}", flush=True)
    output_parts: list[str] = []
    log_file = None
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_file = log_path.open("w", encoding="utf-8")
    try:
        with subprocess.Popen(
            command,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        ) as process:
            assert process.stdout is not None
            for line in process.stdout:
                output_parts.append(line)
                print(line, end="", flush=True)
                if log_file is not None:
                    log_file.write(line)
                    log_file.flush()
            return_code = process.wait()
    except OSError as exc:
        raise RunnerError(f"could not execute {command[0]}: {exc}") from exc
    finally:
        if log_file is not None:
            log_file.close()
    return return_code, "".join(output_parts)


def append_summary(heading: str, results: list[tuple[str, bool, str]]) -> None:
    """Print and, on GitHub Actions, append actual per-check results."""
    print(f"\n{heading}:")
    lines = []
    for name, passed, detail in results:
        status = "PASS" if passed else "FAIL"
        print(f"  {status} {name}: {detail}")
        lines.append(f"- **{status}** `{name}`: {detail}")

    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with Path(summary_path).open("a", encoding="utf-8") as summary:
            summary.write(f"### {heading}\n")
            summary.write("\n".join(lines) + "\n")


def run_static_analysis() -> int:
    cppcheck = require_tool("cppcheck")
    sources = static_sources()
    if not sources:
        detail = "no host-test or portable firmware C/H sources; nothing to check"
        append_summary("Static analysis", [("cppcheck", True, detail)])
        return 0

    command = [
        cppcheck,
        "--enable=warning,performance,portability",
        "--error-exitcode=1",
        "--std=c11",
        *(str(path) for path in sources),
    ]
    return_code, _ = run_command(command)
    passed = return_code == 0
    detail = f"{len(sources)} files checked; cppcheck exited {return_code}"
    append_summary("Static analysis", [("cppcheck", passed, detail)])
    return 0 if passed else 1


def ctest_detail(output: str, return_code: int) -> str:
    matches = CTEST_RESULT_RE.findall(output)
    if matches:
        return matches[-1].strip()
    return f"ctest exited {return_code} without a test-count summary"


def run_host_projects(build_root: Path, test_timeout: int = DEFAULT_TEST_TIMEOUT_S) -> int:
    projects = discover_projects()
    if not projects:
        firmware_sources = [
            path for path in (ROOT / "firmware").rglob("*.c")
            if is_portable_firmware_source(path)
        ]
        if firmware_sources:
            detail = (
                f"no tests/host projects, but firmware/ contains "
                f"{len(firmware_sources)} C source files"
            )
            append_summary("Host module tests", [("discovery", False, detail)])
            return 1
        append_summary(
            "Host module tests",
            [("discovery", True, "no firmware C sources or host projects; nothing to run")],
        )
        return 0

    for tool in ("cmake", "ninja", "ctest"):
        require_tool(tool)
    build_root.mkdir(parents=True, exist_ok=True)
    results: list[tuple[str, bool, str]] = []

    for project in projects:
        name = project.name
        project_build = build_root / name
        configure_log = build_root / f"{name}.configure.log"
        build_log = build_root / f"{name}.log"
        ctest_log = build_root / f"{name}.ctest"
        print(f"\n=== {name} ===", flush=True)

        configure_code, _ = run_command(
            ["cmake", "-S", str(project), "-B", str(project_build), "-G", "Ninja"],
            log_path=configure_log,
        )
        if configure_code != 0:
            results.append((name, False, f"CMake configure exited {configure_code}; build/ctest not run"))
            continue

        build_code, build_output = run_command(
            ["cmake", "--build", str(project_build)], log_path=build_log
        )
        if build_code != 0:
            results.append((name, False, f"build exited {build_code}; ctest not run"))
            continue
        if WARNING_RE.search(build_output):
            results.append((name, False, f"compiler warnings in {display_path(build_log)}; ctest not run"))
            continue

        test_code, test_output = run_command(
            [
                "ctest",
                "--test-dir",
                str(project_build),
                "--output-on-failure",
                "--no-tests=error",
                "--timeout",
                str(test_timeout),
            ],
            log_path=ctest_log,
        )
        detail = ctest_detail(test_output, test_code)
        results.append((name, test_code == 0, detail))

    append_summary("Host module tests", results)
    return 0 if all(passed for _, passed, _ in results) else 1


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--static-only",
        action="store_true",
        help="run cppcheck only; do not configure, build, or execute tests",
    )
    mode.add_argument(
        "--static",
        action="store_true",
        help="run cppcheck as well as all host builds and tests",
    )
    parser.add_argument(
        "--test-timeout",
        type=int,
        default=DEFAULT_TEST_TIMEOUT_S,
        metavar="SECONDS",
        help="per-test CTest timeout in seconds (default: %(default)s)",
    )
    parser.add_argument(
        "--build-dir",
        default="build/host",
        metavar="PATH",
        help="per-project build parent (relative paths are resolved from the repository root)",
    )
    args = parser.parse_args(argv)
    if args.test_timeout <= 0:
        parser.error("--test-timeout must be a positive number of seconds")
    args.build_root = resolve_build_root(args.build_dir)
    return args


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        if args.static_only:
            return run_static_analysis()

        failed = False
        if args.static:
            failed = run_static_analysis() != 0
        failed = run_host_projects(args.build_root, args.test_timeout) != 0 or failed
        return 1 if failed else 0
    except RunnerError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
