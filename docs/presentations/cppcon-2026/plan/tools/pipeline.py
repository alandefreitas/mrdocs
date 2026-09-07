#!/usr/bin/env python3
"""The talk pipeline: meta -> story -> beats -> slides -> html, with logs and reviews.

Each level is a YAML file validated against a JSON schema, then checked against
the levels above it. Every check has an id (M*, P*, S*, H*, D*, R*) and
every claim in research/HOW-TO-GIVE-GREAT-TALKS.md is mapped to the checks that
enforce it in plan/levels/0-criteria.yml. The `report` command shows, per research
claim, whether its checks passed.

Commands:
  validate [--strict]   schema-validate every level, run every check
  build [--out PATH] [--artifacts] [--rehearsal]
                        generate the deck (default plan/build/index.html; --rehearsal writes plan/build/rehearsal.html
                        with faint slide numbers and visible arrows, for practice only)
  report [--strict]     validate, then print criteria coverage
  criteria              regenerate plan/levels/0-criteria.yml from the research file
  schemas               write JSON copies of the YAML schemas to plan/build/schemas/
  pace                  per-beat targets against declared and estimated speaking time
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import yaml
from jsonschema import Draft202012Validator, FormatChecker

TOOLS = Path(__file__).resolve().parent
PLAN = TOOLS.parent
DECK = PLAN.parent
REPO = DECK.parents[2]
LEVELS = PLAN / "levels"
SCHEMAS = PLAN / "schemas"
# level name used in the code -> file stem, numbered by position in the workflow (4 is the built HTML)
LEVEL_FILE = {"criteria": "0-criteria", "meta": "1-meta", "story": "2-plan", "logs": "4-logs"}


def level_file(name):
    return LEVELS / f"{LEVEL_FILE[name]}.yml"


def schema_file(name):
    return SCHEMAS / f"{LEVEL_FILE[name]}.schema.yml"


TAG_LINE = re.compile(r"^\s*(?://|#|--|<!--)\s*(tag|end)::([\w-]+)\[\]")


def read_source(file, include=None):
    """A code block taken from a file in the repository instead of typed into the plan. `include` narrows it the way
    Asciidoctor does: tag=name or tags=a;!b keep tagged regions, lines=a..b keeps a range (after the tags), indent=0
    strips the common indentation. Tag marker lines never show. Each tagged region loses its own indentation, so a
    line tagged inside an if() block lines up with one tagged at the top level, and a blank line separates regions
    that were not adjacent in the file."""
    text = (REPO / file).read_text(encoding="utf-8")
    lines = text.split("\n")
    a = dict(kv.split("=", 1) for kv in include.split(",") if "=" in kv) if include else {}
    tags = a.get("tag") or a.get("tags")
    if tags:
        wanted, excluded = set(), set()
        for tg in tags.split(";"):
            (excluded if tg.startswith("!") else wanted).add(tg.lstrip("!"))
        regions, region, active, skipped = [], [], [], False
        for ln in lines:
            m = TAG_LINE.match(ln)
            if m:
                if m.group(1) == "tag":
                    active.append(m.group(2))
                elif m.group(2) in active:
                    active.remove(m.group(2))
                continue
            cur = set(active)
            if (wanted and not (cur & wanted)) or (excluded and (cur & excluded)):
                if region:
                    regions.append(region); region = []
                skipped = True
                continue
            region.append(ln)
        if region:
            regions.append(region)
        out = []
        for region in regions:
            # a region picked by name is a snippet of its own; with exclusions only, the file keeps its own shape
            pad = min((len(l) - len(l.lstrip()) for l in region if l.strip()), default=0) if wanted else 0
            if out and wanted:
                out.append("")
            out.extend(l[pad:] for l in region)
        lines = out
    else:
        lines = [ln for ln in lines if not TAG_LINE.match(ln)]
    if "lines" in a:
        lo, _, hi = a["lines"].partition("..")
        lines = lines[int(lo or 1) - 1:int(hi) if hi else len(lines)]
    if a.get("indent") == "0":
        pad = min((len(l) - len(l.lstrip()) for l in lines if l.strip()), default=0)
        lines = [l[pad:] for l in lines]
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    return "\n".join(lines)


def resolve_code(node):
    """A slide, step or block that names a `file` gets its code read from the repository: `source` on a snippet
    slide, `code` everywhere else."""
    key = "source" if node.get("kind") == "snippet" else "code"
    if node.get("file") and node.get(key) is None:
        node[key] = read_source(node["file"], node.get("include"))


def hl_items(s):
    """A slide's highlights as a list: a single highlight may be written as one bare range string."""
    h = s.get("highlights") or []
    return [h] if isinstance(h, str) else list(h)


def hl_lines(s):
    """The line ranges of a single-block slide's highlights, whether written as strings or as objects with notes."""
    return [h if isinstance(h, str) else h["lines"] for h in hl_items(s)]


def hl_notes(s):
    """What is said over each highlight; empty for a highlight written as a bare range."""
    return [("" if isinstance(h, str) else h.get("notes", "")) for h in hl_items(s)]


def slides_of(story, wpm=120):
    """The slides of every beat of every act, flattened in talk order and tagged with their beat. A slide of kind
    `same` repeats the previous slide's content with new notes: it inherits everything but id, notes, duration and why.
    A slide without duration_s gets the time its notes take to say at `wpm` plus the look-time for its kind; the
    field is written only to override that (a live demo, a pause)."""
    out = []
    for a in story["acts"]:
        for b in a.get("beats", []):
            for s in b.get("slides", []):
                s = dict(s, beat=b["id"])
                if s["kind"] == "same":
                    prev = out[-1]
                    keep = {k: s[k] for k in ("id", "beat", "notes", "duration_s", "why") if k in s}
                    s = {k: v for k, v in prev.items() if k not in ("id", "notes", "duration_s", "why", "motion", "star_moment", "dramatic_question")}
                    s.update(keep); s["same"] = True
                resolve_code(s)
                for st in s.get("steps") or []:
                    resolve_code(st)
                    for blk in st.get("blocks") or []:
                        resolve_code(blk)
                if "notes" not in s and any(hl_notes(s)):  # a highlighted block speaks through its highlights
                    s["notes"] = "\n\n".join(n for n in hl_notes(s) if n)
                    s["notes_from_highlights"] = True
                elif "notes" not in s:  # a stepped slide speaks through its steps; the joined text serves the checks
                    s["notes"] = "\n\n".join(st.get("notes", "") for st in steps_of(s) if st.get("notes"))
                    s["notes_from_steps"] = True
                if "duration_s" not in s:
                    s["duration_s"] = max(3 if s["kind"] in ("code", "snippet", "rendered") else 5, estimated_seconds(s, wpm))
                    s["derived_duration"] = True
                out.append(s)
    return {"slides": out}


def beats_of(story):
    """The beats of every act, flattened in talk order, each tagged with its act id, for the beat checks."""
    return {"beats": [dict(b, act=a["id"]) for a in story["acts"] for b in a.get("beats", [])]}
LOGS = PLAN / "logs" / "logs.yml"
BUILD = PLAN / "build"
RESEARCH = DECK / "research" / "HOW-TO-GIVE-GREAT-TALKS.md"
NAV = REPO / "docs" / "modules" / "ROOT" / "nav.adoc"

SCREEN_LINES = 21  # lines of code the frame shows at full size; a taller block must scroll through highlight steps
COVER_KINDS = {"title-card", "cover", "conclusions", "cta", "backup", "roadmap"}
HIGH_AROUSAL = {"awe", "surprise", "tension", "amusement", "anger"}
SPINE = ["once-upon-a-time", "every-day", "until-one-day", "because-of-that", "until-finally", "ever-since-then"]
BANNED = [
    r"\bsorry\b", r"\bapologi[sz]e", r"is the mic on", r"a lot to tell you", r"not good at this",
    r"great question", r"this next bit is boring", r"bear with me",
]


# --------------------------------------------------------------------------- utilities
def words(s):
    return re.findall(r"[A-Za-z0-9'’+#-]+", s or "")


def wc(s):
    return len(words(s))


def load_yaml(path):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


def norm(s):
    return re.sub(r"\s+", " ", (s or "").strip().lower())


class Findings:
    def __init__(self):
        self.items = []  # (check, status, message)

    def ok(self, check, msg=""):
        self.items.append((check, "ok", msg))

    def fail(self, check, msg):
        self.items.append((check, "fail", msg))

    def warn(self, check, msg):
        self.items.append((check, "warn", msg))

    def skip(self, check, msg):
        self.items.append((check, "skip", msg))

    def status_of(self, check):
        st = [s for c, s, _ in self.items if c == check]
        if not st:
            return "unrun"
        if "fail" in st:
            return "fail"
        if "warn" in st:
            return "warn"
        if "skip" in st and "ok" not in st:
            return "skip"
        return "ok"


# --------------------------------------------------------------------------- schema validation
def load_schema(name):
    return load_yaml(schema_file(name))


def export_schemas():
    """JSON copies of the YAML schemas, for editors that only map .json schema files."""
    out = BUILD / "schemas"
    out.mkdir(parents=True, exist_ok=True)
    for p in sorted(SCHEMAS.glob("*.schema.yml")):  # numbered files sort in workflow order
        (out / p.name.replace(".yml", ".json")).write_text(json.dumps(load_yaml(p), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"wrote {out / p.name.replace('.yml', '.json')}")


def validate_schema(data, schema, label, F):
    v = Draft202012Validator(schema, format_checker=FormatChecker())
    errs = sorted(v.iter_errors(data), key=lambda e: list(e.absolute_path))
    for e in errs:
        where = "/".join(str(p) for p in e.absolute_path) or "(root)"
        F.fail(f"schema:{label}", f"{where}: {e.message}")
    if not errs:
        F.ok(f"schema:{label}")
    return not errs


# --------------------------------------------------------------------------- level checks
def check_meta(meta, F, slides=None):
    n = wc(meta["idea"]["statement"])
    (F.ok if n <= 15 else F.fail)("M1", f"idea.statement has {n} words (limit 15)")
    idea = set(w.lower() for w in words(meta["idea"]["statement"]))
    all_notes = norm(" ".join(s["notes"] for s in slides["slides"])) if slides else ""
    for name, th in meta["idea"]["take_home"].items():
        tw = set(w.lower() for w in words(th))
        if len(idea & tw) / max(1, len(idea)) > 0.7:
            F.fail("M3", f"take_home.{name} restates the one idea; it should be a reason or an action, not the idea")
        if slides and norm(th) not in all_notes:
            F.fail("M3", f"take_home.{name} is never said in the notes")
    if F.status_of("M3") == "unrun":
        F.ok("M3", "problem, proof and action are distinct from the idea, each said")
    for k in ("M2", "M4", "M5", "M6", "M7", "M8", "M9", "M10", "M11"):
        F.ok(k, "present (schema)")
    if meta["policies"]["slide_numbers_visible"]:
        F.warn("M12", "slide numbers are visible: a rehearsal aid; set policies.slide_numbers_visible to false before the talk (Conway2014)")
    else:
        F.ok("M12", "present (schema)")


def check_story(story, meta, F, strict, slides=None, beats=None):
    acts = story["acts"]
    # P1 spine order
    seq = [s for a in acts for s in a["spine"] if s != "none"]
    idx = [SPINE.index(s) for s in seq]
    if seq and idx == sorted(idx) and seq[0] == SPINE[0] and seq[-1] == SPINE[-1]:
        F.ok("P1", " -> ".join(seq))
    else:
        F.fail("P1", f"spine beats out of order or incomplete: {seq}")
    # P2 gap and alternation
    if acts[0]["side"] != "both":
        F.fail("P2", "opening act must show both status_quo and what_could_be")
    sides = [a["side"] for a in acts[1:-1] if a["side"] != "both"]
    switches = sum(1 for i in range(1, len(sides)) if sides[i] != sides[i - 1])
    middle = {a["id"] for a in acts[1:-1]}
    tacks = sum(1 for b in (beats or {}).get("beats", []) if b["act"] in middle and b.get("contrast"))
    (F.ok if switches + tacks >= 2 else F.fail)("P2", f"the middle crosses the gap {switches + tacks} times: {switches} between acts, {tacks} inside beats flagged contrast (need 2)")
    F.ok("P3", story["dramatic_question"])
    ins, stmt = norm(story["insight"]), norm(meta["idea"]["statement"])
    said = any(ins in norm(s["notes"]) for s in slides["slides"]) if slides else True
    if ins == stmt:
        F.fail("P5", "insight repeats the idea statement; it should explain it in other words")
    elif not said:
        F.fail("P5", "insight is never said in the notes")
    else:
        F.ok("P5", "insight distinct from the statement and said aloud")
    (F.ok if any(a.get("moment_of_change") for a in acts) else F.fail)("P4", "moment_of_change")
    ar = [a["arousal"] for a in acts]
    if "sadness" in ar:
        F.fail("P6", "an act targets sadness (deactivating)")
    elif not (set(ar) & HIGH_AROUSAL):
        F.fail("P6", f"no high-arousal act: {ar}")
    else:
        F.ok("P6", ", ".join(ar))
    stars = [a["id"] for a in acts if a.get("star_moment")]
    (F.ok if len(stars) == 1 else F.fail)("P7", f"star moment acts: {stars}")
    F.ok("P8", "fence present (schema)")
    cyc = sum(1 for a in acts if a["cycles_idea"])
    (F.ok if cyc >= 3 else F.fail)("P9", f"idea cycled in {cyc} acts (need 3)")
    missing = [c["mnemonic_image"] for c in story["chapters"] if not (DECK / c["mnemonic_image"]).exists()]
    (F.fail if missing else F.ok)("P10", f"{len(story['chapters'])} chapters; missing images: {missing}")
    kept = sum(wc(p) for c in story["chapters"] for p in c["points"])
    prose = story["candidate_points"]
    pages = len(nav_breadcrumbs()) if re.search(r"nav\.adoc|documentation", prose) else 0
    dump = wc(prose) + 10 * pages
    (F.ok if dump >= 2 * kept else F.fail)("P11", f"dump: {wc(prose)} words of prose plus {pages} documented pages; kept {kept} words of chapter points (dump must be at least twice)")
    unverified = [f"{a['id']}/{c['id']}" for a in acts for c in a["claims"] for e in c["evidence"] if e.get("verify") and not e.get("verified")]
    if unverified:
        (F.fail if strict else F.warn)("P12", f"facts still to verify: {unverified}")
    else:
        F.ok("P12", "all evidence verified")
    F.ok("P13", f"{len(story.get('metaphors', []))} metaphors; the familiar ground is the audience's own Doxygen experience (audience.share)")
    (F.ok if acts[-1].get("references_act") == acts[0]["id"] else F.fail)("P14", "closing act must reference the opening act")
    (F.ok if acts[-1].get("new_bliss") else F.fail)("P15", "closing act needs new_bliss")
    F.ok("P16", "context present (schema)")
    F.ok("P17", "memorability present (schema)")


def talk_minutes(meta):
    e = meta["event"]
    return e["duration_min"] - e["qa_min"] - e.get("buffer_min", 0)


def check_beats(beats, story, meta, slides, F):
    # P28: every claim of an act is carried by at least one of its beats, and beats name only claims that exist
    for a in story["acts"]:
        claims = {c["id"] for c in a["claims"]}
        carried = {c for b in a.get("beats", []) for c in (b.get("claims") or [])}
        if carried - claims:
            F.fail("P28", f"act {a['id']}: beats name claims that do not exist: {sorted(carried - claims)}")
        if claims - carried:
            F.fail("P28", f"act {a['id']}: claims carried by no beat: {sorted(claims - carried)}")
    if F.status_of("P28") == "unrun":
        F.ok("P28", "every claim is carried by a beat of its act")
    bl = beats["beats"]
    wpm = meta["event"]["speaking_wpm"]
    tm = talk_minutes(meta)
    times = beat_times(bl, slides, wpm, tm)
    for b in bl:
        b["target"], b["estimate"], _ = times[b["id"]]
    given = sum(b["target_minutes"] for b in bl if b.get("target_minutes") is not None)
    free = [b["id"] for b in bl if b.get("target_minutes") is None]
    if given > tm + 1:
        F.warn("P18", f"the beats with targets already sum to {given:.2f} min, talk is {tm} min; a warning while the deck is being cut down")
    elif free:
        F.ok("P18", f"{given:.2f} min of targets, {tm - given:.2f} min shared by {len(free)} beats without one: {free}")
    else:
        (F.ok if abs(given - tm) <= 1 else F.warn)("P18", f"beat targets sum to {given:.2f} min, talk is {tm} min" + ("" if abs(given - tm) <= 1 else "; a warning while the deck is being cut down"))
    act_ids = [a["id"] for a in story["acts"]]
    for b in bl:
        if b["act"] not in act_ids:
            F.fail("P18", f"beat {b['id']} names unknown act {b['act']}")
    # P19 transition words in first slide notes
    first_slide = {}
    for s in slides["slides"]:
        first_slide.setdefault(s["beat"], s)
    for b in bl:
        s = first_slide.get(b["id"])
        if not s:
            F.fail("P19", f"beat {b['id']} has no slides")
        elif norm(b["transition_words"]) not in norm(spoken_text(s)):  # the notes as said
            F.fail("P19", f"beat {b['id']}: transition words '{b['transition_words']}' not in first slide notes ({s['id']})")
    if F.status_of("P19") == "unrun":
        F.ok("P19", "every beat opens with its transition words")
    for b in bl:
        if not b.get("claims") and not b.get("purpose"):
            F.fail("P20", f"beat {b['id']} carries no claim and states no purpose")
    if F.status_of("P20") == "unrun":
        F.ok("P20", "every beat carries a claim or states its purpose")
    ids = [b["id"] for b in bl]
    for b in bl:
        if b.get("explains_system"):
            pb = b.get("parts_beat")
            if not pb or pb not in ids or ids.index(pb) >= ids.index(b["id"]):
                F.fail("P21", f"beat {b['id']} explains a system without an earlier parts beat")
    if F.status_of("P21") == "unrun":
        F.ok("P21")
    for b in bl:
        if b["target"] > 6 and not (b.get("pause") or b.get("question_to_room")):
            F.fail("P22", f"beat {b['id']} runs {b['target']:.2f} min with no pause or question")
    if F.status_of("P22") == "unrun":
        F.ok("P22")
    (F.ok if any(b.get("question_to_room") for b in bl) else F.warn)("P23", "no beat asks the room a question")
    media = {m for b in bl for m in b["media"]}
    (F.ok if len(media & {"live", "code", "diagram", "image", "prop"}) >= 2 else F.fail)("P24", f"media kinds: {sorted(media)}")
    closing = [b for b in bl if b["act"] == act_ids[-1]]
    cm = sum(b["target"] for b in closing)
    (F.ok if len(closing) >= 2 and cm >= 2 else F.fail)("P25", f"closing act: {len(closing)} beats, {cm:.2f} min")
    t = 0
    seen = False
    for b in bl:
        if b.get("establishes_vision"):
            seen = True
            (F.ok if t <= 5 else F.fail)("P26", f"vision established at minute {t:.2f}")
        t += b["target"]
    if not seen:
        F.warn("P26", "no beat flagged establishes_vision")


def steps_of(s):
    """The stepped views of a slide: code steps, or a table's emphasis steps."""
    return s.get("steps") or (s.get("table") or {}).get("steps") or []


def spoken_text(s):
    """Everything said over a slide: its notes plus its steps' notes, without double counting a slide whose notes
    were assembled from the steps."""
    notes = s["notes"]
    if s.get("notes_from_steps") or s.get("notes_from_highlights"):
        return notes
    # a highlight note that only names its lines ("Lines 17 to 32.") is a cue for the speaker view, not speech
    said = [n for n in hl_notes(s) if not re.fullmatch(r"Lines \d+( to \d+)?\.", n.strip())]
    return notes + " " + " ".join(st.get("notes", "") for st in steps_of(s)) + " " + " ".join(said)


def estimated_seconds(s, wpm):
    """Speaking time implied by the notes (and per-step 'say' text on code slides), plus a floor
    for the audience to look at a picture or a page."""
    words = wc(spoken_text(s))
    spoken = words / wpm * 60
    # an example slide with a cue for notes (a few words) is a glance: it is on screen for the seconds its words
    # take, as the docs examples are flipped through one per phrase; the look floor is for slides that are explained
    steps_extra = 3 * (max(0, len(hl_lines(s)) - 1) + sum(max(0, len(st.get("highlight") or []) - 1) for st in steps_of(s)))
    if s["kind"] in ("code", "snippet", "rendered") and words <= 8:
        return max(3, round(spoken)) + steps_extra
    # a snippet or rendered slide is a documentation panel read while the notes explain it: no floor of its own
    look = {"live": 45, "image": 8, "diagram": 12, "table": 12, "code": 6}.get(s["kind"], 0) + steps_extra
    return int(round(spoken + look))


def beat_times(beats, slides, wpm, tm):
    """{beat id: (target, estimate, derived)} in minutes. The talk time tm from the meta file is the budget: beats with
    target_minutes take theirs from it, and what is left is shared among the beats without a target in proportion to
    the minutes their notes take to say (equally, when none of them has notes yet). The estimate is the notes said at
    wpm plus look time; a beat with a target and no notes yet is estimated at its target."""
    est = {b["id"]: sum(estimated_seconds(s, wpm) for s in slides["slides"] if s["beat"] == b["id"]) / 60 for b in beats}
    given = {b["id"]: b["target_minutes"] for b in beats if b.get("target_minutes") is not None}
    free = [b["id"] for b in beats if b["id"] not in given]
    left = max(0.0, tm - sum(given.values()))
    weight = {i: est[i] for i in free}
    if free and sum(weight.values()) == 0: weight = {i: 1.0 for i in free}
    out = {}
    for b in beats:
        i = b["id"]
        if i in given: out[i] = (given[i], est[i] if est[i] > 0 else given[i], False)
        else: out[i] = (left * weight[i] / sum(weight.values()), est[i], True)
    return out


def print_pace(meta, beats, slides):
    wpm = meta["event"]["speaking_wpm"]
    print(f"{'story':12s} {'beat':18s} {'target':>7s} {'estimated':>10s} {'deviation':>10s}   (minutes; notes at {wpm} wpm plus look time)")
    tt = te = 0.0
    act_t = {}; act_e = {}
    times = beat_times(beats["beats"], slides, wpm, talk_minutes(meta))
    for b in beats["beats"]:
        target, est, derived = times[b["id"]]
        tt += target; te += est
        act_t[b["act"]] = act_t.get(b["act"], 0) + target; act_e[b["act"]] = act_e.get(b["act"], 0) + est
        flag = "" if abs(est - target) <= max(0.5, 0.25 * target) else "  <- off target"
        note = "  (derived: its share of the minutes the other targets leave)" if derived else ""
        print(f"{b['act']:12s} {b['id']:18s} {target:7.2f} {est:10.2f} {est - target:+10.2f}{flag}{note}")
    print()
    for act in act_t:
        print(f"{act:12s} {'':18s} {act_t[act]:7.2f} {act_e[act]:10.2f} {act_e[act] - act_t[act]:+10.2f}")
    print(f"{'total':12s} {'':18s} {tt:7.2f} {te:10.2f} {te - tt:+10.2f}   talk {talk_minutes(meta)} min")
    total_words = sum(wc(spoken_text(s)) for s in slides["slides"] if s["kind"] != "backup")
    if LOGS.exists():
        rs = [r for r in (load_yaml(LOGS) or {}).get("rehearsals", []) if r.get("live") and r.get("timed_minutes")]
        if rs:
            for r in rs:
                print(f"rehearsal {r['date']}: {r['timed_minutes']} min for {total_words} words of notes -> {total_words / r['timed_minutes']:.0f} wpm")
            print(f"meta.yml has speaking_wpm: {wpm}; set it to the measured rate once the notes are final.")
        else:
            print(f"no rehearsal logged yet; speaking_wpm {wpm} is the rate measured on two slides; a recorded rehearsal replaces it (see meta.yml).")
    print("\nRehearse one beat at a time against its target; the speaker view (press S) shows a pacing bar per slide from data-timing.")


def nav_pages():
    """Every page in docs/modules/ROOT/nav.adoc: (section, title, page). Top-level pages are their own section."""
    pages = []
    if not NAV.exists():
        return pages
    section = None
    for ln in NAV.read_text(encoding="utf-8").splitlines():
        m1 = re.match(r"^\* (?:xref:([^\[]+)\[([^\]]*)\]|(.+))$", ln)
        m2 = re.match(r"^\*\* xref:([^\[]+)\[([^\]]*)\]", ln)
        if m2:
            page, title = m2.groups()
            pages.append((section, title or Path(page).stem.replace("-", " ").title(), page))
        elif m1:
            page, title, plain = m1.groups()
            section = (plain or title or Path(page).stem.replace("-", " ").title()).strip()
            if page:
                pages.append((section, None, page))
    return pages


def nav_breadcrumbs():
    crumbs = set()
    if not NAV.exists():
        return crumbs
    section = None
    for ln in NAV.read_text(encoding="utf-8").splitlines():
        m1 = re.match(r"^\* (?:xref:([^\[]+)\[([^\]]*)\]|(.+))$", ln)
        m2 = re.match(r"^\*\* xref:([^\[]+)\[([^\]]*)\]", ln)
        if m2:
            page, title = m2.groups()
            if not title:
                title = Path(page).stem.replace("-", " ").title()
            crumbs.add(norm(f"{section} / {title}"))
        elif m1:
            page, title, plain = m1.groups()
            section = (plain or title or Path(page).stem.replace("-", " ").title()).strip()
            crumbs.add(norm(section))
    return crumbs


def image_size(path):
    try:
        out = subprocess.run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", str(path)], capture_output=True, text=True).stdout
        w = int(re.search(r"pixelWidth: (\d+)", out).group(1))
        h = int(re.search(r"pixelHeight: (\d+)", out).group(1))
        return w, h
    except Exception:
        return None


def check_slides(slides, beats, story, meta, F):
    sl = slides["slides"]
    beat_ids = [b["id"] for b in beats["beats"]]
    for s in sl:
        if s["beat"] not in beat_ids:
            F.fail("S1", f"slide {s['id']} names unknown beat {s['beat']}")
    seq = [s["beat"] for s in sl]
    runs = [b for k, b in enumerate(seq) if k == 0 or b != seq[k - 1]]
    missing = [b for b in beat_ids if b not in runs]
    if missing:
        F.fail("S29", f"beats with no slide: {missing}")
    elif len(runs) != len(set(runs)):
        F.fail("S29", f"a beat's slides are split by another beat's: {sorted({b for b in runs if runs.count(b) > 1})}")
    elif [b for b in runs if b in beat_ids] != [b for b in beat_ids if b in runs]:
        F.fail("S29", f"beats appear in the deck out of plan order: {runs}")
    else:
        F.ok("S29", f"{len(sl)} slides over {len(runs)} beats, contiguous, in plan order")
    kinds = [s["kind"] for s in sl]
    non_backup = [s for s in sl if s["kind"] != "backup"]
    if kinds[0] != "title-card":
        F.fail("S1", "first slide must be the title card")
    # after the conclusions only backups and a repeat of the title card (the frame for after the questions) may follow
    tail = [s for s in non_backup[non_backup.index(next(s for s in non_backup if s["kind"] == "conclusions")) + 1:]] if any(s["kind"] == "conclusions" for s in non_backup) else []
    if not any(s["kind"] == "conclusions" for s in non_backup) or any(s["kind"] != "title-card" for s in tail):
        F.fail("S1", "the conclusions must be the last slide, apart from backups and a repeated title card")
    ctas = [i for i, s in enumerate(sl) if s["kind"] == "cta"]
    concl = [i for i, s in enumerate(sl) if s["kind"] == "conclusions"]
    if len(ctas) != 1 or len(concl) != 1 or ctas[0] > concl[0]:
        F.fail("S1", "exactly one cta slide, before the single conclusions slide")
    if F.status_of("S1") == "unrun":
        F.ok("S1")
    # S2 on-screen limits
    for s in sl:
        t = s["on_screen"]
        k = s["kind"]
        n = wc(t)
        if k == "word" and n > 3:
            F.fail("S2", f"{s['id']}: word slide has {n} words")
        if k in ("line", "conclusions") and n > 14:
            F.fail("S2", f"{s['id']}: line has {n} words (limit 14)")
        if re.search(r"(^|\n)\s*([-*•]|\d+[.)])\s", t):
            F.fail("S2", f"{s['id']}: list-like on-screen text")
        if k == "table":
            tb = s["table"]
            if len(tb["header"]) > 5 or any(len(r) > 5 for r in tb["rows"]):
                F.fail("S2", f"{s['id']}: table wider than 5 columns")
            bad = [r for r in (tb.get("highlight_rows") or []) if not 1 <= r <= len(tb["rows"])]
            bad += [f"col {c}" for c in (tb.get("highlight_cols") or []) if not 1 <= c <= len(tb["header"])]
            if bad:
                F.fail("S2", f"{s['id']}: highlights point outside the table: {bad}")
        if k == "code" and s.get("code") is not None:
            lines = s["code"].rstrip("\n").split("\n")
            if len(lines) > SCREEN_LINES and len(hl_lines(s)) < 2:
                F.fail("S2", f"{s['id']}: {len(lines)} lines, taller than the frame ({SCREEN_LINES}); give it highlight steps with notes so it scrolls, or cut it")
            if any(len(l) > 320 for l in lines):
                F.fail("S2", f"{s['id']}: line over 320 columns")
            for h in hl_lines(s):  # a highlight step lights at most a screenful minus one: twenty lines
                for part in h.split(","):
                    a, _, b = part.partition("-")
                    if b and int(b) - int(a) + 1 > SCREEN_LINES - 1:
                        F.fail("S2", f"{s['id']}: highlight {h} spans more than {SCREEN_LINES - 1} lines")
        if k == "snippet":  # half-width pane at a smaller size: 72 columns fit
            lines = s["source"].rstrip("\n").split("\n")
            if any(len(l) > 72 for l in lines):
                F.fail("S2", f"{s['id']}: source line over 72 columns")
        if k == "code" and s.get("steps"):
            for i, st in enumerate(s["steps"], 1):
                blocks = st.get("blocks") or [st]
                if len(st.get("highlight") or []) > 1:
                    F.fail("S12", f"{s['id']} step {i}: several highlight ranges in one step have no notes of their own; make them steps or slides")
                total = 0
                for bi, b in enumerate(blocks, 1):
                    lines = b["code"].rstrip("\n").split("\n")
                    total += len(lines)
                    where = f"{s['id']} step {i}" + (f" block {bi}" if len(blocks) > 1 else "")
                    if any(len(l) > 320 for l in lines):
                        F.fail("S2", f"{where}: line over 320 columns")
                    if len(lines) > SCREEN_LINES and not st.get("blocks"):
                        F.fail("S2", f"{where}: {len(lines)} lines, taller than the frame ({SCREEN_LINES}); a step cannot scroll, split it into slides with highlight steps")
                if len(blocks) > 2:
                    F.fail("S2", f"{s['id']} step {i}: {len(blocks)} blocks (limit 2)")
        if k == "roadmap" and len(t.split("·")) > 5:
            F.fail("S2", f"{s['id']}: roadmap with more than 5 items")
    if F.status_of("S2") == "unrun":
        F.ok("S2")
    F.ok("S3", "every slide has an assertion (schema)")
    total = sum(s["duration_s"] for s in sl if s["kind"] != "backup")
    tm = talk_minutes(meta) * 60
    if abs(total - tm) > 0.10 * tm:
        F.warn("S4", f"slide durations sum to {total}s, talk is {tm}s (±10%); a warning while the deck is being cut down")
    wpm = meta["event"]["speaking_wpm"]
    for s in sl:
        cap = 240 if s["kind"] in ("live", "code", "table", "diagram") else 120  # a picture talked over may hold longer than a line
        steps = steps_of(s)
        if steps:  # a stepped slide is several views; the cap applies to the longest view, not to their sum
            longest = max(estimated_seconds({"notes": st.get("notes") or (s["notes"] if j == 0 else ""), "kind": s["kind"]}, wpm) for j, st in enumerate(steps))
            if longest > cap:
                F.fail("S4", f"{s['id']}: one step holds for {longest}s, over {cap}s")
        elif s["duration_s"] > cap:
            F.fail("S4", f"{s['id']}: {s['duration_s']}s exceeds {cap}s")
    if len(non_backup) < 20:
        F.fail("S4", f"only {len(non_backup)} slides; the style wants more, shorter slides")
    if F.status_of("S4") == "unrun":
        F.ok("S4", f"{len(non_backup)} slides, {total}s")
    # S5 deliberate motion: declared only where it shows a relation; everything else uses the deck's default transition
    prev = None; morphs = trans = 0
    for s in sl:
        m = s.get("motion")
        if m and m["kind"] == "auto-animate":
            morphs += 1
            pm = prev.get("motion") if prev else None
            if pm and pm.get("kind") == "auto-animate" and pm["group"] == m["group"] and not set(m["shared"]) & set(pm["shared"]):
                F.fail("S5", f"{s['id']}: shares no element with the previous slide in group {m['group']}")
        elif m and m["kind"] == "transition":
            trans += 1
        prev = s
    if F.status_of("S5") == "unrun":
        F.ok("S5", f"{morphs} morphs and {trans} named transitions declared; the other slides use the deck default")
    for s in sl:
        if s["kind"] in ("image", "diagram"):
            floor = 12  # a picture is explained in speech, at least a sentence; code, snippet and rendered slides are examples shown while a sentence is said
            if wc(spoken_text(s)) < floor and not s.get("silent"):  # a silent picture is a second screen of the page before it
                F.fail("S6", f"{s['id']}: picture slide with {wc(spoken_text(s))} words of notes (need {floor})")
            if wc(s["on_screen"]) > 14:
                F.fail("S6", f"{s['id']}: picture slide with {wc(s['on_screen'])} words on screen")
    if F.status_of("S6") == "unrun":
        F.ok("S6")
    for s in sl:
        spoken = wc(spoken_text(s))
        if not s.get("silent") and spoken < 12 and s["kind"] not in ("code", "snippet", "rendered"):  # a one-line turn is a slide too
            F.fail("S7", f"{s['id']}: notes have {spoken} words")
    if F.status_of("S7") == "unrun":
        F.ok("S7")
    for s in sl:
        for node in [s] + list(s.get("steps") or []) + [b for st in s.get("steps") or [] for b in st.get("blocks") or []]:
            if node.get("file") and not (REPO / node["file"]).exists():
                F.fail("S8", f"{s['id']}: missing source file {node['file']}")
        if s["kind"] in ("snippet", "rendered") and not (DECK / s["doc_html"]).exists():
            F.fail("S8", f"{s['id']}: missing documentation fragment {s['doc_html']}")
        for a in (s.get("assets") or []) + ([s["qr_asset"]] if s.get("qr_asset") else []):
            if not (DECK / a).exists():
                F.fail("S8", f"{s['id']}: missing asset {a}")
    if F.status_of("S8") == "unrun":
        F.ok("S8")
    for s in sl:
        ws = [w.lower() for w in words(s["on_screen"])]
        nw = " ".join(w.lower() for w in words(s["notes"]))
        for i in range(0, max(0, len(ws) - 8)):
            if " ".join(ws[i:i + 9]) in nw:
                F.fail("S9", f"{s['id']}: on-screen text repeats the notes verbatim")
                break
    if F.status_of("S9") == "unrun":
        F.ok("S9")
    F.ok("S10", "image roles enumerated (schema)")
    F.ok("S11", "diagram labels and chart kinds required (schema)")
    for s in sl:
        # one highlight may be a bare range lighting several places under the slide's notes; with several steps,
        # every step after the first is a point of its own and needs its notes, so the speaker view never repeats
        if s["kind"] == "code" and len(hl_items(s)) > 1:
            silent = [h for h, n in list(zip(hl_lines(s), hl_notes(s)))[1:] if not n.strip()]
            if silent:
                F.fail("S12", f"{s['id']}: highlight steps {silent} have no notes; each step after the first needs what is said over it")
    if F.status_of("S12") == "unrun":
        F.ok("S12")
    F.ok("S13", "code steps bounded (schema + S2)")
    seen_concl = False
    for s in sl:
        if s["kind"] == "conclusions":
            seen_concl = True
        if s["kind"] == "backup" and not seen_concl:
            F.fail("S14", f"{s['id']}: backup slide before the conclusions")
    if F.status_of("S14") == "unrun":
        F.ok("S14")
    stars = [s["id"] for s in sl if s.get("star_moment")]
    (F.ok if len(stars) == 1 else F.fail)("S15", f"star slides: {stars}")
    c = next((s for s in sl if s["kind"] == "conclusions"), None)
    if c:
        if norm(meta["idea"]["statement"]) not in norm(c["on_screen"]):
            F.fail("S16", "conclusions slide must show the one idea verbatim")
        if re.search(r"questions\?|thank you", c["on_screen"], re.I):
            F.fail("S16", "conclusions slide must not say Questions? or Thank you")
    if F.status_of("S16") == "unrun":
        F.ok("S16")
    cta = next((s for s in sl if s["kind"] == "cta"), None)
    if cta:
        if cta["cta_url"] != meta["idea"]["call_to_action"]["url"]:
            F.fail("S17", "cta url differs from the meta")
        if not (DECK / cta["qr_asset"]).exists():
            F.fail("S17", f"missing QR asset {cta['qr_asset']}")
    if F.status_of("S17") == "unrun":
        F.ok("S17")
    tc = sl[0]
    if tc["kind"] == "title-card":
        p = DECK / tc["assets"][0]
        sz = image_size(p) if p.exists() else None
        if not sz:
            F.fail("S18", f"title card missing or unreadable: {tc['assets'][0]}")
        elif abs(sz[0] / sz[1] - 16 / 9) > 0.01:
            F.fail("S18", f"title card is {sz[0]}x{sz[1]}, not 16:9")
        else:
            F.ok("S18", f"{sz[0]}x{sz[1]}")
    dc = [s["id"] for s in sl if s.get("deliberately_complex")]
    (F.ok if len(dc) <= 1 else F.fail)("S19", f"deliberately complex slides: {dc}")
    rs = sum(1 for s in sl if s.get("restates_idea"))
    (F.ok if rs >= 3 else F.fail)("S20", f"{rs} slides restate the idea (need 3)")
    for s in sl:
        for pat in BANNED:
            if re.search(pat, s["notes"], re.I):
                F.fail("S22", f"{s['id']}: banned phrase /{pat}/")
    if F.status_of("S22") == "unrun":
        F.ok("S22")
    acc, first_ten = 0, []
    for s in sl:
        if acc <= 600:
            first_ten.append(s["notes"])
        acc += s["duration_s"]
    opening = " ".join(first_ten)
    needles = [("promise", meta["idea"]["promise"])]
    q = meta["policies"]["questions"]
    if q["announced"]:
        needles.append(("questions policy", q["wording"]))
    for what, needle in needles:
        if norm(needle) not in norm(opening):
            F.fail("S23", f"the first ten minutes of notes do not contain the {what} verbatim")
    first_two = " ".join(s["notes"] for s in sl[:2])
    if not re.search(r"thank you|alright|good (morning|afternoon)", first_two, re.I):
        F.fail("S23", "the first two slides lack the applause/attention cue")
    if not meta["policies"]["opening"]["self_introduction"] and re.search(r"my name is|i am alan|i'm alan|about me", first_two, re.I):
        F.fail("S23", "the opening introduces the speaker; policies.opening.self_introduction is false")
    if F.status_of("S23") == "unrun":
        F.ok("S23", "opening notes carry the " + " and the ".join(w for w, _ in needles) + ", and the attention cue")
    dq = [s for s in sl if s.get("dramatic_question")]
    if len(dq) > 1:
        F.fail("S24", f"more than one dramatic question slide: {[s['id'] for s in dq]}")
    for s in sl:
        if s["on_screen"].rstrip().endswith("?"):
            if not s.get("dramatic_question"):
                F.fail("S24", f"{s['id']}: question as headline")
            elif norm(s["on_screen"]) != norm(story["dramatic_question"]):
                F.fail("S24", f"{s['id']}: dramatic question differs from the story's")
    if F.status_of("S24") == "unrun":
        F.ok("S24")
    F.ok("S25", "emphasis declared on picture slides (schema)")
    we = sum(len(re.findall(r"\b(we|our|us|let's)\b", s["notes"], re.I)) for s in sl)
    (F.ok if we >= 10 else F.fail)("S26", f"'we/our/us' appears {we} times in the notes")
    wpm = meta["event"]["speaking_wpm"]
    est_total = 0
    for s in sl:
        if s["kind"] == "backup":
            continue
        est = estimated_seconds(s, wpm)
        est_total += est
        if not s.get("silent") and est > 0 and not s.get("derived_duration"):
            if s["duration_s"] < 0.6 * est:
                F.fail("S27", f"{s['id']}: {wc(s['notes'])} words of notes take ~{est}s at {wpm} wpm but the slide is given {s['duration_s']}s")
            elif s["duration_s"] > 1.6 * est and s["kind"] not in ("live", "title-card"):
                F.warn("S27", f"{s['id']}: given {s['duration_s']}s but the notes take ~{est}s; either more to say or too long on screen")
    tm = talk_minutes(meta) * 60
    if abs(est_total - tm) > 0.15 * tm:
        F.warn("S27", f"the notes take ~{est_total // 60} min to say at {wpm} wpm; the talk is {tm // 60} min; a warning while the deck is being cut down")
    if F.status_of("S27") == "unrun":
        F.ok("S27", f"notes ~{est_total // 60} min at {wpm} wpm against {tm // 60} min")


# --------------------------------------------------------------------------- html checks
def css_vars(css):
    return dict(re.findall(r"(--[a-z0-9-]+):\s*([^;]+);", css))


def to_rgb(spec, vars_):
    spec = spec.strip()
    m = re.match(r"var\((--[a-z0-9-]+)\)", spec)
    if m:
        return to_rgb(vars_[m.group(1)], vars_)
    m = re.match(r"#([0-9a-fA-F]{6})", spec)
    if m:
        h = m.group(1)
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)), 1.0
    m = re.match(r"rgba?\(([^)]+)\)", spec)
    if m:
        parts = [p.strip() for p in m.group(1).split(",")]
        rgb = tuple(int(float(p)) for p in parts[:3])
        a = float(parts[3]) if len(parts) > 3 else 1.0
        return rgb, a
    return None, None


def luminance(rgb):
    def ch(c):
        c /= 255
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = rgb
    return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)


def contrast(fg, bg):
    l1, l2 = luminance(fg), luminance(bg)
    hi, lo = max(l1, l2), min(l1, l2)
    return (hi + 0.05) / (lo + 0.05)


def check_html(html_path, slides, F):
    if not html_path.exists():
        for k in ("H1", "H2", "H3", "H4", "H5", "H6", "H7", "H8", "H9", "H10"):
            F.skip(k, "no built html; run build")
        F.warn("H11", "no build artefacts")
        return
    html = html_path.read_text(encoding="utf-8")
    css_path = DECK / "css" / "mrdocs-theme.css"
    css = css_path.read_text(encoding="utf-8") if css_path.exists() else ""
    secs = re.findall(r"^    <section\b([^>]*)>(.*?)^    </section>", html, re.M | re.S)
    ids = [re.search(r'\bid="([^"]+)"', a).group(1) if re.search(r'\bid="([^"]+)"', a) else None for a, _ in secs]
    expected = [s["id"] for s in slides["slides"]]
    base_ids = [i.split("--")[0] if i else None for i in ids]
    # collapse step sections to their slide
    collapsed = [b for j, b in enumerate(base_ids) if j == 0 or b != base_ids[j - 1]]
    if collapsed != expected:
        F.fail("H1", f"html sections {collapsed[:5]}... differ from slides {expected[:5]}...")
    for (a, body), i in zip(secs, ids):
        if '<aside class="notes">' not in body:
            F.fail("H1", f"section {i} has no notes")
    if F.status_of("H1") == "unrun":
        F.ok("H1", f"{len(secs)} sections")
    outside_panels = re.sub(r'<div class="docpanel[^"]*"[^>]*>.*?</div>\s*(?=\n      <p|\n      <aside|\n    </section>)', "", "".join(b for _, b in secs), flags=re.S)
    (F.fail if re.search(r"<(ul|ol)\b", outside_panels) else F.ok)("H2", "no bullet lists outside generated pages")
    m = re.search(r"width:\s*(\d+),\s*height:\s*(\d+)", html)
    (F.ok if m and abs(int(m.group(1)) / int(m.group(2)) - 16 / 9) < 0.01 else F.fail)("H3", "canvas 16:9")
    v = css_vars(css)
    fs = int(re.match(r"(\d+)", v.get("--r-main-font-size", "0px")).group(1))
    if fs < 40:
        F.fail("H4", f"base font {fs}px (need 40 on the 1600 canvas)")
    for rule in re.findall(r"([^{}]+)\{([^}]*text-transform:\s*uppercase[^}]*)\}", css):
        if ".cover-title" not in rule[0]:
            F.fail("H4", f"uppercase outside covers: {rule[0].strip()}")
    for rule in re.findall(r"([^{}]+)\{([^}]*font-style:\s*italic[^}]*)\}", css):
        if not any(k in rule[0] for k in ("hljs-comment", "hljs-doc-comment", "hljs-quote", "hljs-emphasis")):
            F.fail("H4", f"italic run: {rule[0].strip()}")
    fams = set(re.findall(r"family=([A-Za-z+]+)", css))
    if len(fams) > 3:
        F.fail("H4", f"{len(fams)} typefaces loaded")
    if F.status_of("H4") == "unrun":
        F.ok("H4", f"{fs}px, {len(fams)} typefaces")
    for sel in (".reveal.cover .controls", ".reveal.cover .progress", ".reveal.cover .slide-number"):
        if sel not in css:
            F.fail("H5", f"missing rule {sel}")
    if F.status_of("H5") == "unrun":
        F.ok("H5")
    for (a, body), i in zip(secs, ids):
        if 'data-state="cover"' not in a and re.search(r"<header|class=\"[^\"]*(logo|brand)", body):
            F.fail("H6", f"section {i} carries a logo or header")
    if "brand-corner" in html:
        F.fail("H6", "persistent corner logo present on content slides")
    if F.status_of("H6") == "unrun":
        F.ok("H6")
    kinds = {s["id"]: s["kind"] for s in slides["slides"]}
    bleed = {s["id"] for s in slides["slides"] if s["kind"] == "image" and s.get("fit") in ("bleed", "page")}
    for (a, _), i in zip(secs, ids):
        base = (i or "").split("--")[0]
        if "data-background" in a and kinds.get(base) not in ("title-card", "live") and base not in bleed:
            F.fail("H7", f"section {i} sets its own background")
    if F.status_of("H7") == "unrun":
        F.ok("H7")
    for j in range(1, len(secs)):
        a0, b0 = secs[j - 1]
        a1, b1 = secs[j]
        if "data-auto-animate" in a0 and "data-auto-animate" in a1:
            g0 = re.search(r'data-auto-animate-id="([^"]+)"', a0)
            g1 = re.search(r'data-auto-animate-id="([^"]+)"', a1)
            if g0 and g1 and g0.group(1) == g1.group(1):
                d0 = set(re.findall(r'data-id="([^"]+)"', b0))
                d1 = set(re.findall(r'data-id="([^"]+)"', b1))
                if not d0 & d1:
                    F.fail("H8", f"sections {ids[j-1]} and {ids[j]} auto-animate with no shared data-id")
    if F.status_of("H8") == "unrun":
        F.ok("H8")
    if re.search(r"<audio|autoplay", html):
        F.fail("H9", "audio or autoplay present")
    for t in re.findall(r'data-transition="([^"]+)"', html):
        if t not in ("none", "fade", "slide", "zoom"):
            F.fail("H9", f"transition {t}")
    zooms = len(re.findall(r'data-transition="zoom"', html))
    if zooms > 1:
        F.fail("H9", f"{zooms} zoom transitions (one allowed, for the reveal)")
    if F.status_of("H9") == "unrun":
        F.ok("H9")
    try:
        bg, _ = to_rgb(v["--cc-navy-lift"], v)
        for name in ("--cc-gold", "--cc-text", "--cc-text-70", "--cc-muted"):
            fg, a = to_rgb(v[name], v)
            if a < 1:
                fg = tuple(int(a * f + (1 - a) * b) for f, b in zip(fg, bg))
            c = contrast(fg, bg)
            if c < 4.5:
                F.fail("H10", f"{name} on canvas: {c:.1f}:1")
        if F.status_of("H10") == "unrun":
            F.ok("H10")
    except Exception as e:
        F.warn("H10", f"could not compute contrast: {e}")
    missing = [p.name for p in (html_path.parent / "deck.pdf", html_path.parent / "contact-sheet.png") if not p.exists()]
    (F.ok if not missing else F.warn)("H11", f"missing artefacts: {missing}" if missing else "pdf and contact sheet present")


# --------------------------------------------------------------------------- logs and reviews
def check_logs(logs, meta, F, strict):
    rs = logs["rehearsals"]
    tm = talk_minutes(meta)
    live = [r for r in rs if r["live"] and abs(r["timed_minutes"] - tm) <= tm * 0.1]
    novice = [r for r in rs if any(not a["knows_topic"] for a in r["audience"])]
    msg = f"{len(live)} live timed rehearsals within slot, {len(novice)} with a non-expert listener"
    (F.ok if len(live) >= 3 and novice else (F.fail if strict else F.warn))("D1", msg)
    rec = [r for r in rs if r["recorded"]]
    cap = [r for r in rec if r.get("captions_check")]
    (F.ok if rec and cap else (F.fail if strict else F.warn))("D2", f"{len(rec)} recorded, {len(cap)} with captions check")
    ck = logs["checklist"]
    ok3 = ck["equipment_tested"] and ck["pdf_backup"] and ck["clicker_contingency"]
    (F.ok if ok3 else (F.fail if strict else F.warn))("D3", f"equipment {ck['equipment_tested']}, pdf {ck['pdf_backup']}")
    (F.ok if ck.get("room") and ck["room_visited"] else F.warn)("D4", f"room: {ck.get('room')}; visited: {ck['room_visited']}")
    (F.ok if ck["lights_request"] else (F.fail if strict else F.warn))("D5", f"lights: {ck['lights_request']}")
    (F.ok if any("excited" in x.lower() for x in ck["pre_talk_ritual"]) else F.fail)("D6", "pre-talk ritual must include the 'I am excited' relabel")
    (F.ok if ck["qa_conduct_acknowledged"] else (F.fail if strict else F.warn))("D7", "Q&A conduct checklist")
    F.ok("D8", ck["feedback_plan"])
    latest = {}
    for r in logs["reviews"]:
        latest[r["check"]] = r
    for k in ("R1", "R2", "R3", "R4", "R5"):
        r = latest.get(k)
        if not r:
            (F.fail if strict else F.warn)(k, "no review recorded")
        elif r["verdict"] == "pass":
            F.ok(k, f"{r['reviewer']} {r['date']}")
        else:
            F.fail(k, f"review failed: {r.get('notes', '')}")


# --------------------------------------------------------------------------- build
def esc(s):
    return (s or "").replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def build(out_path, artifacts=False, rehearsal=False):
    """Talk mode is the default and the deliverable: no slide numbers, arrows only on hover. Rehearsal mode is a
    separate file with the speaker's aids (faint slide numbers, visible arrows); it is never what the audience sees."""
    meta = load_yaml(level_file("meta"))
    story = load_yaml(level_file("story"))
    slides = slides_of(story, meta["event"]["speaking_wpm"])["slides"]
    # the speaker view paces against the talk time, so every section's estimate is stretched (or squeezed) by one
    # factor to sum to it: a rule of three over the emitted sections, applied once the page is assembled
    timing = lambda sec: f"__T{sec:.3f}__"
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    rel = os.path.relpath(DECK, out_path.parent)
    rel = "" if rel == "." else rel + "/"
    chapters = {c["id"]: c for c in story["chapters"]}
    sections = []

    def paragraphs(raw):
        # blank lines in the YAML are paragraph breaks in the speaker view; single line breaks are soft (joined)
        paras = [" ".join(par.split()) for par in re.split(r"\n\s*\n", raw.strip()) if par.strip()]
        return "".join(f"<p>{esc(par)}</p>" for par in paras)

    def notes(s, text=None):
        """The slide's speaker notes. A highlighted block says its first highlight's notes with the slide's; the
        later highlights' notes travel with their fragments (see the script at the end of the page)."""
        raw = text if text is not None else s["notes"]
        if text is None and s.get("notes_from_highlights"):
            raw = hl_notes(s)[0]  # the joined text is for the checks; on stage the first highlight speaks first
        first = hl_notes(s)[0] if (text is None and not s.get("notes_from_highlights") and hl_notes(s)) else ""
        return '      <aside class="notes">' + paragraphs(raw) + (paragraphs(first) if first.strip() else "") + "</aside>\n"

    def attrs(s, extra=""):
        a = f' id="{s["id"]}" data-timing="{timing(s["duration_s"])}"'
        if s["kind"] in ("title-card", "cover", "conclusions", "cta"):
            a += ' data-state="cover"'
        m = s.get("motion") or {}
        if m.get("kind") == "auto-animate":
            a += f' data-auto-animate data-auto-animate-id="{m["group"]}"'
        elif m.get("kind") == "transition":
            a += f' data-transition="{m["name"]}"'
        return a + extra

    def did(s):
        return f' data-id="{s["motion"]["shared"][0]}"' if (s.get("motion") or {}).get("kind") == "auto-animate" else ""

    def fit(code):
        """Code is never shrunk to fit: nobody can read small code. Every block soft-wraps (theme), and a block
        taller than the frame scrolls through the highlight steps the plan gives it (S2 insists on them)."""
        return ""

    def step_hl(h):
        """Integers are lines lit together in the step; range strings are lit one after another, walking the block."""
        h = h or []
        if any(isinstance(x, str) for x in h):
            return "|".join(str(x) for x in h)
        return ",".join(str(x) for x in h)

    def codebox(code, lang, hl, title=None, data_id="code", step_notes=None):
        """One code frame. A title is the file the code comes from, shown as a tab above the frame, so two files on
        one slide are two frames and never one block with a comment naming the second file. Every highlight step
        after the first carries notes for the speaker view: the plan's, or the line range when the plan gave none."""
        steps = hl.split("|") if hl else []
        notes_attr = ""
        if len(steps) > 1 and any((step_notes or [])[1:]):
            notes_attr = " data-highlight-notes='" + json.dumps([paragraphs(n) for n in (step_notes or [])]).replace("'", "&#39;") + "'"
        pre = (f'<pre data-id="{data_id}"{fit(code)}{notes_attr}><code class="language-{lang}" data-trim data-line-numbers="{hl}">'
               f'{esc(code.rstrip(chr(10)))}</code></pre>')
        if title:
            return f'      <div class="codebox"><div class="code-title">{esc(title)}</div>{pre}</div>\n'
        return f"      {pre}\n"

    def doc_fragment(path, symbols=None):
        """The documentation Mr.Docs generated, as saved under assets/. The fragment arrives pre-highlighted by the
        website's build; the deck's highlighter wants plain code. `symbols` keeps only the named symbols' blocks."""
        doc = (DECK / path).read_text(encoding="utf-8") if (DECK / path).exists() else ""
        if symbols:
            blocks = re.findall(r'<div class="symbol">.*?<!-- /symbol -->', doc, re.S)
            keep = []
            for b in blocks:
                h = re.search(r'<h2 id="([^"]*)">(.*?)</h2>', b, re.S)
                name = re.sub(r"<[^>]+>", "", h.group(2)).strip() if h else ""
                if h and (h.group(1) in symbols or name in symbols or name.split("::")[-1] in symbols):
                    keep.append(b)
            doc = "\n".join(keep)
        doc = re.sub(r'<pre><code class="source-code cpp">(.*?)</code></pre>',
                     lambda m: '<pre><code class="language-cpp">' + re.sub(r"<[^>]+>", "", m.group(1)) + "</code></pre>", doc, flags=re.S)
        # the generator names an admonition's kind only in its heading; the theme needs it as a class to colour it
        return re.sub(r'<div class="admonition">(\s*<h4>)(\w+)(</h4>)',
                      lambda m: f'<div class="admonition {m.group(2).lower()}">{m.group(1)}{m.group(2)}{m.group(3)}', doc)
    for s in slides:
        k = s["kind"]
        body = ""
        extra = ""
        cls = []
        if k == "title-card":
            extra = (f' data-background-image="{rel}{s["assets"][0]}" data-background-size="contain"'
                     ' data-background-repeat="no-repeat" data-background-position="center" data-background-color="#1F2844"')
            cls.append("title-card")
        elif k == "cover":
            cls.append("centered")
            for a in s.get("assets") or []:
                body += f'      <img class="title-banner" src="{rel}{a}" alt="">\n'
            if s["on_screen"]:
                body += f'      <h1 class="cover-title"{did(s)}>{esc(s["on_screen"])}</h1>\n'
        elif k == "word":
            cls.append("centered")
            body += f'      <h1 class="word"{did(s)}>{esc(s["on_screen"])}</h1>\n'
        elif k == "line":
            cls.append("centered")
            body += f'      <h2 class="line"{did(s)}>{esc(s["on_screen"])}</h2>\n'
        elif k == "conclusions":
            cls += ["centered", "conclusions"]
            # the closing hero: the same banner as the cover, the one idea under it, and the QR for the questions
            for a in s.get("assets") or []:
                body += f'      <img class="title-banner" src="{rel}{a}" alt="">\n'
            body += f'      <h2 class="cover-title"{did(s)}>{esc(s["on_screen"])}</h2>\n'
        elif k == "cta":
            cls.append("centered")
            body += ('      <div class="qr-wrap">\n'
                     f'        <div class="qr"><img src="{rel}{s["qr_asset"]}" alt="QR code"></div>\n'
                     f'        <p><a href="{s["cta_url"]}">{esc(s["on_screen"] or s["cta_url"])}</a></p>\n'
                     '      </div>\n')
        elif k == "roadmap":
            cls.append("centered")
            body += '      <div class="roadmap">\n'
            for cid in [c.strip() for c in s["on_screen"].split("·")]:
                c = chapters.get(cid)
                if c:
                    body += (f'        <figure data-id="chapter-{cid}"><img src="{rel}{c["mnemonic_image"]}" alt="">'
                             f'<figcaption>{esc(c["label"])}</figcaption></figure>\n')
            body += "      </div>\n"
        elif k == "image" and s.get("fit") in ("bleed", "page"):
            cls.append("bleed")
            # bleed paints its own flat colour behind a landscape capture; a page is portrait and shows the canvas at its sides
            colour = ' data-background-color="#1F2844"' if s["fit"] == "bleed" else ""
            extra = (f' data-background-image="{rel}{s["assets"][0]}" data-background-size="contain"'
                     f' data-background-repeat="no-repeat" data-background-position="center"{colour}')
            if s["on_screen"]:  # the page fills the slide, so its caption sits in a chip over it
                body += f'      <div class="url-chip">{esc(s["on_screen"])}</div>\n'
        elif k == "image":
            cls.append("centered")
            for a in s["assets"]:
                body += f'      <img class="hero-image"{did(s)} src="{rel}{a}" alt="">\n'
            if s["on_screen"]:
                body += f'      <p class="line">{esc(s["on_screen"])}</p>\n'
        elif k == "diagram":
            body += f'      <pre class="mermaid"{did(s)}>\n{s.get("diagram", "")}\n      </pre>\n'
            if s["on_screen"]:
                body += f'      <p class="line">{esc(s["on_screen"])}</p>\n'
        elif k == "table":
            tb = s["table"]
            hl = set(tb.get("highlight_rows") or []); hc = set(tb.get("highlight_cols") or [])
            glyph = {"yes": '<span class="cell-yes" title="yes">\u2713</span>', "no": '<span class="cell-no" title="no">\u2715</span>',
                     "maybe": '<span class="cell-maybe" title="maybe">?</span>', "": ""}
            cell = lambda c, j: esc(c) if j == 0 else glyph.get(str(c).strip().lower(), esc(c))
            colcls = lambda ci: ' class="col-diff"' if (ci + 1) in hc else ""
            if s["on_screen"]:
                body += f'      <p class="line table-caption">{esc(s["on_screen"])}</p>\n'
            has = " has-diff" if hl or hc else ""
            body += f'      <table class="compare{has}" data-id="table">\n        <thead><tr>' + "".join(f'<th data-id="h{ci}"{colcls(ci)}>{esc(h)}</th>' for ci, h in enumerate(tb["header"])) + "</tr></thead>\n        <tbody>\n"
            for i, row in enumerate(tb["rows"], 1):
                cls_attr = ' class="diff"' if i in hl else ""
                body += f'          <tr data-id="r{i}"{cls_attr}>' + "".join(f'<td data-id="c{i}-{j}"{colcls(j)}>{cell(c, j)}</td>' for j, c in enumerate(row)) + "</tr>\n"
            body += "        </tbody>\n      </table>\n"
        elif k == "live":
            url = s["live_url"] if re.match(r"^https?://", s["live_url"]) else rel + s["live_url"]
            extra = f' data-background-iframe="{url}" data-background-interactive'
            body += f'      <div class="url-chip">{esc(s["on_screen"] or s["live_url"])}</div>\n'
        elif k == "blank":
            cls.append("centered")
        elif k == "backup":
            cls.append("backup")
            if s["on_screen"]:
                body += f'      <h2 class="line">{esc(s["on_screen"])}</h2>\n'
            for a in s.get("assets") or []:
                body += f'      <img class="hero-image" src="{rel}{a}" alt="">\n'
        if k == "table" and s["table"].get("steps"):
            tb = s["table"]; st_all = tb["steps"]
            glyph = {"yes": '<span class="cell-yes" title="yes">\u2713</span>', "no": '<span class="cell-no" title="no">\u2715</span>',
                     "maybe": '<span class="cell-maybe" title="maybe">?</span>', "": ""}
            cell = lambda c, j: esc(c) if j == 0 else glyph.get(str(c).strip().lower(), esc(c))
            per = max(3, s["duration_s"] // len(st_all))
            for j, st in enumerate(st_all, 1):
                sid = s["id"] if j == 1 else f'{s["id"]}--{j}'
                rows, cols = set(st.get("rows") or []), set(st.get("cols") or [])
                grp = s["motion"]["group"] if (s.get("motion") or {}).get("kind") == "auto-animate" else s["id"]
                a = f' id="{sid}" data-timing="{timing(estimated_seconds({"notes": st.get("notes", ""), "kind": "table"}, meta["event"]["speaking_wpm"]))}" data-auto-animate data-auto-animate-id="{grp}"'
                colcls = lambda ci: ' class="col-diff"' if (ci + 1) in cols else ""
                has = " has-diff" if rows or cols else ""
                tbl = f'      <table class="compare{has}" data-id="table">\n        <thead><tr>' + "".join(
                    f'<th data-id="h{ci}"{colcls(ci)}>{esc(h)}</th>' for ci, h in enumerate(tb["header"])) + "</tr></thead>\n        <tbody>\n"
                for ri, row in enumerate(tb["rows"], 1):
                    rowcls = ' class="diff"' if ri in rows else ""
                    tbl += f'          <tr data-id="r{ri}"{rowcls}>' + "".join(f'<td data-id="c{ri}-{ci}"{colcls(ci)}>{cell(c, ci)}</td>' for ci, c in enumerate(row)) + "</tr>\n"
                tbl += "        </tbody>\n      </table>\n"
                sections.append(f"    <section{a}>\n" + (f'      <p class="line table-caption" data-id="caption">{esc(s["on_screen"])}</p>\n' if s["on_screen"] else "")
                                + tbl + notes(s, st.get("notes") or (s["notes"] if j == 1 else "")) + "    </section>\n")
            continue
        if k == "code" and s.get("code") is not None:
            hl = "|".join(hl_lines(s))
            lang = s.get("language", "cpp")

            body += (f'      <p class="line" data-id="assertion">{esc(s["on_screen"])}</p>\n' if s["on_screen"] else "")
            body += codebox(s["code"], lang, hl, s.get("title"), step_notes=hl_notes(s))
        elif k == "snippet":
            doc = doc_fragment(s["doc_html"])
            body += ('      <div class="snippet">\n'
                     f'        <pre data-id="code"><code class="language-{s.get("language", "cpp")}" data-trim>{esc(s["source"].rstrip(chr(10)))}</code></pre>\n'
                     f'        <div class="docpanel" data-id="docpanel">{doc}</div>\n      </div>\n')
            if s["on_screen"]:
                body += f'      <p class="line">{esc(s["on_screen"])}</p>\n'
        elif k == "rendered":
            body += f'      <div class="docpanel rendered" data-id="docpanel">{doc_fragment(s["doc_html"], s.get("symbols"))}</div>\n'
            if s["on_screen"]:
                body += f'      <p class="line">{esc(s["on_screen"])}</p>\n'
        if k == "code" and s.get("steps"):
            for j, st in enumerate(s["steps"], 1):
                sid = s["id"] if j == 1 else f'{s["id"]}--{j}'
                hl = step_hl(st.get("highlight"))
                lang = st.get("language", "cpp")
                a = f' id="{sid}" data-timing="{timing(estimated_seconds({"notes": st.get("notes") or (s["notes"] if j == 1 else ""), "kind": "code"}, meta["event"]["speaking_wpm"]))}" data-auto-animate data-auto-animate-id="{s["motion"]["group"] if (s.get("motion") or {}).get("kind") == "auto-animate" else s["id"]}"'
                if st.get("blocks"):
                    frames = "".join(codebox(b["code"], b.get("language", "cpp"), step_hl(b.get("highlight")),
                                             b.get("title"), "code" if bi == 1 else f"code-{bi}")
                                     for bi, b in enumerate(st["blocks"], 1))
                    frames = '      <div class="blocks">\n' + frames + "      </div>\n"
                else:
                    frames = codebox(st["code"], lang, hl, st.get("title"))
                sections.append(
                    f"    <section{a}>\n"
                    + (f'      <p class="line" data-id="assertion">{esc(s["on_screen"])}</p>\n' if s["on_screen"] else "")
                    + frames
                    + notes(s, st.get("notes") or (s["notes"] if j == 1 else ""))
                    + "    </section>\n")
            continue
        if s.get("qr_asset") and k != "cta":
            body += f'      <div class="qr qr-corner"><img src="{rel}{s["qr_asset"]}" alt="QR code"></div>\n'
        cl = f' class="{" ".join(cls)}"' if cls else ""
        sections.append(f"    <section{attrs(s, extra)}{cl}>\n{body}{notes(s)}    </section>\n")

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{esc(meta["talk_title"])}</title>
  <link rel="icon" href="{rel}assets/mrdocs-favicon.svg">
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/reveal.js@5.1.0/dist/reveal.css">
  <link rel="stylesheet" href="{rel}css/mrdocs-theme.css">
  <style>
    .reveal h1.word {{ font-size: 3.2em; font-weight: 700; color: var(--cc-gold); }}
    .reveal h2.line {{ font-size: 1.5em; font-weight: 500; color: var(--cc-white); line-height: 1.3; }}
    .reveal .hero-image {{ max-height: 70vh; max-width: 92%; }}
    .reveal .roadmap {{ display: flex; gap: 1.2em; justify-content: center; }}
    .reveal .roadmap figure {{ margin: 0; text-align: center; }}
    .reveal .roadmap img {{ height: 180px; }}
    .reveal .roadmap figcaption {{ font-size: 0.6em; color: var(--cc-text-70); margin-top: 0.4em; }}
    .reveal .backup h2.line {{ color: var(--cc-muted); }}
  </style>
</head>
<body{' class="rehearsal"' if rehearsal else ''}>
<div class="reveal">
  <div class="slides">
{"".join(sections)}  </div>
</div>
<script src="https://cdn.jsdelivr.net/npm/reveal.js@5.1.0/dist/reveal.js"></script>
<script src="https://cdn.jsdelivr.net/npm/reveal.js@5.1.0/plugin/notes/notes.js"></script>
<script src="https://cdn.jsdelivr.net/npm/reveal.js@5.1.0/plugin/highlight/highlight.js"></script>
<script src="{rel}vendor/mrdocs-highlight.js"></script>
<script src="https://cdn.jsdelivr.net/npm/reveal.js@5.1.0/plugin/zoom/zoom.js"></script>
<script src="https://cdn.jsdelivr.net/npm/reveal.js@5.1.0/plugin/math/math.js"></script>
<script>
  Reveal.initialize({{
    hash: true,
    slideNumber: {"'c/t'" if (rehearsal or meta["policies"]["slide_numbers_visible"]) else "false"},
    showNotes: false,
    transition: 'slide',
    backgroundTransition: 'fade',
    controls: true,
    progress: true,
    center: true,
    width: 1600,
    height: 900,
    margin: 0.055,
    // the docs' own highlighter colours every block first (doc comments get their tags and code spans lit, as on
    // the documentation site); reveal's plugin then only adds line numbers and highlight steps
    highlight: {{ beforeHighlight: () => document.querySelectorAll('.reveal pre code').forEach(block => {{
      window.mrdocsHighlight.highlightBlock(block);
      block.className = 'nohighlight ' + block.className; block.dataset.highlighted = 'yes';  // reveal's highlighter skips it
    }}) }},
    autoAnimateEasing: 'ease',
    autoAnimateDuration: 0.7,
    defaultTiming: 60,
    totalTime: {talk_minutes(meta) * 60},
    plugins: [ RevealHighlight, RevealNotes, RevealZoom, RevealMath.KaTeX ]
  }});
  const toggleCover = (slide) => document.querySelector('.reveal').classList.toggle('cover',
      slide && slide.getAttribute('data-state') === 'cover');
  Reveal.on('slidechanged', (e) => toggleCover(e.currentSlide));
  Reveal.on('ready', (e) => toggleCover(e.currentSlide));
</script>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs';
  mermaid.initialize({{ startOnLoad: false, theme: 'base', securityLevel: 'loose', fontFamily: 'Lexend, sans-serif',
    flowchart: {{ htmlLabels: true, useMaxWidth: true, curve: 'basis', padding: 14 }},
    quadrantChart: {{ titleFontSize: 26, pointLabelFontSize: 18, quadrantLabelFontSize: 18, xAxisLabelFontSize: 18, yAxisLabelFontSize: 18, pointRadius: 7 }},
    themeVariables: {{ fontFamily: 'Lexend, sans-serif', primaryColor: '#2C3A61', primaryTextColor: '#E8ECF6', primaryBorderColor: '#FFBB33',
      lineColor: '#FFBB33', secondaryColor: '#1F2844', tertiaryColor: '#1B2440', quadrantPointFill: '#FF8933', quadrantTitleFill: '#FFBB33',
      quadrantXAxisTextFill: '#AEBAD6', quadrantYAxisTextFill: '#AEBAD6' }} }});
  try {{ await document.fonts.load('400 1em "Lexend"'); await document.fonts.ready; }} catch (e) {{}}
  // reveal keeps every slide but the current one out of layout, and mermaid measures text with getBBox, which
  // returns zero boxes there: the diagram comes out as one huge rectangle. So every diagram is rendered inside a
  // staging box that is laid out (off screen, invisible) and moved back to its slide afterwards.
  const stage = document.createElement('div');
  stage.style.cssText = 'position:fixed;left:-100000px;top:0;width:1600px;visibility:hidden;pointer-events:none';
  document.querySelector('.reveal').appendChild(stage);
  const pres = [...document.querySelectorAll('.reveal pre.mermaid')];
  const homes = pres.map(p => ({{ parent: p.parentNode, next: p.nextSibling }}));
  pres.forEach(p => stage.appendChild(p));
  await mermaid.run({{ nodes: pres }});
  pres.forEach((p, i) => homes[i].parent.insertBefore(p, homes[i].next));
  stage.remove();
  // the diagrams changed the slides' heights after reveal centred them
  // a generated page a little taller than its frame shrinks until every line is on screen; one that would still
  // not fit at the smallest readable size stays at the usual size, so what is on screen can at least be read
  for (const panel of document.querySelectorAll('.docpanel.rendered')) {{
    let size = 0.5;
    while (panel.scrollHeight > panel.clientHeight + 2 && size > 0.4) {{
      size -= 0.02; panel.style.fontSize = size.toFixed(2) + 'em';
    }}
    if (panel.scrollHeight > panel.clientHeight + 2) panel.style.fontSize = '';
  }}
  const relayout = () => Reveal.layout();
  if (Reveal.isReady()) relayout(); else Reveal.on('ready', relayout);
  // a highlight is a point being made: each one carries its own notes, so the speaker view changes with the fragment
  const carryNotes = () => document.querySelectorAll('pre[data-highlight-notes]').forEach(pre => {{
    const notes = JSON.parse(pre.getAttribute('data-highlight-notes'));
    pre.querySelectorAll('code.fragment').forEach(frag => {{
      const i = parseInt(frag.getAttribute('data-fragment-index'), 10) + 1;
      if (notes[i] && !frag.querySelector('aside.notes')) {{
        const aside = document.createElement('aside'); aside.className = 'notes'; aside.innerHTML = notes[i];
        frag.appendChild(aside);
      }}
    }});
  }});
  if (Reveal.isReady()) carryNotes(); else Reveal.on('ready', carryNotes);
</script>
</body>
</html>
"""
    marks = [float(m) for m in re.findall(r"__T([\d.]+)__", html)]
    scale = talk_minutes(meta) * 60 / (sum(marks) or 1)
    timed = [max(3, round(m * scale)) for m in marks]
    if timed:  # rounding and the 3 s floors leave a few seconds; the longest section absorbs them
        timed[timed.index(max(timed))] += talk_minutes(meta) * 60 - sum(timed)
    it = iter(timed)
    html = re.sub(r"__T([\d.]+)__", lambda m: str(next(it)), html)
    out_path.write_text(html, encoding="utf-8")
    print(f"built {out_path} ({len(sections)} sections)")
    if artifacts:
        make_artifacts(out_path)


def make_artifacts(html_path):
    """PDF via headless Chrome print-pdf and a contact sheet via per-slide screenshots."""
    chrome = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
    if not Path(chrome).exists() or not shutil.which("magick"):
        print("artefacts skipped: need Chrome and ImageMagick")
        return
    import http.server, socketserver, threading
    root = DECK
    rel_html = html_path.relative_to(root).as_posix()
    handler = lambda *a, **k: http.server.SimpleHTTPRequestHandler(*a, directory=str(root), **k)
    with socketserver.TCPServer(("127.0.0.1", 0), handler) as srv:
        port = srv.server_address[1]
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        base = f"http://127.0.0.1:{port}/{rel_html}"
        subprocess.run([chrome, "--headless", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                        "--window-size=1600,900", "--virtual-time-budget=15000", "--no-pdf-header-footer",
                        f"--print-to-pdf={html_path.parent / 'deck.pdf'}", f"{base}?print-pdf"], capture_output=True)
        n = len(slides_of(load_yaml(level_file("story")))["slides"])
        shots = []
        for i in range(n):
            p = html_path.parent / f"shot-{i:02d}.png"
            subprocess.run([chrome, "--headless", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                            "--window-size=800,450", "--virtual-time-budget=12000", f"--screenshot={p}", f"{base}#/{i}"], capture_output=True)
            if p.exists():
                shots.append(str(p))
        if shots:
            subprocess.run(["magick", "montage", *shots, "-tile", "6x", "-geometry", "+8+8", "-background", "#1F2844",
                            str(html_path.parent / "contact-sheet.png")], capture_output=True)
            for s in shots:
                os.remove(s)
        srv.shutdown()
    print("artefacts: deck.pdf, contact-sheet.png")


# --------------------------------------------------------------------------- yaml output style
class BlockDumper(yaml.SafeDumper):
    """Long strings as folded blocks, multi-line strings as literal blocks, so the files read as prose."""


def _block_str(dumper, s):
    if "\n" in s.rstrip("\n"):
        return dumper.represent_scalar("tag:yaml.org,2002:str", s, style="|")
    if len(s) > 100:  # only fold what would not fit on one line at the dump width (110 columns minus key and indent)
        return dumper.represent_scalar("tag:yaml.org,2002:str", s, style=">")
    return dumper.represent_scalar("tag:yaml.org,2002:str", s)


BlockDumper.add_representer(str, _block_str)


# --------------------------------------------------------------------------- criteria registry
def regenerate_criteria():
    sys.path.insert(0, str(TOOLS))
    import criteria_map  # noqa: E402
    src = RESEARCH.read_text(encoding="utf-8").splitlines()
    sec = secnum = None
    counts = {}
    crits = []
    for ln in src:
        mh = re.match(r"^(##+) (.*)", ln)
        if mh:
            title = mh.group(2).strip()
            mm = re.match(r"^(\d+(?:\.\d+)?)\.? ", title)
            secnum = mm.group(1) if mm else None
            sec = title
            continue
        if ln.startswith("- ") and secnum:
            counts[secnum] = counts.get(secnum, 0) + 1
            cid = f"C{secnum.replace('.', '-')}-{counts[secnum]:02d}"
            text = ln[2:].strip()
            tags = sorted(set(re.findall(r"`\[([A-Za-z]+\d{4})", text)))
            marks = re.findall(r"\*\*\(([^)]+)\)\*\*", text)
            plain = re.sub(r"\*\*\([^)]*\)\*\*", "", text)
            plain = re.sub(r"`\[[^\]]+\]`", "", plain)
            plain = re.sub(r"\*\*", "", plain)
            plain = re.sub(r"\s+", " ", plain).strip()
            sentences = re.split(r"(?<=[.!?])\s", plain, 2)
            first = sentences[0]
            if len(first) < 20 and len(sentences) > 1:  # a one-word imperative ("Cycle.") needs the sentence that explains it
                first = " ".join(sentences[:2])
            checks, note = criteria_map.MAP.get(cid, (["TODO"], "unmapped"))
            kinds = sorted(set(criteria_map.CHECKS[c][1] for c in checks if c in criteria_map.CHECKS))
            levels = sorted(set(criteria_map.CHECKS[c][0] for c in checks if c in criteria_map.CHECKS))
            crit = {"id": cid, "section": sec, "claim": first, "sources": tags, "evidence": marks[0] if marks else "",
                    "evidence_level": evidence_level(marks[0] if marks else ""),
                    "checks": checks, "levels": levels, "enforcement": kinds}
            if note:
                crit["note"] = note
            crits.append(crit)
    doc = {"about": "Every claim of research/HOW-TO-GIVE-GREAT-TALKS.md, one entry each, with the check(s) that enforce it and the pipeline level where that happens. Generated by tools/pipeline.py criteria from tools/criteria_map.py; edit the map, not this file.",
           "checks": {k: {"level": v[0], "kind": v[1], "what": v[2]} for k, v in criteria_map.CHECKS.items()},
           "criteria": crits}
    header = ("# yaml-language-server: $schema=../schemas/0-criteria.schema.yml\n"
              "# Level 0: the research criteria. GENERATED by `pipeline.py criteria` from research/HOW-TO-GIVE-GREAT-TALKS.md and\n"
              "# tools/criteria_map.py; edit those, not this file. Schema: ../schemas/0-criteria.schema.yml\n")
    level_file("criteria").write_text(header + yaml.dump(doc, Dumper=BlockDumper, sort_keys=False, allow_unicode=True, width=110), encoding="utf-8")
    todo = [c["id"] for c in crits if c["checks"] == ["TODO"]]
    print(f"{level_file('criteria').name}: {len(crits)} criteria, {len(doc['checks'])} checks, unmapped: {todo}")


def evidence_level(mark):
    """The strongest evidence kind named in a research evidence mark, normalised for the schema's enum."""
    m = mark.lower()
    for key, out in (("meta", "meta"), ("exp,", "exp"), ("exp;", "exp"), ("exp ", "exp"), ("obs", "obs"),
                     ("expert consensus", "expert-consensus"), ("expert", "expert"), ("own", "own")):
        if m == key.strip(",; ") or m.startswith(key) or f"; {key}" in m or f", {key}" in m:
            if key.startswith("exp") and "expert" in m.split(";")[0].split(",")[0] and not m.startswith("exp,") and not m.startswith("exp;") and m.split(",")[0].split(";")[0].strip() != "exp":
                continue
            return out
    return "none" if not m.strip() else "expert"


# --------------------------------------------------------------------------- driver
def run_validate(strict, html_path):
    F = Findings()
    data = {}
    crit = load_yaml(level_file("criteria"))
    validate_schema(crit, load_schema("criteria"), "criteria", F)
    unknown = sorted({k for c in crit["criteria"] for k in c["checks"]} - set(crit["checks"]))
    if unknown:
        F.fail("schema:criteria", f"criteria name checks that do not exist: {unknown}")
    for name in ("meta", "story"):
        p = level_file(name)
        if not p.exists():
            F.fail(f"schema:{name}", f"missing {p}")
            continue
        data[name] = load_yaml(p)
        validate_schema(data[name], load_schema(name), name, F)
    logs = load_yaml(LOGS) if LOGS.exists() else None
    if logs is None:
        F.fail("schema:logs", f"missing {LOGS}")
    else:
        validate_schema(logs, load_schema("logs"), "logs", F)
    if all(F.status_of(f"schema:{n}") == "ok" for n in ("meta", "story")):
        data["beats"] = beats_of(data["story"])
        data["slides"] = slides_of(data["story"], data["meta"]["event"]["speaking_wpm"])
        check_meta(data["meta"], F, data["slides"])
        check_story(data["story"], data["meta"], F, strict, data["slides"], data["beats"])
        check_beats(data["beats"], data["story"], data["meta"], data["slides"], F)
        check_slides(data["slides"], data["beats"], data["story"], data["meta"], F)
        check_html(html_path, data["slides"], F)
    else:
        for k in ("P", "S", "H"):
            F.skip(f"{k}*", "schema errors above; level checks not run")
    if logs is not None and F.status_of("schema:logs") == "ok" and "meta" in data:
        check_logs(logs, data["meta"], F, strict)
    return F


def print_findings(F, strict):
    order = {"fail": 0, "warn": 1, "skip": 2, "ok": 3}
    for c, s, m in sorted(F.items, key=lambda x: (order[x[1]], x[0])):
        if s == "ok" and not m:
            continue
        print(f"  {s.upper():5s} {c:12s} {m}")
    fails = [x for x in F.items if x[1] == "fail"]
    warns = [x for x in F.items if x[1] == "warn"]
    print(f"\n{len(fails)} failures, {len(warns)} warnings, {sum(1 for x in F.items if x[1]=='ok')} ok")
    return 1 if fails or (strict and warns) else 0


def print_report(F):
    reg = load_yaml(level_file("criteria"))
    rows = []
    for c in reg["criteria"]:
        sts = []
        for k in c["checks"]:
            sts.append("n/a" if k == "N/A" else F.status_of(k))
        if all(s == "n/a" for s in sts):
            agg = "n/a"
        elif "fail" in sts:
            agg = "FAIL"
        elif "unrun" in sts or "skip" in sts:
            agg = "pending"
        elif "warn" in sts:
            agg = "warn"
        else:
            agg = "ok"
        rows.append((c["id"], agg, ",".join(c["checks"]), c["claim"][:70]))
    from collections import Counter
    cnt = Counter(r[1] for r in rows)
    print("\nCriteria coverage:", dict(cnt))
    for r in rows:
        if r[1] != "ok":
            print(f"  {r[1]:8s} {r[0]:9s} [{r[2]}] {r[3]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    v = sub.add_parser("validate")
    v.add_argument("--strict", action="store_true")
    v.add_argument("--html", default=str(BUILD / "index.html"))
    b = sub.add_parser("build")
    b.add_argument("--out", default=None, help="default plan/build/index.html, or plan/build/rehearsal.html with --rehearsal")
    b.add_argument("--artifacts", action="store_true")
    b.add_argument("--rehearsal", action="store_true", help="speaker's aids on: faint slide numbers, visible arrows; writes a separate file")
    r = sub.add_parser("report")
    r.add_argument("--strict", action="store_true")
    r.add_argument("--html", default=str(BUILD / "index.html"))
    sub.add_parser("criteria")
    sub.add_parser("pace")
    sub.add_parser("schemas")
    a = ap.parse_args()
    if a.cmd == "criteria":
        regenerate_criteria()
        return 0
    if a.cmd == "build":
        build(a.out or str(BUILD / ("rehearsal.html" if a.rehearsal else "index.html")), a.artifacts, a.rehearsal)
        return 0
    if a.cmd == "schemas":
        export_schemas()
        return 0
    if a.cmd == "pace":
        story = load_yaml(level_file("story"))
        meta = load_yaml(level_file("meta"))
        print_pace(meta, beats_of(story), slides_of(story, meta["event"]["speaking_wpm"]))
        return 0
    F = run_validate(a.strict, Path(a.html))
    code = print_findings(F, a.strict)
    if a.cmd == "report":
        print_report(F)
    return code


if __name__ == "__main__":
    sys.exit(main())
