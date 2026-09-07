"""Lights the lines of each example that use the feature its docs section is about.

The tour flips through the docs' examples one per phrase, so the viewer needs to see at once which lines matter.
For every run of code slides that ends in a `rendered` slide, this adds one static highlight per code slide (or per
step): the doc-comment commands and markup on the Commands pages, the option lines in a mrdocs.yml, the lines a
configuration option acts on, the registration and output calls in a script. Slides that already carry highlights
are left alone.

    python3 tools/feature_highlights.py          # edit levels/2-plan.yml in place
    python3 tools/feature_highlights.py --dry    # print what would be lit
"""
import pathlib, re, sys
import yaml

HERE = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE / "tools"))
from pipeline import read_source

PLAN = HERE / "levels" / "2-plan.yml"
DRY = "--dry" in sys.argv

# option key -> what its lines in the C++ look like
OPTION_TOKENS = {
    "sfinae": r"enable_if|requires",
    "auto-function-objects": r"operator\(\)|constexpr",
    "extract-private": r"\bprivate\b",
    "extract-private-bases": r":\s*private",
    "show-enum-constants": r"\benum\b",
    "show-namespaces": r"\bnamespace\b",
    "inherit-base-members": r"^\s*(struct|class)\s+\w+\s*:\s*\w|^\s*:\s*(public|protected|private)\b",
    "extract-all": "UNDOCUMENTED",
    "auto-brief": "UNDOCUMENTED",
    "auto-function-metadata": "UNDOCUMENTED",
    "auto-relates": "RELATED",
    "extract-all-macros": r"#define",
    "include-macros": r"#define",
    "exclude-macros": r"#define",
    "macros-guard": r"#if|#define",
    "overloads": None,  # repeated names, computed below
    "implementation-defined": r"detail|impl",
    "see-below": r"@seebelow|see_below",
}
# inputs of the extension examples: what the script looks for in them
INPUT_TOKENS = {
    "brief-from-name": r"\bis_\w+",
    "parse-format-relates": r"\b(parse|format)_\w+",
    "subclass-tree": r"^\s*(struct|class)\s+\w+\s*:\s*\w",
    "project-conventions": r"///\s*\S{0,3}\s*$|/\*\*\s*\S{0,12}\s*\*/",
    "corpus-transforms-16-simple": "UNDOCUMENTED",
}
# the five landing-page snippets: the feature each one shows
SNIPPETS = {
    "snippet-terminate": r"@note|\[\[|noexcept",
    "snippet-distance": r"@param|@return",
    "snippet-is-prime": r"@\w+",
    "snippet-sqrt": r"enable_if|\\f\$|\$",
    "snippet-abs": r"operator\(\)|constexpr",
}


def ranges(nums):
    """1-based line numbers as reveal ranges: 2,4-6."""
    nums = sorted(set(nums)); out = []
    while nums:
        a = b = nums.pop(0)
        while nums and nums[0] == b + 1:
            b = nums.pop(0)
        out.append(f"{a}-{b}" if b > a else str(a))
    return ",".join(out)


def lines_matching(code, pattern):
    return [i for i, l in enumerate(code.split("\n"), 1) if re.search(pattern, l)]


def doc_command_lines(code):
    """Lines of a doc comment that carry a command; a @code block counts whole."""
    lines = code.split("\n"); out, in_code = [], False
    for i, l in enumerate(lines, 1):
        if re.search(r"@(code|verbatim)\b", l): in_code = True
        if in_code or re.search(r"(?<![\w@])@[a-z]\w*", l):
            out.append(i)
        if re.search(r"@end(code|verbatim)\b", l): in_code = False
    return out


def markup_lines(code):
    """Inline markup and list or table rows inside a comment."""
    pat = r"\*\*[^*]+\*\*|(?<![\w*/])\*[^*\s][^*]*\*(?![\w*/])|`[^`]+`|~~[^~]+~~|\[[^\]]+\]\([^)]+\)|https?://|^\s*\*?\s*[-*+]\s+\S|^\s*\*?\s*\d+\.\s+\S|\||</?t(able|r|d|h)\b"
    return [i for i, l in enumerate(code.split("\n"), 1) if re.search(pat, l) and not re.match(r"^\s*/\*\*|^\s*\*/", l)]


def description_lines(code):
    """The paragraphs of a doc comment after the brief: from the first blank line inside the comment to its end."""
    lines = code.split("\n"); out = []; inside = False; seen_blank = False
    for i, l in enumerate(lines, 1):
        if "/**" in l: inside = True; seen_blank = False; continue
        if "*/" in l: inside = False; continue
        if inside:
            if not l.strip(): seen_blank = True; continue
            if seen_blank: out.append(i)
    return out


def undocumented_lines(code):
    """Declarations with no doc comment right above them; plain // comments do not count."""
    lines = code.split("\n"); out = []; prev_doc = False
    for i, l in enumerate(lines, 1):
        s = l.strip()
        if not s: continue
        if s.startswith(("///", "/**")) or s.endswith("*/") or s.startswith("*"):
            prev_doc = True; continue
        if s.startswith(("//", "#", "}", "{")):
            prev_doc = False; continue
        if re.search(r"[;{]\s*$", s) and not re.match(r"^(struct|class|namespace|enum)\b", s):
            if not prev_doc: out.append(i)
        prev_doc = False
    return out


def related_lines(code):
    """Free functions that take or return a type defined in the file."""
    names = set(re.findall(r"\b(?:struct|class)\s+(\w+)", code))
    return [i for i, l in enumerate(code.split("\n"), 1)
            if "(" in l and not re.match(r"^\s*(struct|class)\b", l) and any(re.search(rf"\b{n}\b", l) for n in names)]


def comment_lines(code):
    lines = code.split("\n"); out = []; inside = False
    for i, l in enumerate(lines, 1):
        s = l.strip()
        if s.startswith("///"): out.append(i); continue
        if "/**" in s: inside = True
        if inside: out.append(i)
        if "*/" in s: inside = False
    return out


def by_token(code, pat):
    if pat == "UNDOCUMENTED": return undocumented_lines(code)
    if pat == "RELATED": return related_lines(code)
    return lines_matching(code, pat)


def repeated_names(code):
    names = re.findall(r"\b(\w+)\s*\(", code)
    dup = {n for n in names if names.count(n) > 1 and n not in ("if", "for", "while", "return", "sizeof")}
    return [i for i, l in enumerate(code.split("\n"), 1) if any(re.search(rf"\b{d}\s*\(", l) for d in dup)]


def yaml_tokens(ycode):
    """Identifiers named by the option values: the parts of globs and paths the C++ lines can be matched on."""
    toks = set()
    for v in re.findall(r"['\"]([^'\"]+)['\"]", ycode) + re.findall(r":\s*([\w./*-]+)\s*$", ycode, re.M):
        for part in re.split(r"::|/|\*|\.|\s", v):
            if len(part) >= 3 and part not in ("include", "true", "false", "src", "hpp", "cpp", "yml"):
                toks.add(part)
    return toks


def yaml_options(ycode):
    return set(re.findall(r"^([a-z][\w-]*):", ycode, re.M))


def code_of(node):
    if node.get("code") is not None: return node["code"]
    if node.get("file"): return read_source(node["file"], node.get("include"))
    return ""


def feature_lines(code, lang, page, sid, yml):
    out = _feature_lines(code, lang, page, sid, yml)
    nonblank = len([l for l in code.split("\n") if l.strip()])
    return out if out and len(set(out)) <= 0.75 * nonblank else []


def _feature_lines(code, lang, page, sid, yml):
    """Which lines to light, given the code, its language, the docs page (from the slide id), and the group's mrdocs.yml."""
    if sid in SNIPPETS:
        return lines_matching(code, SNIPPETS[sid])
    if lang == "yaml":
        body = [i for i, l in enumerate(code.split("\n"), 1) if l.strip() and not l.lstrip().startswith("#")]
        return body if len(body) < len([l for l in code.split("\n") if l.strip()]) else []
    if lang in ("javascript", "lua"):
        return lines_matching(code, r"mrdocs\.register_|ctx\.output\.write|mrdocs\.report|register_(transform|generator)")
    if lang == "handlebars":
        return lines_matching(code, r"\{\{")
    if lang != "cpp":
        return []
    if page.startswith("commands"):
        if "index-01" in sid:
            return []  # the whole example is the feature: doc comments in general
        if "paragraph" in sid:
            return description_lines(code)
        cmds = doc_command_lines(code)
        if cmds: return cmds
        return markup_lines(code)
    # configuration and extension inputs: the lines the option or the script acts on
    out = []
    for key, pat in INPUT_TOKENS.items():
        if key in sid: return by_token(code, pat)
    if yml:
        for opt in yaml_options(yml):
            pat = OPTION_TOKENS.get(opt)
            if pat: out += by_token(code, pat)
            elif opt == "overloads": out += repeated_names(code)
        toks = yaml_tokens(yml)
        if toks: out += lines_matching(code, r"\b(" + "|".join(map(re.escape, sorted(toks))) + r")")
    elif re.search(r"^\s*#(define|if)", code, re.M):
        out = lines_matching(code, r"^\s*#(define|if|endif|else)")
    if not out:
        out = doc_command_lines(code)
    return out


def main():
    text = PLAN.read_text(); story = yaml.safe_load(text)
    plan_lines = text.split("\n"); edits = []  # (line index to insert after, text)
    for a in story["acts"]:
        for b in a["beats"]:
            buf = []
            for s in b["slides"]:
                if s["kind"] == "code": buf.append(s)
                elif s["kind"] == "rendered":
                    if buf and buf[0]["id"] not in ("install-from-source",): process(buf, edits, plan_lines)
                    buf = []
                else: buf = []
    if DRY: return
    for idx, ins in sorted(edits, key=lambda e: -e[0]):
        plan_lines[idx + 1:idx + 1] = ins
    PLAN.write_text("\n".join(plan_lines))
    print(f"lit {len(edits)} blocks")


def find_slide_line(plan_lines, sid):
    for i, l in enumerate(plan_lines):
        if l == f"    - id: {sid}": return i
    raise KeyError(sid)


def process(buf, edits, plan_lines):
    yml = next((code_of(s) for s in buf if s.get("language") == "yaml"), None)
    for s in buf:
        page = s["id"].split("-", 1)[0] + "-" + s["id"].split("-")[1] if s["id"].startswith(("commands", "configuration", "extensions", "generators")) else s["id"]
        start = find_slide_line(plan_lines, s["id"])
        if s.get("steps"):
            if any(st.get("highlight") for st in s["steps"]): continue
            step_no = 0
            for i in range(start + 1, len(plan_lines)):
                l = plan_lines[i]
                if l.startswith("    - id: ") or (l and not l.startswith(" ")) or l.startswith("  - id: "): break
                if re.match(r"^        language: ", l):
                    st = s["steps"][step_no]; step_no += 1
                    hl = feature_lines(code_of(st), st.get("language"), page, s["id"], yml)
                    if hl:
                        r = ranges(hl); print(f"{s['id']} step {step_no}: {r}")
                        edits.append((i, ["        highlight:", f"        - '{r}'"]))
        else:
            if s.get("highlights"): continue
            hl = feature_lines(code_of(s), s.get("language"), page, s["id"], yml)
            if not hl: print(f"{s['id']}: nothing to light"); continue
            r = ranges(hl); print(f"{s['id']}: {r}")
            for i in range(start + 1, len(plan_lines)):
                if re.match(r"^      language: ", plan_lines[i]):
                    edits.append((i, ["      highlights:", f"      - '{r}'"])); break
                if plan_lines[i].startswith("    - id: "): break


if __name__ == "__main__":
    main()
