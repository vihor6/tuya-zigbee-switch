#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: materialize_silabs_slcp.py <source> <target> <sdk_line> <sdk_version>",
            file=sys.stderr,
        )
        return 64

    source, target, sdk_line, sdk_version = sys.argv[1:]
    text = pathlib.Path(source).read_text()

    text = re.sub(
        r"^sdk:\s*\{id:\s*[^,]+,\s*version:\s*[^}]+\}$",
        f"sdk: {{id: {sdk_line}, version: {sdk_version}}}",
        text,
        count=1,
        flags=re.MULTILINE,
    )

    if sdk_line == "simplicity_sdk":
        text = re.sub(
            r"(?m)^- condition: \[device_series_1\]\n  id: emlib_adc\n",
            "",
            text,
            count=1,
        )

    target_path = pathlib.Path(target)
    target_path.parent.mkdir(parents=True, exist_ok=True)
    target_path.write_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
