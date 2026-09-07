"""Gives a code block taller than the frame its highlight steps: one declaration, function or section per step, merged
while they fit in twenty lines, each with a plain sentence saying what it is. Used once on the blocks S2 names; the
plan is the source of truth afterwards, so edit the steps there.

    python3 tools/semantic_steps.py ID [ID...]        # write steps for these slides
    python3 tools/semantic_steps.py --dry ID [ID...]  # print them
"""
import pathlib, re, sys, yaml
HERE = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE / "tools"))
from pipeline import read_source, BlockDumper
LIMIT = 20


def units(code, lang):
    """(first, last, name, kind) for each top-level thing in the file, 1-based inclusive."""
    lines = code.split("\n"); n = len(lines)
    if lang in ("cpp", "c++", "c"):
        starts = []
        depth = 0; i = 0
        for i, l in enumerate(lines):
            s = l.strip()
            if depth == 0 and s and (s.startswith(("/**", "///", "//", "#", "template", "struct", "class", "enum", "namespace", "inline", "constexpr", "static", "using", "typedef")) or re.match(r"^[\w:<>*&,\s]+\s\w+\s*\(", s) or re.match(r"^\w", s)):
                if not starts or starts[-1] != i: starts.append(i)
            depth += l.count("{") - l.count("}")
        return group_by_blank(lines, kind_name_cpp)
    if lang in ("javascript", "lua", "python", "js"):
        return group_by_blank(lines, kind_name_script, lang if lang in ("lua", "python") else "js")
    if lang in ("markdown", "latex", "tex", "asciidoc", "text"):
        return group_by_blank(lines, kind_name_text, "text")
    return group_by_blank(lines, kind_name_data, "data")


def group_by_blank(lines, namer, lang="cpp"):
    """Top-level paragraphs: blank lines split only outside any brace, block or indentation, so a function body
    stays one unit; a comment block attaches to the declaration that follows it."""
    paras, cur, depth, in_comment, ns_open = [], [], 0, False, []
    for i, l in enumerate(lines, 1):
        if not l.strip():
            if cur and depth <= 0 and not in_comment: paras.append(cur); cur = []
            continue
        cur.append(i)
        if lang not in ("lua", "python"):
            if "/*" in l and "*/" not in l.split("/*", 1)[1]: in_comment = True
            if "*/" in l: in_comment = False
        if lang == "python" and l.count('"""') % 2 == 1: in_comment = not in_comment
        code = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', "", l)
        if lang == "lua":
            code = re.sub(r"--.*", "", code)
            depth += len(re.findall(r"\b(function|do|then|repeat)\b", code)) - len(re.findall(r"\b(end|until)\b", code))
            depth -= len(re.findall(r"\belseif\b.*\bthen\b", code))  # elseif's then does not open a new block
        elif lang == "python":
            depth = 1 if (l.startswith((" ", "\t")) or l.rstrip().endswith(":")) else 0
        else:
            code = re.sub(r"//.*", "", re.sub(r"/\*.*?\*/", "", code))
            if lang == "cpp" and re.match(r"^\s*(inline\s+)?namespace\b[^{]*\{\s*$", code):
                ns_open.append(depth); continue
            if lang == "cpp" and ns_open and depth == ns_open[-1] and re.match(r"^\}\s*;?\s*$", code.strip()):
                ns_open.pop(); continue
            depth += code.count("{") - code.count("}") + code.count("(") - code.count(")") + code.count("[") - code.count("]")
    if cur: paras.append(cur)
    out = []
    for p in paras:
        a, b = p[0], p[-1]
        text = "\n".join(lines[a - 1:b])
        # a comment block alone attaches to the next paragraph
        if out and out[-1][3] == "comment" and out[-1][1] == a - 2:
            pa, _, _, _ = out.pop(); a = pa; text = "\n".join(lines[a - 1:b])
        # closing braces alone (the end of a namespace) attach to the paragraph before them
        tail = {"lua": r"--.*", "python": r"#.*"}.get(lang, r"//.*")
        if out and lang not in ("text",) and all(re.fullmatch(r"\s*[}\]);]*\s*(" + tail + ")?", x) for x in text.split("\n")):
            pa, _, pn, pk = out.pop(); out.append((pa, b, pn, pk)); continue
        # a namespace opening alone attaches to the paragraph that follows it
        if lang == "cpp" and re.fullmatch(r"\s*(inline\s+)?namespace\b[^{]*\{\s*", re.sub(r"//.*", "", text)):
            out.append((a, b, "", "namespace-open")); continue
        if out and out[-1][3] == "namespace-open":
            pa, _, _, _ = out.pop(); a = pa; text = "\n".join(lines[a - 1:b])
        name, kind = namer(text)
        out.append((a, b, name, kind))
    return out


KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "catch", "else", "do", "static_assert", "decltype", "operator", "defined", "alignof", "noexcept"}


def kind_name_cpp(text):
    body = re.sub(r"//.*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.S))  # the words in a comment are not declarations
    body = re.sub(r"template\s*<[^<>]*(?:<[^<>]*>[^<>]*)*>", "", body)
    if not body.strip(): return ("", "comment")
    stripped = re.sub(r"^\s*(inline\s+)?namespace\b[^{\n]*\{\s*\n", "", body)
    if stripped.strip(): body = stripped
    m = re.search(r"\b(struct|class|enum(?: class)?|namespace|union)\s+(\w+)", body)
    if m: return (m.group(2), m.group(1).split()[0])
    if re.search(r"#\s*ifndef\s+(\w+)\s*\n\s*#\s*define\s+\1\b", body): return ("include", "guard")
    m = re.search(r"#\s*define\s+(\w+)", body)
    if m: return (m.group(1), "macro")
    if re.fullmatch(r"(\s*#\s*include\b[^\n]*\n?)+", body): return ("", "includes")
    m = re.search(r"^\s*#\s*(if|ifdef|ifndef|else|endif|include)", body, re.M)
    if m and not re.search(r"\w+\s*\(", body): return (m.group(1), "preprocessor")
    m = re.search(r"(?:constexpr|inline)\s+[\w:<>]+\s+(\w+)\s*=", body)
    if m: return (m.group(1), "variable")
    m = re.search(r"operator\s*([^\s(]+)\s*\(", body)
    if m: return (f"operator {m.group(1)}", "function")
    for m in re.finditer(r"([~\w]+)\s*\(", body):
        if m.group(1) not in KEYWORDS and not m.group(1).isdigit(): return (m.group(1), "function")
    m = re.search(r"\b(\w+)\s*;", body)
    if m: return (m.group(1), "declaration")
    return ("", "code")


def kind_name_script(text):
    m = re.search(r"mrdocs\.register_(transform|generator)\(\s*[\"']([^\"']+)", text)
    if m: return (m.group(2), f"register {m.group(1)}")
    if re.match(r"^\s*(//|--|#|/\*|\*)", text) and not re.search(r"^\s*(?!//|--|#|/\*|\*)\S", text, re.M): return ("", "comment")
    m = re.search(r"(?:^|\n)\s*(?:local\s+)?function\s+([\w.:]+)|(?:^|\n)\s*(?:const|let|var)\s+(\w+)\s*=\s*(?:function|\(|async)|(?:^|\n)def\s+(\w+)", text)
    if m: return (next(g for g in m.groups() if g), "function")
    m = re.search(r"^\s*(?:const|let|var|local)\s+([\w, ]+)\s*=", text, re.M)
    if m: return (m.group(1).strip(), "variable")
    if re.fullmatch(r"(\s*local\s+[\w, ]+\s*\n?)+", text): return ("", "forward declarations")
    m = re.search(r"^\s*(?:import|require|from)\b.*", text, re.M)
    if m: return ("imports", "imports")
    m = re.search(r"^\s*(\w+)\s*=", text, re.M)
    if m: return (m.group(1), "variable")
    return ("", "code")


def kind_name_text(text):
    m = re.search(r"^\s*(?:#{1,2}(?!#)|={1,2}(?!=)|\\(?:sub)?section\*?\{)\s*([^\n}]+)", text, re.M)
    if m: return (re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", m.group(1)).strip(), "section")
    if re.search(r"\\end\{document\}", text): return ("", "end")
    return ("", "text")


def kind_name_data(text):
    m = re.search(r"\{\{#>?\s*([\w/.-]+)\s*([^}]*)\}\}", text)
    if m: return (m.group(1), "block")
    m = re.search(r"\{\{>\s*([\w/.-]+)", text)
    if m: return (m.group(1).split("/")[-1], "partial")
    m = re.search(r"^\s*[\"']?([\w-]+)[\"']?\s*:", text, re.M)
    if m: return (m.group(1), "key")
    m = re.search(r"^\s*<(\w+)", text, re.M)
    if m: return (m.group(1), "element")
    return ("", "code")


def sentence(names_kinds):
    """One plain sentence for a step made of these units."""
    parts = []
    for name, kind in names_kinds:
        if kind == "comment" or (not name and kind not in ("includes", "forward declarations", "guard", "end")): continue
        label = {"struct": f"the {name} struct", "class": f"the {name} class", "enum": f"the {name} enum", "namespace": f"the {name} namespace",
                 "union": f"the {name} union", "macro": f"the {name} macro", "preprocessor": "the preprocessor guard", "variable": f"the {name} variable",
                 "function": f"the {name} function", "declaration": f"the {name} declaration", "register transform": f"the registration of the {name} transform",
                 "register generator": f"the registration of the {name} generator", "imports": "the imports", "section": f"the {name} section",
                 "key": f"the {name} entry", "element": f"the {name} element", "text": "this part", "code": "this part",
                 "partial": f"the call to the {name} partial", "block": f"the {name} block", "guard": "the include guard",
                 "includes": "the includes", "forward declarations": "the forward declarations", "end": "the end of the document"}[kind]
        if label not in parts: parts.append(label)
    if not parts: return None
    if len(parts) == 1: return ("These are " if parts[0] in ("the imports", "the includes", "the forward declarations") else "This is ") + parts[0] + "."
    return "These are " + ", ".join(parts[:-1]) + " and " + parts[-1] + "."


def steps_for(code, lang):
    us = units(code, lang)
    steps, cur, last = [], [], [None]
    def rng(a, b): return f"{a}-{b}" if b > a else str(a)
    def flush():
        if cur:
            a, b = cur[0][0], cur[-1][1]
            said = sentence([(u[2], u[3]) for u in cur])
            if said is None: said = f"This is the rest of {last[0]}." if last[0] else "This is the rest of the file."
            else: last[0] = next(u[2] for u in cur[::-1] if u[2]) if any(u[2] for u in cur) else last[0]
            steps.append({"lines": rng(a, b), "notes": said})
    for u in us:
        a, b = u[0], u[1]
        if cur and (b - cur[0][0] + 1) > LIMIT:
            flush(); cur = []
        cur.append(u)
        # a single unit longer than the limit is cut into pieces of the limit
        if b - a + 1 > LIMIT:
            flush(); cur = []
            # replace the last step by pieces
            steps.pop()
            for pa in range(a, b + 1, LIMIT):
                pb = min(pa + LIMIT - 1, b)
                steps.append({"lines": rng(pa, pb), "notes": (sentence([(u[2], u[3])]) or "This is the start of the file.") if pa == a else f"This is the rest of {u[2] or 'it'}."})
            if u[2]: last[0] = u[2]
    flush()
    if lang == "json" and steps and units(code, lang):
        first = units(code, lang)[0][2] or "symbols"
        steps[0]["notes"] = f"This is the start of the {first} array, one object per symbol."
        for st in steps[1:]: st["notes"] = "This continues the array."
    return steps


def main():
    dry = "--dry" in sys.argv
    ids = [a for a in sys.argv[1:] if not a.startswith("--")]
    P = HERE / "levels" / "2-plan.yml"; T = P.read_text(); story = yaml.safe_load(T)
    slides = {s["id"]: s for a in story["acts"] for b in a["beats"] for s in b["slides"]}
    END = re.compile(r"^(    - id: |  - id: |- id: |  #|[A-Za-z#])", re.M)
    for sid in ids:
        s = slides[sid]
        if s.get("steps"):
            m = re.search(rf"^    - id: {re.escape(sid)}\n", T, re.M); e = END.search(T, m.end()).start()
            new_slides = []
            for k, st in enumerate(s["steps"]):
                lang = st.get("language", "cpp")
                ns = {"id": sid if k == 0 else f"{sid}-{lang}", "kind": "code", "on_screen": "", "assertion": s["assertion"],
                      "notes": s.get("notes") if k == 0 else (st.get("notes") or f"And the same in {lang.capitalize()}."),
                      "title": st.get("title"), "file": st.get("file"), "language": lang}
                if st.get("include"): ns["include"] = st["include"]
                if st.get("code") is not None and not st.get("file"): ns["code"] = st["code"]; ns.pop("file")
                code = ns.get("code") if ns.get("code") is not None else read_source(ns["file"], ns.get("include"))
                ns["highlights"] = [{"lines": x["lines"], "notes": x["notes"]} for x in steps_for(code, lang)]
                print(f"== {ns['id']} ({len(code.split(chr(10)))} lines) -> {len(ns['highlights'])} steps")
                for x in ns["highlights"]: print(f"   {x['lines']:>9s}  {x['notes']}")
                new_slides.append(ns)
            if not dry:
                out = yaml.dump(new_slides, Dumper=BlockDumper, sort_keys=False, allow_unicode=True, width=110)
                text = "".join(("    " + l + "\n") if l.strip() else "\n" for l in out.rstrip("\n").split("\n"))
                T = T[:m.start()] + text + T[e:]
            continue
        code = s.get("code") if s.get("code") is not None else read_source(s["file"], s.get("include"))
        steps = steps_for(code, s.get("language", "cpp"))
        print(f"== {sid} ({len(code.split(chr(10)))} lines) -> {len(steps)} steps")
        for st in steps: print(f"   {st['lines']:>9s}  {st['notes']}")
        if dry: continue
        m = re.search(rf"^    - id: {re.escape(sid)}\n", T, re.M); e = END.search(T, m.end()).start(); body = T[m.start():e]
        body = re.sub(r"^      highlights:\n(?:      - .*\n(?:        .*\n)*)+", "", body, flags=re.M)
        hl = "      highlights:\n" + "".join(f"      - lines: '{st['lines']}'\n        notes: >-\n          {st['notes']}\n" for st in steps)
        body = re.sub(r"^(      language: .*\n)", lambda mm: mm.group(1) + hl, body, count=1, flags=re.M)
        T = T[:m.start()] + body + T[e:]
    if not dry:
        P.write_text(T); print("written")


if __name__ == "__main__":
    main()
