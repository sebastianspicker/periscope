#!/usr/bin/env python3
"""Repository-level verification dispatcher."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RADAR_BUILD = Path(os.environ.get("PERISCOPE_RADAR_BUILD", ROOT / "radar/build/verify"))
BUILD_JOBS = os.environ.get("PERISCOPE_BUILD_JOBS", "4")


def run(*command: str, cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd or ROOT, check=True)


def verify_architecture() -> None:
    run(sys.executable, str(ROOT / "scripts/check_architecture.py"))


def verify_radar() -> None:
    run(
        "cmake",
        "-S",
        str(ROOT / "radar"),
        "-B",
        str(RADAR_BUILD),
        "-DLR_BUILD_STRATEGY_LAB=ON",
    )
    run("cmake", "--build", str(RADAR_BUILD), "-j", BUILD_JOBS)
    run("ctest", "--test-dir", str(RADAR_BUILD), "--output-on-failure")
    run(str(RADAR_BUILD / "strategy_lab"), "list")
    run(str(RADAR_BUILD / "strategy_lab"), "all", "--quiet")


def verify_vision() -> None:
    vision = ROOT / "vision"
    run("uv", "sync", "--frozen", "--extra", "dev", cwd=vision)
    run("uv", "run", "ruff", "check", "src/", "tests/", cwd=vision)
    run("uv", "run", "ruff", "format", "--check", "src/", "tests/", cwd=vision)
    run("uv", "run", "mypy", "--strict", "src/cs2_vision_access/", cwd=vision)
    run("uv", "run", "pytest", "tests/", "-q", cwd=vision)
    run("uv", "build", cwd=vision)


def main() -> int:
    actions = {
        "architecture": (verify_architecture,),
        "radar-sim": (verify_radar,),
        "vision-cpu": (verify_vision,),
        "all": (verify_architecture, verify_radar, verify_vision),
    }
    requested = sys.argv[1] if len(sys.argv) == 2 else ""
    if requested not in actions:
        print("usage: verify.py architecture|radar-sim|vision-cpu|all", file=sys.stderr)
        return 2
    for action in actions[requested]:
        action()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
