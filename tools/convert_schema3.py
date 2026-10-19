#!/usr/bin/env python3
"""Convert .rsdef and .rsproj files from schema 2 to schema 3.

Schema 3 only adds optional fields, so the conversion is a version bump:
  .rsdef   "schema": 2 -> 3
  .rsproj  adds "schema": 3 and "level": "prototype" (a schema 2 project is a
           Prototype project with no wiring)

The app does the same in memory when it loads a schema 2 file, and saves it
as schema 3; this script updates files in place without reformatting them
(a text edit, so hand-formatted files keep their layout).

Usage: tools/convert_schema3.py [--check] [file-or-directory ...]
       (default: models/ demo/ resources/templates/ examples/)
--check lists what would change and exits 1 if anything would.
"""
import json
import re
import sys
from pathlib import Path

DEFAULT_DIRS = ("models", "demo", "resources/templates", "examples")


def convert_rsdef(text):
    d = json.loads(text)
    if d.get("schema") != 2:
        return None
    new, n = re.subn(r'("schema"\s*:\s*)2\b', r"\g<1>3", text, count=1)
    return new if n == 1 else None


def convert_rsproj(text):
    d = json.loads(text)
    if "schema" in d:
        return None
    m = re.match(r"\s*\{(\s*)", text)
    if not m:
        return None
    indent = m.group(1) or "\n    "
    fields = '"level": "prototype",' + indent if "level" not in d else ""
    fields += '"schema": 3,' + indent
    return text[: m.end()] + fields + text[m.end():]


def collect(args, root):
    paths = [Path(a) for a in args] or [root / d for d in DEFAULT_DIRS]
    for p in paths:
        if p.is_dir():
            yield from sorted(p.rglob("*.rsdef"))
            yield from sorted(p.rglob("*.rsproj"))
        elif p.suffix in (".rsdef", ".rsproj"):
            yield p


def main(argv):
    check = "--check" in argv
    args = [a for a in argv if a != "--check"]
    root = Path(__file__).resolve().parent.parent
    changed = 0
    for path in collect(args, root):
        text = path.read_text(encoding="utf-8")
        new = convert_rsdef(text) if path.suffix == ".rsdef" else convert_rsproj(text)
        if new is None:
            continue
        json.loads(new)  # never write a file that doesn't parse
        changed += 1
        print(("would convert " if check else "converted ") + str(path))
        if not check:
            tmp = path.with_name(path.name + ".tmp")
            tmp.write_text(new, encoding="utf-8")
            tmp.replace(path)
    print(f"{changed} file(s) {'to convert' if check else 'converted'}")
    return 1 if check and changed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
