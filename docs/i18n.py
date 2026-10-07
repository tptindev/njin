"""Keep the English wiki in step with the Vietnamese one.

The Vietnamese files are the source of truth. Each has an English copy:

  src/engine/api/*.h   ->  docs/api_en/*.h
  docs/pages/*         ->  docs/pages_en/*
  docs/examples/*      ->  docs/examples_en/*   (only files with Vietnamese text)

  python docs/i18n.py check            list what is missing, stale or drifted
  python docs/i18n.py stamp [PATH...]  record that the English copy of these
                                       Vietnamese files is up to date (all if none)

check reports, per file:
  missing   no English copy yet
  stale     the Vietnamese file changed since the last stamp
  code      the code differs once comments and string contents are removed
            (headers and examples): a signature changed on one side only
  links     the page ids, @ref targets or code fences differ (pages)
  orphan    an English file whose Vietnamese source is gone

Exit status is 1 if anything is reported. Stamps live in docs/i18n_en.json.
"""

import hashlib
import json
import re
import sys
from pathlib import Path

DOCS = Path(__file__).resolve().parent
ROOT = DOCS.parent
STAMPS = DOCS / "i18n_en.json"

# (Vietnamese folder, English folder, kind, file pattern)
PAIRS = [
    (ROOT / "src" / "engine" / "api", DOCS / "api_en", "code", "*.h"),
    (DOCS / "pages", DOCS / "pages_en", "page", "*"),
    (DOCS / "examples", DOCS / "examples_en", "code", "*"),
]

# Any letter Vietnamese uses that ASCII does not have: a file without one has
# nothing to translate and may be shared.
VI_LETTER = re.compile(
    "[àáảãạăằắẳẵặâầấẩẫậèéẻẽẹêềếểễệìíỉĩịòóỏõọôồốổỗộơờớởỡợùúủũụưừứửữựỳýỷỹỵđ"
    "ÀÁẢÃẠĂẰẮẲẴẶÂẦẤẨẪẬÈÉẺẼẸÊỀẾỂỄỆÌÍỈĨỊÒÓỎÕỌÔỒỐỔỖỘƠỜỚỞỠỢÙÚỦŨỤƯỪỨỬỮỰỲÝỶỸỴĐ]"
)


def read(path: Path) -> str:
    return path.read_bytes().decode("utf-8", errors="replace").replace("\r\n", "\n")


def digest(path: Path) -> str:
    return hashlib.sha1(read(path).encode("utf-8")).hexdigest()


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def strip_code(text: str, lua: bool = False) -> str:
    """Code without comments, string contents or layout, for comparing.

    Lua comments are -- and --[[ ]]; in Lua // is floor division, not a comment.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        two = text[i : i + 2]
        c = text[i]
        if lua and two == "--":
            if text.startswith("[[", i + 2):
                j = text.find("]]", i + 4)
                i = n if j < 0 else j + 2
            else:
                while i < n and text[i] != "\n":
                    i += 1
        elif lua and two in ("//", "/*"):
            out.append(c)
            i += 1
        elif two == "//":
            while i < n and text[i] != "\n":
                i += 1
        elif two == "/*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c in "\"'":
            i += 1
            while i < n and text[i] != c and text[i] != "\n":
                i += 2 if text[i] == "\\" else 1
            i += 1
            out.append(c + c)
        else:
            out.append(c)
            i += 1
    return " ".join("".join(out).split())


def page_ids(text: str) -> list:
    ids = re.findall(r"\{#([\w.:-]+)\}", text)
    ids += re.findall(r"[@\\](?:ref|subpage|section|subsection|anchor)\s+([\w.:-]+)", text)
    ids = [i.rstrip(".:-") for i in ids]  # "@ref game_loop." ends a sentence
    ids.append("fences=%d" % text.count("```"))
    return sorted(ids)


def files(folder: Path, pattern: str = "*") -> dict:
    if not folder.is_dir():
        return {}
    return {p.name: p for p in sorted(folder.glob(pattern)) if p.is_file()}


def load_stamps() -> dict:
    if STAMPS.exists():
        return json.loads(STAMPS.read_text(encoding="utf-8"))
    return {}


def check() -> int:
    stamps = load_stamps()
    problems = []
    for vi_dir, en_dir, kind, pattern in PAIRS:
        vi_files, en_files = files(vi_dir, pattern), files(en_dir, pattern)
        for name, vi in vi_files.items():
            en = en_files.get(name)
            if en is None:
                if kind == "code" and en_dir.name == "examples_en" and not VI_LETTER.search(read(vi)):
                    continue
                problems.append(("missing", rel(en_dir / name)))
                continue
            if stamps.get(rel(vi)) != digest(vi):
                problems.append(("stale", rel(en)))
            lua = name.endswith(".lua")
            if kind == "code" and strip_code(read(vi), lua) != strip_code(read(en), lua):
                problems.append(("code", rel(en)))
            if kind == "page" and page_ids(read(vi)) != page_ids(read(en)):
                problems.append(("links", rel(en)))
        for name, en in en_files.items():
            if name not in vi_files:
                problems.append(("orphan", rel(en)))
    for kind, path in problems:
        print("%-8s %s" % (kind, path))
    print("%d problem(s)" % len(problems) if problems else "English wiki is in step.")
    return 1 if problems else 0


def stamp(paths: list) -> int:
    stamps = load_stamps()
    wanted = {Path(p).resolve() for p in paths}
    for vi_dir, en_dir, _kind, pattern in PAIRS:
        en_files = files(en_dir, pattern)
        for name, vi in files(vi_dir, pattern).items():
            if name in en_files and (not wanted or vi.resolve() in wanted):
                stamps[rel(vi)] = digest(vi)
    STAMPS.write_text(json.dumps(stamps, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print("stamped %d file(s)" % len(stamps))
    return 0


if __name__ == "__main__":
    command = sys.argv[1] if len(sys.argv) > 1 else "check"
    if command == "check":
        sys.exit(check())
    if command == "stamp":
        sys.exit(stamp(sys.argv[2:]))
    print(__doc__)
    sys.exit(2)
