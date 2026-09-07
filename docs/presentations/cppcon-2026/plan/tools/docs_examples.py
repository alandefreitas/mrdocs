"""Transcribes every example on the toured docs pages into slides: the source as written, then the page Mr.Docs
generates from it.

For each page under Commands, Configuration, Generators and Extensions (the reference pages excepted, by the chapters'
decision), the page is read top down. Every source block becomes a code slide (its caption or file name as the
frame's title); every rendered preview becomes a `rendered` slide whose fragment is produced here by running the
release binary on the same fixture the docs build uses. Consecutive source blocks that are the same file in two
scripting languages become one code slide with a step per language.

    python3 tools/docs_examples.py            # fragments to assets/rendered/docs/, slides to build/docs-slides.yml
    python3 tools/docs_examples.py --no-build # slides only, from fragments already built
    python3 tools/docs_examples.py --splice   # also replace the example slides of the seven tour beats in the plan

The splice keeps the framing slides (the filters and quadrant diagrams, the extension-points diagram, the Antora
picture) and puts each beat's transition words on its first generated slide. The plan is the source of truth in
between: run the splice again only when the docs' examples change.
"""
import pathlib, re, shutil, subprocess, sys, html as htmllib
import yaml

HERE = pathlib.Path(__file__).resolve().parent.parent           # plan/
DECK = HERE.parent
REPO = DECK.parent.parent.parent
PAGES = REPO / "docs" / "modules" / "ROOT" / "pages"
MRDOCS = REPO / "install" / "release-macos" / "bin" / "mrdocs"
ADDONS = REPO / "install" / "release-macos" / "share" / "mrdocs" / "addons"
SCRATCH = HERE / "build" / "docs-examples"
OUT = DECK / "assets" / "rendered" / "docs"
BUILD = "--no-build" not in sys.argv

# beat id -> (section label, [(page file, nav title)])
BEATS = {
    "commands": ("Commands", [("commands/index.adoc", "Overview"), ("commands/blocks.adoc", "Block Commands"),
                              ("commands/metadata.adoc", "Metadata Commands"), ("commands/inlines.adoc", "Inline Commands")]),
    "configuration": ("Configuration", [("configuration/index.adoc", "Overview"), ("configuration/inputs.adoc", "Inputs"),
                                        ("configuration/filters.adoc", "Filters"), ("configuration/extraction.adoc", "Extraction"),
                                        ("configuration/output.adoc", "Output"), ("configuration/diagnostics.adoc", "Diagnostics")]),
    "generators": ("Generators", [("generators/index.adoc", "Overview"), ("generators/html.adoc", "HTML"), ("generators/adoc.adoc", "AsciiDoc"),
                                  ("generators/json.adoc", "JSON"), ("generators/xml.adoc", "XML"), ("generators/noop.adoc", "No-op")]),
    "ext-hooks": ("Extensions", [("extensions/handlebars-extensions.adoc", "Handlebars Extensions"),
                                 ("extensions/data-driven-generators.adoc", "Data-Driven Generators")]),
    "ext-conventions": ("Extensions", [("extensions/corpus-transforms.adoc", "Corpus Transforms")]),
    "ext-generators": ("Extensions", [("extensions/script-generators.adoc", "Script Generators")]),
    "ext-integration": ("Extensions", [("extensions/antora.adoc", "Antora Extensions"), ("extensions/as-library.adoc", "Mr.Docs as a Library")]),
}
LANG = {"js": "javascript", "adoc-handlebars": "handlebars", "": "text", "c++": "cpp"}
STAR = "vec2.described.hpp"          # the star moment: the generator that writes code
IDEA = "The macro layer is gone. The documented C++ is the compiled C++."


# ---------------------------------------------------------------- includes
def resolve(target):
    if target.startswith("example$snippets/"):
        return REPO / "tests" / "golden" / "fixtures" / "snippets" / target[len("example$snippets/"):]
    if target.startswith("example$examples/"):
        return REPO / "examples" / target[len("example$examples/"):]
    if target.startswith("example$"):
        return REPO / "docs" / "modules" / "ROOT" / "examples" / target[len("example$"):]
    if target.startswith("partial$"):
        return REPO / "docs" / "modules" / "ROOT" / "partials" / target[len("partial$"):]
    raise ValueError(target)


TAG = re.compile(r"^\s*(?://|#|--|<!--)\s*(tag|end)::([\w-]+)\[\]")


def included(path, attrs):
    """The text of an included file after Asciidoctor's tag, tags, lines and indent attributes."""
    text = path.read_text(encoding="utf-8")
    lines = text.split("\n")
    a = dict(kv.split("=", 1) for kv in attrs.split(",") if "=" in kv) if attrs else {}
    tags = a.get("tag") or a.get("tags")
    if tags:
        wanted, excluded = set(), set()
        for t in tags.split(";"):
            (excluded if t.startswith("!") else wanted).add(t.lstrip("!"))
        out, active = [], [] if wanted else [None]
        for ln in lines:
            m = TAG.match(ln)
            if m:
                if m.group(1) == "tag": active.append(m.group(2))
                else:
                    if m.group(2) in active: active.remove(m.group(2))
                continue
            cur = set(active) - {None}
            if wanted and not (cur & wanted): continue
            if excluded and (cur & excluded): continue
            out.append(ln)
        lines = out
    else:
        lines = [ln for ln in lines if not TAG.match(ln)]
    if "lines" in a:
        lo, _, hi = a["lines"].partition("..")
        lo = int(lo or 1); hi = int(hi) if hi else len(lines)
        lines = lines[lo - 1:hi]
    if a.get("indent") == "0":
        pad = min((len(l) - len(l.lstrip()) for l in lines if l.strip()), default=0)
        lines = [l[pad:] for l in lines]
    while lines and not lines[0].strip(): lines.pop(0)
    while lines and not lines[-1].strip(): lines.pop()
    return "\n".join(lines)


# ---------------------------------------------------------------- prose
def plain(s):
    """Asciidoc inline markup to speech."""
    s = re.sub(r"xref:[^\[\]]+\[([^\]]*)\]", r"\1", s)
    s = re.sub(r"(?:link:|https?://)[^\s\[\]]+\[([^\],]*?)\^?(?:,[^\]]*)?\]", r"\1", s)
    s = re.sub(r"pass:\[([^\]]*)\]", r"\1", s)
    s = re.sub(r"cpp:([^\[\]]+)\[\]", r"\1", s)
    s = re.sub(r"cpp:[^\[\]]+\[([^\]]*)\]", r"\1", s)
    s = re.sub(r"<<[^,>]+,([^>]*)>>", r"\1", s)
    s = s.replace("{cpp}", "C++").replace("&period;", ".").replace("&colon;", ":")
    s = re.sub(r"`([^`]*)`", r"\1", s)
    s = re.sub(r"(?<![\w+])\+([^+\n]+)\+(?![\w+])", r"\1", s)
    s = re.sub(r"(?<!\w)\*([A-Za-z][^*\n]*?)\*(?!\w)", r"\1", s)
    s = re.sub(r"(?<!\w)_(\S[^_\n]*?)_(?!\w)", r"\1", s)
    s = re.sub(r"\[#[^\]]*\]", "", s)
    return re.sub(r"\s+", " ", s).strip()


# ---------------------------------------------------------------- page parsing
class Item(dict):
    pass


def parse(path):
    """The page as a flat list of items: heading, prose, source, preview, in order."""
    lines = path.read_text(encoding="utf-8").split("\n")
    items, i, caption, lang, tab, para = [], 0, None, None, None, []
    def flush():
        nonlocal para
        if para:
            items.append(Item(kind="prose", text=plain(" ".join(para))))
            para = []
    while i < len(lines):
        ln = lines[i]
        if re.match(r"^=+ ", ln):
            flush()
            m = re.match(r"^(=+) (.*)", ln)
            items.append(Item(kind="heading", level=len(m.group(1)), text=plain(m.group(2))))
        elif ln.startswith(":") or ln.startswith("//"):
            pass
        elif re.match(r"^\.[^.\s]", ln):
            flush(); caption = plain(ln[1:])
        elif re.match(r"^\[source(,|\])", ln):
            flush()
            m = re.match(r"^\[source,?([^\],]*)", ln); lang = LANG.get(m.group(1).strip(), m.group(1).strip()) or "text"
        elif ln.startswith("[.adoc-preview]"):
            flush()
            i += 1  # the ==== fence
            fence = lines[i]; i += 1
            body = []
            while i < len(lines) and lines[i] != fence:
                body.append(lines[i]); i += 1
            inc = next((re.match(r"include::([^\[]+)\[([^\]]*)\]", b) for b in body if b.startswith("include::")), None)
            if inc:
                items.append(Item(kind="preview", target=inc.group(1), attrs=inc.group(2), caption=caption))
            caption = None
        elif ln == "----" and lang is not None:
            body = []; i += 1
            while i < len(lines) and lines[i] != "----":
                body.append(lines[i]); i += 1
            inc = re.match(r"include::([^\[]+)\[([^\]]*)\]", body[0]) if body and body[0].startswith("include::") else None
            if inc:
                code = included(resolve(inc.group(1)), inc.group(2)); file = inc.group(1)
                repo_file = str(resolve(inc.group(1)).relative_to(REPO)); attrs = inc.group(2) or None
            else:
                code = "\n".join(body).strip("\n"); file = None; repo_file = None; attrs = None
            items.append(Item(kind="source", lang=lang, code=code, file=file, caption=caption, tab=tab, repo_file=repo_file, attrs=attrs))
            caption = None; lang = None
        elif ln in ("[tabs]", "======"):
            flush(); tab = None if ln == "======" and tab is not None and items and items[-1]["kind"] != "source" else tab
            if ln == "======": tab = None
        elif re.match(r"^[A-Za-z][\w+ ]*::$", ln):
            flush(); tab = ln[:-2]
        elif ln.strip() in ("+", "--", "====") or re.match(r"^\[(NOTE|TIP|WARNING|IMPORTANT|CAUTION)\]", ln) or re.match(r"^(NOTE|TIP|WARNING|IMPORTANT):", ln):
            flush()
            if re.match(r"^\[(NOTE|TIP|WARNING|IMPORTANT|CAUTION)\]", ln) and i + 1 < len(lines) and lines[i + 1] == "====":
                i += 2
                while i < len(lines) and lines[i] != "====": i += 1
        elif re.match(r"^(\*+|\.+|-) ", ln) or re.match(r"^[^\s].*::$", ln) or ln.startswith("|") or ln.startswith("[") or ln.startswith("image::"):
            flush()  # lists, tables, definition lists and block attributes are not spoken
        elif ln.strip() == "":
            flush()
        else:
            para.append(ln.strip())
        i += 1
    flush()
    return items


# ---------------------------------------------------------------- building a preview
def fixture_config(adoc):
    """How to build the reference a preview shows: (directory to copy, config text, output filename)."""
    d = adoc.parent
    if adoc.is_relative_to(REPO / "examples"):
        cfg = yaml.safe_load((d / "mrdocs.yml").read_text())
        gens = cfg.get("generator", "adoc")
        gens = [g for g in (gens if isinstance(gens, list) else [gens]) if g not in ("adoc", "html", "xml", "json", "noop")]
        cfg["generator"] = ["html"] + gens if gens else "html"
        cfg["multipage"] = False
        cfg["output"] = "out-html"
        cfg["addons"] = str(ADDONS)
        return (d, None), yaml.safe_dump(cfg, sort_keys=False), "out-html/reference.html"
    stem = adoc.stem
    cfg = {"source-root": ".", "input": ["."], "show-namespaces": False, "generator": "html", "multipage": False,
           "output": "out-html", "addons": str(ADDONS)}
    if (d / f"{stem}.yml").exists():
        extra = yaml.safe_load((d / f"{stem}.yml").read_text()) or {}
        cfg.update(extra)
        cfg["generator"] = "html"  # the deck shows every preview rendered, as the docs do, whatever format the example picks
    # a folder shared by many one-file fixtures: copy only this fixture's files
    shared = len(list(d.glob("*.adoc"))) > 1
    return (d, [f for f in d.iterdir() if f.stem == stem]) if shared else (d, None), yaml.safe_dump(cfg, sort_keys=False), "out-html/reference.html"


SYMBOL = re.compile(r'<div class="symbol">.*?<!-- /symbol -->', re.S)


def build_preview(target, attrs, out_name):
    adoc = resolve(target)
    out = OUT / f"{out_name}.html"
    if not BUILD:
        return out.exists(), ""
    (src, only), cfg, result = fixture_config(adoc)
    work = SCRATCH / out_name
    shutil.rmtree(work, ignore_errors=True)
    if only is None:
        shutil.copytree(src, work, ignore=shutil.ignore_patterns("__pycache__", "out-html", "*.adoc", "exported"))
    else:
        work.mkdir(parents=True)
        for f in only:
            if f.suffix != ".adoc": shutil.copy(f, work / f.name)
    (work / "mrdocs.yml").write_text(cfg)
    r = subprocess.run([str(MRDOCS), "--config", str(work / "mrdocs.yml")], capture_output=True, text=True, cwd=work)
    page = work / result
    if not page.exists():  # several generators write into one folder each
        page = work / "out-html" / "html" / "reference.html"
    if not page.exists():
        return False, r.stderr[-600:]
    blocks = SYMBOL.findall(page.read_text(encoding="utf-8"))
    others = [b for b in blocks if 'id="index"' not in b]
    keep = others if len(others) <= 1 else blocks
    frag = "\n".join(keep)
    frag = re.sub(r'<div class="footer">.*?</div>', "", frag, flags=re.S)
    OUT.mkdir(parents=True, exist_ok=True)
    out.write_text(frag + "\n", encoding="utf-8")
    return True, ""


# ---------------------------------------------------------------- slides
def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def chunks(code):
    """Highlight ranges for a long block: runs of at most eight lines, split at blank lines where possible."""
    lines = code.split("\n")
    if len(lines) <= 12:
        return None
    out, start = [], 1
    n = len(lines)
    while start <= n:
        end = min(start + 7, n)
        if end < n:
            for k in range(end, start + 2, -1):
                if not lines[k - 1].strip():
                    end = k - 1; break
        out.append(f"{start}-{end}" if end > start else str(start))
        start = end + 1
    return out


def parts(code, limit=40):
    """A long file as consecutive parts of at most `limit` lines, split at blank lines where possible."""
    lines = code.split("\n")
    if len(lines) <= limit:
        return [code]
    out, start = [], 0
    while start < len(lines):
        end = min(start + limit, len(lines))
        if end < len(lines):
            for k in range(end, start + limit // 2, -1):
                if not lines[k - 1].strip():
                    end = k; break
        out.append("\n".join(lines[start:end]).strip("\n"))
        start = end
    return out


def title_of(src):
    if src["caption"]:
        return src["caption"]
    if src["file"]:
        return pathlib.PurePosixPath(src["file"]).name
    return None


def first_words(text, n=70):
    """The opening sentences of a paragraph, whole, up to about n words."""
    out, count = [], 0
    for sent in re.split(r"(?<=[.!?])\s+", text):
        out.append(sent); count += len(sent.split())
        if count >= n: break
    return " ".join(out)


def emit(beat, section, page_file, page_title):
    """Slides for one docs page."""
    items = parse(PAGES / page_file)
    bc = f"{section} / {page_title}"
    pslug = slug(page_file.replace(".adoc", ""))
    slides, n = [], 0
    pending_prose, opened = [], False
    heading = page_title
    section_open = True   # the first example after a heading gets the section's sentence; the rest get a cue

    def notes_for(cue):
        """The page's opening sentence on its first slide, a section's opening sentence on the section's first slide,
        and the cue alone on every other slide: an example is on screen for the seconds its sentence takes."""
        nonlocal pending_prose, opened, section_open
        text = pending_prose[0].strip() if pending_prose else ""
        pending_prose = []
        if not opened:  # the page's first sentence, said once
            opened = True; section_open = False
            return f"{first_words(text, 30) if text else cue}"
        if section_open:  # the section is named as its first example appears; the docs' sentence is for the reader
            section_open = False
            return f"{heading}."
        return f"{cue}"

    def sid(stem):
        nonlocal n
        n += 1
        return f"{pslug}-{n:02d}-{slug(stem)[:40]}"

    i = 0
    while i < len(items):
        it = items[i]
        if it["kind"] == "heading":
            if it["level"] >= 2: heading = it["text"]
            pending_prose = []; section_open = True
        elif it["kind"] == "prose":
            if not pending_prose: pending_prose.append(it["text"])
        elif it["kind"] == "source":
            # the same file in two scripting languages becomes one slide with a step per language
            group = [it]
            while i + 1 < len(items) and items[i + 1]["kind"] == "source" and items[i + 1]["tab"] and it["tab"] \
                    and items[i + 1]["tab"] != it["tab"] and items[i + 1]["lang"] != it["lang"] \
                    and (items[i + 1]["file"] or "").rsplit(".", 1)[0] == (it["file"] or "").rsplit(".", 1)[0]:
                i += 1; group.append(items[i])
            t = title_of(it)
            s = {"id": sid(t or heading), "kind": "code", "on_screen": "",
                 "assertion": f"{t or heading}: the example from the {page_title} page, as written in the docs.",
                 "notes": notes_for(f"{t or heading}.")}
            def source(node, src, lines=None):
                """The code as a pointer into the repository when the docs include a file, typed only when the docs typed it."""
                if src["repo_file"]:
                    node["file"] = src["repo_file"]
                    inc = [src["attrs"]] if src["attrs"] else []
                    if lines: inc.append(f"lines={lines[0]}..{lines[1]}")
                    if inc: node["include"] = ",".join(inc)
                else:
                    node["code"] = src["code"]
                return node

            def step(title, lang, src, cue, lines=None, code=None):
                st = {"title": title, "language": lang, "notes": cue}
                if code is not None and not src["repo_file"]:
                    st["code"] = code
                    return st
                return source(st, src, lines)

            def part_ranges(code):
                """(first, last) line numbers of each part of a long file, on the included text."""
                out, start = [], 1
                for c in parts(code):
                    n = len(c.split("\n")); out.append((start, start + n - 1)); start += n
                    while start <= len(code.split("\n")) and not code.split("\n")[start - 1].strip(): start += 1
                return out

            if len(group) > 1:
                s["steps"] = []
                for g in group:
                    st = step(title_of(g) or g["tab"], g["lang"], g, f"{g['tab']}.")
                    s["steps"].append(st)
                s["motion"] = {"kind": "auto-animate", "group": s["id"], "shared": ["code"]}
            else:
                if t: s["title"] = t
                source(s, it); s["language"] = it["lang"]
            slides.append(s)
        elif it["kind"] == "preview":
            name = f"{pslug}-{slug(pathlib.PurePosixPath(it['target']).stem)}"
            ok, err = build_preview(it["target"], it["attrs"], name)
            if not ok:
                print(f"  FAIL preview {it['target']}: {err.strip()[-300:]}", file=sys.stderr)
            s = {"id": sid(f"{pathlib.PurePosixPath(it['target']).stem}-rendered"), "kind": "rendered", "on_screen": "",
                 "doc_html": f"assets/rendered/docs/{name}.html",
                 "assertion": f"The page Mr.Docs generates from the {heading} example.",
                 "notes": notes_for("Rendered.")}
            slides.append(s)
        i += 1
    if not slides and pending_prose:  # a page with nothing to show still gets its stop
        slides.append({"id": sid("overview"), "kind": "line", "on_screen": page_title,
                       "assertion": f"The {page_title} page.", "notes": notes_for(page_title)})
    return slides


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    result = {}
    for beat, (section, pages) in BEATS.items():
        acc = []
        for page_file, title in pages:
            sl = emit(beat, section, page_file, title)
            print(f"{beat:16s} {page_file:44s} {len(sl):3d} slides")
            acc += sl
        result[beat] = acc
    # the star moment and the idea, on the slides that carry them
    for beat, sl in result.items():
        for s in sl:
            if s.get("title") == STAR:
                s["star_moment"] = True
                s["notes"] += "\n\n" + "The output is code: the Boost.Describe annotations, written from the same corpus that writes the reference pages, and never out of sync with the struct. The tool sees what the compiler sees, so it can write code."
            if s["kind"] == "rendered" and "sfinae" in s["id"]:
                s["restates_idea"] = True
                s["notes"] += "\n\n" + IDEA
    (HERE / "build").mkdir(exist_ok=True)
    (HERE / "build" / "docs-slides.yml").write_text(yaml.safe_dump(result, sort_keys=False, allow_unicode=True, width=110))
    print("slides:", sum(len(v) for v in result.values()), "->", HERE / "build" / "docs-slides.yml")
    return result


def splice(result):
    """Replace the example slides of the seven tour beats in levels/2-plan.yml with the generated ones."""
    import re
    sys.path.insert(0, str(HERE / "tools"))
    from pipeline import BlockDumper
    P = HERE / "levels" / "2-plan.yml"; T = P.read_text()
    plan = yaml.safe_load(T)
    tw = {b["id"]: b["transition_words"] for a in plan["acts"] for b in a["beats"]}
    beat_end = re.compile(r"^(  - id: |- id: |[A-Za-z#])", re.M)
    slide_end = re.compile(r"^(    - id: |  - id: |- id: |  #|[A-Za-z#])", re.M)

    def beat_span(bid):
        m = re.search(rf"^  - id: {re.escape(bid)}\n", T, re.M); assert m, bid
        return m.start(), beat_end.search(T, m.end()).start()

    def slide_text(sid):
        m = re.search(rf"^    - id: {re.escape(sid)}\n", T, re.M); assert m, sid
        return T[m.start():slide_end.search(T, m.end()).start()]

    def dump(slides):
        out = yaml.dump(slides, Dumper=BlockDumper, sort_keys=False, allow_unicode=True, width=110)
        return "".join(("    " + l + "\n") if l.strip() else "\n" for l in out.rstrip("\n").split("\n"))

    def with_words(bid, slides):
        w = tw[bid]; first = slides[0]
        bc, _, rest = first["notes"].partition("\n\n")
        if w.lower() not in first["notes"].lower():
            first["notes"] = f"{w}. {rest}" if not w.endswith((".", "?")) else f"{w} {rest}"
        return slides

    keep = {k: slide_text(k) for k in ("filters", "quadrant", "extension-points", "antora")}

    def replace_beat_slides(bid, new_text):
        nonlocal T
        a, b = beat_span(bid); body = T[a:b]
        m = re.search(r"^    slides:\n", body, re.M); assert m, bid
        head = body[:m.start()]
        cm = re.search(r"^  #.*\n", head, re.M); trailing = ""
        if cm:  # a comment at two spaces inside the beat moves in front of the next one
            trailing = cm.group(0); head = head[:cm.start()] + head[cm.end():]
        T = T[:a] + head + "    slides:\n" + new_text + trailing + T[b:]

    replace_beat_slides("commands", dump(with_words("commands", result["commands"])))
    cfg = with_words("configuration", result["configuration"])
    i = next(i for i, s in enumerate(cfg) if s["id"].startswith("configuration-filters-"))
    replace_beat_slides("configuration", dump(cfg[:i]) + keep["filters"] + dump(cfg[i:]))
    replace_beat_slides("generators", keep["quadrant"] + dump(result["generators"]))
    replace_beat_slides("ext-hooks", keep["extension-points"] + dump(result["ext-hooks"]))
    replace_beat_slides("ext-conventions", dump(with_words("ext-conventions", result["ext-conventions"])))
    replace_beat_slides("ext-generators", dump(with_words("ext-generators", result["ext-generators"])))
    replace_beat_slides("ext-integration", keep["antora"] + dump(result["ext-integration"]))
    P.write_text(T)
    print("spliced into", P)


if __name__ == "__main__":
    generated = main()
    if "--splice" in sys.argv:
        splice(generated)
