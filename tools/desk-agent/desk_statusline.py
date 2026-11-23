#!/usr/bin/env python3
"""Claude Code status line that saves the subscription limits for the desk agent.

Claude Code runs the statusLine command with a JSON object on stdin; for Pro, Max and Ultra
subscribers it carries rate_limits.five_hour / seven_day (used_percentage, resets_at) once the
session has had an API response (https://code.claude.com/docs/en/statusline.md). This script
keeps the last rate_limits it saw in ~/.claude/desk-rate-limits.json, which desk_agent.py
serves to the reader, and prints a short status line.

settings.json:
  "statusLine": {"type": "command", "command": "python C:/path/desk_statusline.py",
                 "refreshInterval": 60}
"""

from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path

LIMITS_PATH = Path.home() / ".claude" / "desk-rate-limits.json"


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except (ValueError, OSError):
        data = {}
    limits = data.get("rate_limits") if isinstance(data, dict) else None
    if isinstance(limits, dict) and limits:
        try:
            LIMITS_PATH.parent.mkdir(parents=True, exist_ok=True)
            tmp = LIMITS_PATH.with_suffix(".tmp")
            tmp.write_text(json.dumps({"saved": int(time.time()), "rate_limits": limits}), encoding="utf-8")
            os.replace(tmp, LIMITS_PATH)
        except OSError:
            pass

    parts = []
    model = (data.get("model") or {}).get("display_name") if isinstance(data, dict) else None
    if model:
        parts.append(str(model))
    if isinstance(limits, dict):
        for key, label in (("five_hour", "5h"), ("seven_day", "7d")):
            window = limits.get(key)
            if isinstance(window, dict) and window.get("used_percentage") is not None:
                parts.append(f"{label} {round(float(window['used_percentage']))}%")
    # Claude Code reads the line as UTF-8; Windows Python would write the console code page.
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    sys.stdout.write(" · ".join(parts))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
