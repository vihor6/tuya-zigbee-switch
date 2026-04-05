#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from pathlib import Path

import yaml


def unique_preserving_order(items: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for item in items:
        if item not in seen:
            seen.add(item)
            result.append(item)
    return result


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: resolve_build_plan.py <device_db.yaml> <event_name> <mode> <boards-input>",
            file=sys.stderr,
        )
        return 64

    db_path = Path(sys.argv[1])
    event_name = sys.argv[2]
    requested_mode = sys.argv[3].strip() or "compile_only"
    boards_input = sys.argv[4].strip()

    if requested_mode not in {"compile_only", "full_build"}:
        print(f"Unsupported build mode: {requested_mode}", file=sys.stderr)
        return 64

    if event_name != "workflow_dispatch":
        requested_mode = "compile_only"

    db = yaml.safe_load(db_path.read_text())

    if boards_input:
        requested_boards = unique_preserving_order(boards_input.split())
        unknown = [board for board in requested_boards if board not in db]
        if unknown:
            print(
                f"Unknown board key(s): {', '.join(unknown)}",
                file=sys.stderr,
            )
            return 1
        boards = requested_boards
    else:
        boards = sorted(
            key
            for key, value in db.items()
            if isinstance(value, dict) and value.get("build") is True
        )

    need_silabs = any(db[board].get("mcu_family") == "Silabs" for board in boards)
    need_telink = any(db[board].get("mcu_family") == "Telink" for board in boards)

    if need_silabs:
        manifest = subprocess.check_output(
            ["make", "-s", "-f", "board.mk", "print-silabs-sdk-install-manifest"],
            text=True,
        )
        silabs_manifest_hash = hashlib.sha256(manifest.encode()).hexdigest()
    else:
        silabs_manifest_hash = "no-silabs"

    plan = {
        "boards_json": json.dumps(boards),
        "board_count": str(len(boards)),
        "build_mode": requested_mode,
        "need_silabs": "true" if need_silabs else "false",
        "need_telink": "true" if need_telink else "false",
        "clear_indexes": "true"
        if requested_mode == "full_build" and not boards_input
        else "false",
        "silabs_manifest_hash": silabs_manifest_hash,
    }

    for key, value in plan.items():
        print(f"{key}={value}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
