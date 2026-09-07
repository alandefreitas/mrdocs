#!/usr/bin/env python3
"""Vary the opener of the descriptive notes on tour slides. The notes were written as "This is X." on every slide and
every highlight step; this keeps each sentence's content and changes only how it opens, by position:

  slide notes, first slide of an example      This is X.            -> Here is X.
  slide notes, a rendered page                This is the page for Y. -> The page for Y is this.  /  And the page for Y.
  slide notes, later slides of an example     This is X.            -> And here is X.
  highlight steps, first step                 This is X. / These are A and B.  -> Here is X. / Here are A and B.
  highlight steps, later steps                This is X. / These are A and B.  -> X. / A and B.   (bare, capitalised)
  highlight steps, a continuation             This is the rest of X. -> And the rest of X.

Usage: python3 tools/openers.py [--dry]   (--dry prints before -> after and writes nothing)
"""
import re, sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
PLAN = HERE / "levels" / "2-plan.yml"


def example_of(sid):
    s = re.sub(r"-(config|rendered|input|lua|cli|\d+)(-\d+)?$", "", sid)
    return re.sub(r"-(config|rendered|input|lua|cli|\d+)$", "", s)


def cap(s):
    return s[:1].upper() + s[1:]


def slide_opener(note, first, rendered, nth_rendered):
    m = re.match(r"This is (.*)", note, re.S)
    if not m:
        return None
    rest = m.group(1)
    if rendered:
        pm = re.match(r"(the page for .+?)\.\s*$", rest, re.S)
        if pm:
            return (cap(pm.group(1)) + " is this.") if nth_rendered % 2 == 0 else ("And " + pm.group(1) + ".")
    return ("Here is " if first else "And here is ") + rest


def step_opener(note, first):
    m = re.match(r"This is the rest of (.*)", note, re.S)
    if m:
        return "And the rest of " + m.group(1)
    m = re.match(r"(This is|These are) (.*)", note, re.S)
    if not m:
        return None
    plural = m.group(1) == "These are"
    if first:
        return ("Here are " if plural else "Here is ") + m.group(2)
    return cap(m.group(2))


def main():
    dry = "--dry" in sys.argv
    text = PLAN.read_text(encoding="utf-8")
    lines = text.split("\n")
    # walk the raw file: a slide starts at '    - id: X'; its notes block follows 'notes: |-' (slide) or
    # '        notes: |-' / '>-' (highlight). Only the first line of each block is touched.
    changes = []
    sid = None; prev_example = None; first = False; n_rendered = 0; kind = None; step = 0
    i = 0
    while i < len(lines):
        l = lines[i]
        m = re.match(r"^    - id: (\S+)\s*$", l)
        if m:
            sid = m.group(1); ex = example_of(sid)
            first = ex != prev_example
            if first: n_rendered = 0
            prev_example = ex; kind = None; step = 0
        m = re.match(r"^      kind: (\S+)", l)
        if m: kind = m.group(1)
        m = re.match(r"^      notes: [|>]-?\s*$", l)
        if m and kind in ("code", "rendered") and i + 1 < len(lines):
            rendered = kind == "rendered"
            new = slide_opener(lines[i + 1].strip(), first, rendered, n_rendered)
            if rendered: n_rendered += 1
            if new:
                changes.append((sid, lines[i + 1].strip(), new)); lines[i + 1] = "        " + new
        m = re.match(r"^      - lines: ", l)
        if m: step += 1
        m = re.match(r"^        notes: [|>]-?\s*$", l)
        if m and i + 1 < len(lines):
            new = step_opener(lines[i + 1].strip(), step == 1)
            if new:
                changes.append((f"{sid}/{step}", lines[i + 1].strip(), new)); lines[i + 1] = "          " + new
        i += 1
    for sid, old, new in changes:
        print(f"{sid:44s} {old}\n{'':44s} -> {new}")
    print(f"{len(changes)} notes", "(dry run, nothing written)" if dry else "written")
    if not dry:
        PLAN.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
