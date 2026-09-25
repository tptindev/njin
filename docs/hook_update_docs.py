"""Claude Code PostToolUse hook: regenerate the Doxygen docs after an edit.

Reads the hook JSON from stdin. If the edited file is part of the docs sources
(the public API headers, or anything under docs/ except the generated output),
it runs build_docs.bat. Doxygen takes well under a second and uses no tokens.

Doxygen warnings (undocumented symbols, broken references) are handed back to
Claude as additionalContext so they get fixed right away. Missing Doxygen or an
unrelated file is silently ignored: the hook never blocks an edit.
"""

import json
import subprocess
import sys
from pathlib import Path

DOCS = Path(__file__).resolve().parent
ROOT = DOCS.parent
API = ROOT / "src" / "engine" / "api"
IGNORED = (DOCS / "html", DOCS / "doxygen_warnings.txt")


def edited_path(payload: dict) -> Path | None:
    tool_input = payload.get("tool_input") or {}
    tool_response = payload.get("tool_response") or {}
    raw = tool_input.get("file_path") or tool_response.get("filePath")
    if not raw:
        return None
    path = Path(raw)
    if not path.is_absolute():
        path = ROOT / path
    return path.resolve()


def is_docs_source(path: Path) -> bool:
    if any(path == ignored or ignored in path.parents for ignored in IGNORED):
        return False
    return API in path.parents or DOCS in path.parents


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    path = edited_path(payload)
    if path is None or not is_docs_source(path):
        return 0

    try:
        result = subprocess.run(
            ["cmd", "/c", str(DOCS / "build_docs.bat"), "nopause"],
            cwd=ROOT,
            capture_output=True,
            timeout=60,
        )
    except (OSError, subprocess.TimeoutExpired):
        return 0
    if result.returncode != 0:
        return 0

    warnings_file = DOCS / "doxygen_warnings.txt"
    if not warnings_file.exists():
        return 0
    warnings = warnings_file.read_text(encoding="utf-8", errors="replace").strip()
    if not warnings:
        return 0

    context = "Doxygen docs regenerated with warnings:\n" + warnings
    print(
        json.dumps(
            {
                "hookSpecificOutput": {
                    "hookEventName": "PostToolUse",
                    "additionalContext": context,
                }
            }
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
