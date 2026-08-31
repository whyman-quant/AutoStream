"""Trusted absolute-path bootstrap for the frozen L4 runner release."""

from __future__ import annotations

import os
import sys


def _is_within(path, parent):
    try:
        return os.path.commonpath((path, parent)) == parent
    except ValueError:
        return False


def _safe_sys_path(runner_root):
    runner = os.path.realpath(runner_root)
    trusted_prefixes = {
        os.path.realpath(prefix)
        for prefix in (sys.base_prefix, sys.prefix, sys.exec_prefix)
    }
    safe = [runner]
    for entry in sys.path:
        if not entry:
            continue
        candidate = os.path.realpath(entry)
        if candidate == runner or not any(
            _is_within(candidate, prefix) for prefix in trusted_prefixes
        ):
            continue
        safe.append(candidate)
    return safe


def main():
    runner_root = os.path.dirname(os.path.realpath(__file__))
    sys.path[:] = _safe_sys_path(runner_root)
    os.environ.pop("PYTHONPATH", None)
    from campaigns.l4_production import main as production_main

    return production_main()


if __name__ == "__main__":
    raise SystemExit(main())
