"""Regenerate every schema description as: what the field holds; why it exists; evidence (criteria, strongest first); checks that consume it. Run from plan/: python3 tools/describe.py. The per-field spec lives in SPEC below; the evidence comes from levels/0-criteria.yml."""
import sys, yaml, pathlib
sys.path.insert(0, "tools")
import criteria_map as cm
from pipeline import BlockDumper

reg = yaml.safe_load(open("levels/0-criteria.yml"))["criteria"]
by_check = {}
for c in reg:
    for k in c["checks"]:
        by_check.setdefault(k, []).append(c)
RANK = {"meta": 0, "exp": 1, "obs": 2, "expert consensus": 3, "expert": 4, "own": 5, "none": 6}
NAME = {"meta": "meta-analysis", "exp": "experiment", "obs": "observational", "expert consensus": "expert consensus", "expert": "expert opinion", "own": "own experience", "none": "no evidence mark"}

def level(c):
    return {"expert-consensus": "expert consensus"}.get(c["evidence_level"], c["evidence_level"])

def evidence(checks):
    seen = {}
    for k in checks:
        for c in by_check.get(k, []):
            seen[c["id"]] = c
    if not seen:
        return "No research criterion behind it."
    cs = sorted(seen.values(), key=lambda c: (RANK[level(c)], c["id"]))
    strongest = NAME[level(cs[0])]
    shown = cs if len(cs) <= 6 else cs[:5]
    def tag(c):
        lv = NAME[level(c)]
        src = ", ".join(c["sources"]) or "no source tag"
        return f"{c['id']} ({lv}; {src})"
    parts = [tag(c) for c in shown]
    tail = "" if len(cs) <= 6 else f"; and {len(cs) - 5} more in levels/0-criteria.yml under the checks below"
    return f"Evidence, strongest first ({strongest}): " + "; ".join(parts) + tail + "."

def checks_str(checks):
    if not checks: return ""
    def short(s):
        s = s.split(" (")[0].split(";")[0]
        if len(s) > 90: s = s[:90].rsplit(" ", 1)[0]
        return s
    return " Checks: " + ", ".join(f"{k} ({cm.CHECKS[k][1]}: {short(cm.CHECKS[k][2])})" if k in cm.CHECKS else k for k in checks) + "."

# path -> dict(why=..., checks=[...], what=optional replacement for the first sentence(s), note=optional)
SPEC = {}
def S(path, why=None, checks=(), what=None, note=None):
    SPEC[path] = dict(why=why, checks=list(checks), what=what, note=note)

# ---------------------------------------------------------------- meta
S("talk_title", "The deck, the programme and the recording's title must agree, and the source is the abstract as submitted, not memory.", [], note="Bookkeeping: carried, not checked.")
S("session_url", "Every slot fact in event comes from this page, so it is kept next to them to be re-checked.", [], note="Bookkeeping: carried, not checked.")
S("event", "Everything downstream is sized from the slot: the beats must sum to the talk time, the slide count and pace are judged against it, and the canvas shape is fixed by it.", ["M8", "P18", "S4", "S27", "H3"])
S("event.name", "Names the event the other facts belong to.", [], note="Bookkeeping.")
S("event.date", "Fixes which day the rehearsal and preparation logs count down to.", [], note="Bookkeeping.")
S("event.slot_start", "Recorded so the time of day is known when planning energy and the pre-talk ritual.", [], note="Bookkeeping.")
S("event.duration_min", "The talk time is this minus the question and buffer minutes; beats, slide durations and the notes' speaking time are all checked against that number, and rehearsals are timed against it.", ["P18", "S4", "S27", "D1"])
S("event.qa_min", "Questions are kept to the end so they do not break the flow, and the minutes for them have to come out of the slot before the talk is sized.", ["M7", "P18"])
S("event.recorded", "When the session is published, the recording is the larger audience: the palette follows the broadcast frame, type is sized for a video player, and the delivery review is done on a recording.", ["D2"], note="Read by the design decisions in DESIGN-NOTES.md; the checks that depend on it are the theme checks (H4, H10).")
S("event.aspect", "The broadcast frame is 16:9; the canvas and the title card are checked against it so nothing is letterboxed in the recording.", ["H3", "S18"])
S("event.buffer_min", "Two live demos and a room can add minutes; planning to fill the slot to the second makes every overrun a cut at the end. The number is a decision, not from the research.", ["P18"])
S("event.speaking_wpm", "Turns the notes into an estimate of how long each slide takes to say, so under-written and over-full slides are caught before rehearsal. The research gives no best rate; its only direction is slower, so the value is chosen to make estimates err long, and is replaced by the measured rate once a rehearsal is recorded.",
  ["S27"], what="The speaker's pace in words per minute.")
S("mode", "A talk that exposes an idea and a lecture that teaches a skill want different pacing and different media; the pipeline is built for expose and says so here so nobody reads the thresholds as advice for teaching.", ["M8"],
  what="What the talk is trying to do. 'expose' means the audience should leave knowing the thing exists, what it is and why it matters, which is what a conference talk does; 'teach' means they should leave able to do it, which wants a board and a slower pace.")
S("audience", "Every later choice (what to explain, what to assume, what to put on screen) is made for a specific audience, and the research's most repeated advice is to know who they are before writing anything.", ["M2"])
S("audience.who", "Names the groups the talk is for, in the room and on the recording, so 'the audience' is never abstract.", ["M2"])
S("audience.share", "The idea has to be built from what the room already has; the shared experience named here is that ground, and the story is checked to open on it.", ["M2", "P13"])
S("audience.know", "Separates what can be assumed from what must be introduced, so the talk neither explains the obvious nor uses a concept the room lacks.", ["M2"])
S("audience.expert", "An expert audience reads a technical picture unaided, so the notes should add what the picture does not say rather than describe it; a redundant narration is what the redundancy research warns against. Reviewed, not measured.", ["R4"])
S("speaker_ownership", "Confidence comes from competence; the research asks that the speaker have used or built the thing, and writing the evidence down settles the fear of being caught out before the talk is written.", ["M9", "R5"])
S("idea", what="The idea and what the audience gets from it, kept together because they are one thing said in several registers.", why="One idea, in several registers on purpose, so the talk can return to it without repeating one sentence: the statement is shown last, the promise is said first, the take-home is how it is retold, the call to action is what is done about it.", checks=["M1", "M3", "M4", "M5", "M6", "M10"])
S("idea.statement", "A talk builds one idea; a short statement is what can be repeated by someone who heard it once. It is the last thing on screen, it is restated across the talk, and the take-home and the story's insight are checked to be other words than these.", ["M1", "P5", "S16", "S20"])
S("idea.status_quo", "Persuasion moves between what is and what could be; the story is checked to open on the gap between them and to alternate across it.", ["M5", "P2"])
S("idea.what_could_be", "The other side of the gap; without it the talk has a complaint but no destination.", ["M5", "P2"])
S("idea.promise", "The reason to stay, said near the start: what the listener will be able to do afterwards. The opening notes must contain it verbatim.", ["M4", "S23"])
S("idea.benefit", "An idea is worth a talk only if someone other than the speaker gains from it; writing down who and how is the test.", ["M10"])
S("idea.take_home", "Three points is what a listener can carry away; framing them as problem, proof and action keeps them from being the statement said three times, and each must be said aloud somewhere or it cannot be taken home.", ["M3"])
S("idea.take_home.problem", "The listener has to recognise the problem as their own before caring about the proof.", ["M3"])
S("idea.take_home.proof", "One checkable instance beats a list of features; it is what gets repeated to a colleague.", ["M3"])
S("idea.take_home.action", "A take-home that includes something to do tonight turns the talk into behaviour.", ["M3"])
S("idea.call_to_action", "One ask, not a list; the audience leaves with one thing to do, and the call-to-action slide is checked to carry it.", ["M6", "S17"])
S("idea.call_to_action.text", "Worded as it will be said so the slide and the speech agree.", ["M6"])
S("idea.call_to_action.url", "The one address the ask sends people to; the slide must show this url and a QR code that exists.", ["S17"])
S("handout", "Slides that work as a handout do not work as slides; deciding the handout separately frees the slides to be for the room.", ["M11", "S14"])
S("handout.kind", "Names which of the workable forms the handout takes, so nobody later tries to make the slides do it.", ["M11"])
S("handout.what", "Where the audience will find the handout, so the call to action can point at it.", ["M11"])
S("policies", what="Decisions on the points where the research disagrees with itself, leaves the choice to the speaker, or is overridden for a stated reason.", why="Where the sources disagree, leave the choice open, or are overridden for a stated reason, the choice is written once here so the deck, the notes and the checks follow one policy instead of drifting.", checks=["M12"])
S("policies.opening", "The way in is chosen in the story; the two points left about the first minute are the joke, where the sources disagree, and self-introduction, which is advised only in academic settings.", ["M12", "R2", "S23"])
S("policies.opening.joke", "Winston advises against opening with a joke, the speaker's own notes allow it; a decision is recorded and the humour review checks the notes follow it.", ["R2"])
S("policies.opening.self_introduction", "Introducing yourself is advised only where the chair does not; when false the opening notes are checked for a self-introduction.", ["S23"])
S("policies.closing_words", "The last words are the most remembered; the research advises against ending on thank you and the conclusions slide is checked to show the statement.", ["M12", "S16"])
S("policies.background", "Light versus dark is the one point the advice literature contradicts itself on; the decision is recorded so the theme and the contrast checks work from one answer.", ["M12", "H10"])
S("policies.slide_numbers_visible", "Conway warns that slide numbers read as a countdown, so the audience never sees them and this stays false. The speaker practises with `pipeline.py build --rehearsal`, a separate file with faint numbers and visible arrows, so the aid can never ship by accident; M12 warns if this is flipped.", ["M12"])
S("policies.pointer", "The research argues against a laser because it turns the speaker to the screen; a digital pointer does not, and highlights on the slide do the pointing regardless. The choice is recorded rather than fixed because not every speaker has the same equipment.", ["M12", "S12"])
S("policies.questions", "The research says to announce a questions policy before any content; a venue where nobody interrupts and nobody announces that is a reason not to, and the reason belongs in the comment beside the value. Either way the decision is made before the talk, not on stage.", ["M7", "S23"])
S("policies.questions.when", "Whether the floor is open only at the end, at chapter ends, or any time decides how the beats are paced and what the notes may promise.", ["M7"])
S("policies.questions.announced", "When true the wording is required and the opening notes must contain it verbatim; when false nothing is said and the venue's convention carries it.", ["S23"])
S("policies.questions.wording", "Said near the start so people know when it is all right to ask; checked verbatim in the opening notes when announced.", ["S23"])
S("policies.questions.if_interrupted", "Answering or deferring an unscheduled question is decided in advance so it is not decided on stage.", ["M7"])

# ---------------------------------------------------------------- story
S("dramatic_question", "Popular science stories almost always pose an explicit question up front; it creates the appetite for the answer. The one slide allowed to show a question must show this one.", ["P3", "S24"])
S("insight", what="The answer to the dramatic question, stated outright rather than left to be inferred, in the words used at the reveal. It explains the idea statement in the meta file rather than repeating it: the statement is the handle, the insight is the mechanism.", why="Stating the insight outright, rather than leaving it implicit, is one of the story components that predicted popularity; it must differ from the statement so the reveal explains rather than repeats, and it must be said in the notes.", checks=["P5"])
S("fence", "An idea is understood partly by what it is not; drawing the boundary stops the audience filing it under the nearest familiar thing.", ["P8"])
S("fence.is_not", "Each is a sentence the speaker can say when the wrong filing is likely.", ["P8"])
S("fence.confused_with", "Names the neighbours the audience will compare it to, so the talk can say how it differs.", ["P8"])
S("context", "The audience needs to know why the work exists before hearing what it does: the problem, who else worked on it, and what is at stake.", ["P16"])
S("context.problem", "The problem in one or two sentences, so the talk can state it before solving it.", ["P16"])
S("context.others", what="Who else has worked on the problem, which of them the talk compares against, and why those and not the others.", why="Placing the work among others is part of the context Winston asks for; saying which of them the talk compares against, and why, keeps the comparison honest and inoffensive.", checks=["P16"])
S("context.why_matters", what="Why solving the problem is worth the audience's hour: the benefits the work claims, in the form the project itself states them.", why="The benefits the project itself claims, so the talk's argument and the documentation's are the same argument.", checks=["P16"])
S("memorability", what="The handles that make the work stick in memory: a symbol, a surprise, and the idea that will stand out afterwards.", why="Winston's handles for being remembered: a symbol, a surprise, a salient idea. The slogan is the meta file's idea statement.", checks=["P17"])
S("memorability.symbol", "Something that can be shown or said in a breath and stands for the work afterwards.", ["P17"])
S("memorability.surprise", "The turn the audience does not expect; surprise is also one of the high-arousal feelings that make talks travel.", ["P17"])
S("memorability.salient_idea", "The thing that will stick out afterwards, planned rather than left to chance.", ["P17"])
S("metaphors", "Optional. The research does not ask for a metaphor; it asks that the idea be built from what the room already knows, and here that ground is the audience's own experience with Doxygen, not a figure of speech. A metaphor is worth listing only when it is instantly recognised; one that has to be explained costs more than it gives. No experiment in the reading list measures the effect of metaphor in a talk.",
  ["P13", "R3"], what="Metaphors the talk may use to explain the new idea through something the audience already has.")
S("metaphors[].text", "The metaphor as it will be said, so the reviewer judges the real wording.", ["R3"])
S("metaphors[].familiar_because", "Forces the claim of familiarity to be written down where a reviewer can dispute it; a metaphor that needs this explained to the room fails.", ["P13", "R3"])
S("candidate_points", what="Everything that could be said about the topic, written down as prose before choosing: the curiosities that are not in the documentation, and a pointer to the documentation for the rest, page by page as nav.adoc lists it, rather than a copy of it.", why="Choosing from a long list is recognising what matters; choosing from memory is forgetting. The dump must outweigh what is kept, and the documentation already is the long list, so it is referenced here and counted, not repeated.", checks=["P11"])
S("chapters", "The chapters are not the structure of the talk, the acts are; they are how the tour is presented to the audience, as a map, and they are where the decision of which docs sections to tour is made: a nav section that is not a chapter is not toured. An early map helps people organise what follows, and pictures are remembered better than a list of names; the research's cap is five.", ["P10", "P19"], what="The sections of the tour the audience will be shown as a map, each with an image that stands for it, so the map is remembered as pictures rather than as a list of names. Tour beats name their chapter and list the docs pages under it.")
S("chapters[].label", "The name as it is said and shown on the map.", ["P10"])
S("chapters[].id", "Cross-reference used by the beats to place themselves in a chapter.", [], note="Bookkeeping.")
S("chapters[].mnemonic_image", "The map is remembered as pictures; each image must exist.", ["P10"])
S("chapters[].points", what="The few things this chapter says, at most five, each in one line; a summary of the chapter's pages, not a list of them.", why="At most five per chapter, so the map stays a map and not an outline.", checks=["P10"])
S("acts", what="The talk as a story, in order: each act sits on the story spine, on one side of the gap between what is and what could be, aims at a feeling, and asserts claims backed by evidence.", why="The acts are the structure of the story, the divisions the audience feels but never sees named; the claims inside them are the argument. The chapters below are only how the middle acts are shown, and what the tour visits is decided by the beats' docs lists, not here. The structural checks all read the acts.", checks=["P1", "P2", "P4", "P6", "P7", "P9", "P12", "P14", "P15"])
S("acts[].label", "For people reading the file.", [], note="Bookkeeping.")
S("acts[].id", "Cross-reference used by the beats and by references_act.", [], note="Bookkeeping.")
S("acts[].spine", "The story spine gives causal steps instead of a list of complaints; the steps must appear in order across the acts.", ["P1"])
S("acts[].side", what="Whether the act shows the present (status_quo), the future with the idea adopted (what_could_be), or sets the two against each other (both). An act on one side may still cross the gap inside itself, through beats flagged contrast.", why="The opening act must show both sides of the gap and the middle must alternate at least twice.", checks=["P2"])
S("acts[].arousal", "Arousal, not valence, drives sharing; at least one act must aim at a high-arousal feeling and none at sadness.", ["P6"])
S("acts[].cycles_idea", "At any moment a fifth of the room is fogged out; three passes at the idea make it likely everyone catches it.", ["P9"])
S("acts[].star_moment", "One moment the audience will remember, planned; exactly one act and one slide carry it.", ["P7", "S15"])
S("acts[].moment_of_change", "A turn is one of the story components that predicted popularity; at least one act must have one.", ["P4"])
S("acts[].opener_type", "Names the way in from the catalogue of openers that work, so the choice is deliberate.", ["P1"])
S("acts[].references_act", "The closing act must return to the opening, so the talk comes full circle.", ["P14"])
S("acts[].new_bliss", "The close paints the world with the idea adopted, described rather than summarised.", ["P15"])
S("acts[].claims", what="What the act asserts, each backed by evidence. Claims are the argument, not the itinerary: an act that tours thirty pages may assert five things, and the pages it visits are recorded in the beats' docs lists. Each claim must be carried by at least one beat, which names it in its claims list.", why="Every claim the act makes is listed with its evidence, so nothing in the talk rests on memory alone, and every claim is delivered by a beat, so nothing in the argument is silently dropped.", checks=["P12", "P28"])
S("acts[].claims[].id", "Cross-reference for slides and reviews.", [], note="Bookkeeping.")
S("acts[].claims[].text", "The claim as a sentence, so it can be checked as stated.", ["P12"])
S("acts[].claims[].evidence", "Where the support lives; facts marked verify must be verified before strict mode passes.", ["P12"])
S("acts[].claims[].evidence[].kind", "Says what sort of thing the reference is, so it can be checked the right way.", ["P12"])
S("acts[].claims[].evidence[].ref", "Locates the evidence: path, command, citation or description.", ["P12"])
S("acts[].claims[].evidence[].verify", "Marks facts only the speaker can confirm, so they are not mistaken for checked ones.", ["P12"])
S("acts[].claims[].evidence[].verified", "Flips when confirmed; the check warns until every verify fact is verified.", ["P12"])

# ---------------------------------------------------------------- beats
S("acts[].beats", "The timed spine of the talk: minutes that must sum to the talk time, one assertion each, transitions said aloud, pauses where segments run long, and more than one kind of medium.", ["P18", "P19", "P20", "P21", "P22", "P23", "P24", "P25", "P26"])
S("acts[].beats[].label", "For people reading the file.", [], note="Bookkeeping.")
S("acts[].beats[].id", "Cross-reference used by the slides.", [], note="Bookkeeping.")
S("acts[].beats[].chapter", "Places tour beats on the map, so the structure the audience was shown is the structure they get.", ["P19"])
S("acts[].beats[].target_minutes", "The minutes the beat is meant to take; the pace report compares it with the minutes the notes take to say. Targets must sum to the talk time, and a beat over six minutes needs a pause or a question. The meta file's talk time is the budget: beats with a target take theirs from it, and the beats without one share what is left in proportion to the minutes their notes take to say. A beat with a target and no notes yet is estimated at its target.", ["P18", "P22", "S27"])
S("acts[].beats[].purpose", what="Why the beat exists when it carries no claim: a transition, a demo, an opening, an ask. Beats that carry claims need no purpose; the claims are the purpose.", why="A beat is a stretch of speech with one point; if the point is not a claim of the act, it must still be sayable in a sentence, or the beat is talking without something to say.", checks=["P20"])
S("acts[].beats[].claims", what="The ids of the act's claims this beat carries: says, shows or proves. A beat may carry none (a title card, a pure demo), one, or several; the same claim may be carried by more than one beat.", why="Claims are the argument and beats are the delivery; without this list nothing ties them, and a claim can silently have no beat, as one did. P28 requires every claim of the act to be carried by at least one of its beats.", checks=["P28"])
S("acts[].beats[].transition_words", what="The words said to open the beat and mark the transition for a listener who drifted: an enumeration, a heading said aloud, a name for where we are. They appear in the spoken notes of the beat's first slide.", why="A listener who drifted needs a spoken marker to rejoin (Winston's verbal punctuation); the first slide's notes must contain these words.", checks=["P19"])
S("acts[].beats[].media", "A talk should use more than one kind of medium; at least two distinct kinds across the beats.", ["P24"])
S("acts[].beats[].pause", "Segmenting content with pauses improves learning; a long beat must have a pause or a question. The six-minute figure is a policy, not evidence.", ["P22"])
S("acts[].beats[].question_to_room", "Asking the room and waiting seven seconds is the one recommended audience interaction; at least one beat should, and it also counts as a pause.", ["P22", "P23"])
S("acts[].beats[].explains_system", "A system is understood after its parts; a beat that explains a whole must name the beat that introduced the parts.", ["P21"])
S("acts[].beats[].parts_beat", "The earlier beat whose parts this one assembles.", ["P21"])
S("acts[].beats[].contrast", "Duarte's alternation between what is and what could be does not have to happen between acts; a tour that shows the old way and then the new way in each chapter crosses the gap inside the act, and this flag is how P2 counts those crossings.", ["P2"])
S("acts[].beats[].establishes_vision", "The audience decides within about five minutes whether there is a vision; the beat that shows it must start within the first five.", ["P26"])
S("acts[].beats[].docs", what="The documentation pages this beat covers, as paths under docs/modules/ROOT/pages (or reference:index.adoc for the library reference); empty for a beat that covers none.", why="Ties the talk to the documentation so the two share one map; a record for the reader of the plan, not a checked property.", checks=[], note="Bookkeeping.")

# ---------------------------------------------------------------- slides
S("acts[].beats[].slides", "One entry per slide, in order, with everything the generator needs; the slide checks (L) read this list and the HTML checks (H) compare the output to it.", ["S1", "S4", "H1"])
S("acts[].beats[].slides[].id", "Becomes the section id in the HTML, which is how the built deck is matched back to the plan.", ["H1"])
S("acts[].beats[].slides[].kind", "A slide of kind same keeps the previous slide on screen and changes only the notes. Nothing in the research asks for a slide per point: the one experiment on the question (Moulton2017, 1,069 viewers) found slides no better than no visuals unless they carry meaningful motion, and the only recommended blank is during questions (Berk2012), so same is the evidence-backed default whenever a new point has no new picture worth showing. The renderer and the on-screen limits depend on the kind: word and line slides have word caps, picture slides must be explained in speech, the title card must be first and the conclusions last.", ["S1", "S2", "S6"])
S("acts[].beats[].slides[].on_screen", "Slides are for looking at, not reading: word caps by kind, no list-shaped text, no run of words that repeats the notes, the statement verbatim on the conclusions, a question only on the dramatic-question slide.", ["S2", "S9", "S16", "S20", "S24"])
S("acts[].beats[].slides[].assertion", "Sentence assertions, not topic phrases, are the one slide design choice with experimental support; deciding each slide's assertion also surfaces the concepts the audience lacks.", ["S3"])
S("acts[].beats[].slides[].notes", "Slides are looked at and speech is listened to, so everything the audience should hear is written here as speech: pictures are explained in these words, the promise and the cue are in the opening ones, banned phrases are absent, the take-home and the insight are said, and the speaking time per slide is estimated from them.", ["S6", "S7", "S9", "S22", "S23", "S26", "S27", "M3", "P5", "P19"], what="What the speaker says over this slide, written out in full as it will be spoken; the audience never reads it.")
S("acts[].beats[].slides[].silent", "Exempts a slide from the minimum-notes and speaking-time checks when nothing is said over it.", ["S7", "S27"])
S("acts[].beats[].slides[].duration_s", what="How many seconds the slide is on screen. Optional: when absent it is derived from the notes (words at the meta speaking rate plus a look-time for the kind), so write it only to override, where the time is not the speech: a live demo, a pause on a picture, the title card.", why="The notes are the source and the time is a consequence; a declared number is a promise the notes are then checked against (S27), and the durations sum to the talk time and drive the speaker view's pacing bar.", checks=["S4", "S27"])
S("acts[].beats[].slides[].motion", what="Deliberate motion into this slide: an auto-animate morph (shared elements move into their new place) or a named reveal.js transition. Absent on most slides, which then use the deck's default transition, read by the audience as simply 'next'.", why="Motion that shows how things relate is the one visual feature audiences measurably reward, and decorative motion is a seductive detail that hurts; so motion is declared where it carries meaning and defaulted everywhere else, with no justification needed for the default.", checks=["S5", "H8", "H9"])
S("acts[].beats[].slides[].motion.kind", "Auto-animate morphs shared elements between consecutive slides (the build-ups, the pipeline lighting up its hooks, two tables of the same shape); a named transition is for the one place a different reveal is the point.", ["S5", "H8", "H9"])
S("acts[].beats[].slides[].motion.group", "Names the run of consecutive slides that morph into one another; consecutive sections in a group must share an element.", ["H8"])
S("acts[].beats[].slides[].motion.shared", "The elements present on both slides, so the morph has something to move.", ["S5", "H8"])
S("acts[].beats[].slides[].motion.name", "Transitions are limited to none, fade, slide and one zoom for the reveal; anything showier is decoration.", ["H9"])
S("acts[].beats[].slides[].assets", "Every file the slide shows must exist, or the deck breaks on stage.", ["S8"])
S("acts[].beats[].slides[].image_role", "Pictures that represent or explain help recall; decorative pictures hurt it, so there is no value for them.", ["S10"])
S("acts[].beats[].slides[].labels", "Labels on the thing, never in a separate key, so the eye does not have to travel.", ["S11"])
S("acts[].beats[].slides[].chart_kind", "The graphic form should be chosen for the point, not the data; naming it forces the choice.", ["S11"])
S("acts[].beats[].slides[].chart_rationale", "Why this form fits the point, so a reviewer can disagree.", ["S11"])
S("acts[].beats[].slides[].emphasis", "The most salient element must be the most important one; picture slides must say what the eye lands on and why.", ["S25"])
S("acts[].beats[].slides[].emphasis.element", "The element made largest or brightest.", ["S25"])
S("acts[].beats[].slides[].emphasis.why", "Why that element is the point of the slide.", ["S25"])
S("acts[].beats[].slides[].code", what="For a code slide: one block of code, shown once, up to 40 lines and 60 columns; the highlights walk through it and the deck scrolls to each.", why="One block with moving emphasis is how a reader follows code: the lines keep their numbers and the eye keeps its place, where separate slides of the same code reset both.", checks=["S2", "S12", "S13"])
S("acts[].beats[].slides[].language", "The language name highlight.js uses to colour the code or the snippet source.", [])
S("acts[].beats[].slides[].highlights", what="For a single-block code slide: the positions lit one after another, each a fragment. Every position is an object with the lines it lights and the notes said while they are lit, or a bare range string lit under the slide's own notes; a single highlight may be the bare string itself, without a list.", why="Each highlight is one point about the code, at most eight lines, so the audience is never asked to read more than a paragraph of it at once.", checks=["S2", "S12"])
S("acts[].beats[].slides[].source", what="For a snippet slide: the C++ source exactly as the author wrote it, shown on the left; or name the file it lives in with `file` and let the build read it.", why="The point of a snippet slide is the pair: this is what you wrote, and that is what came out; the source is the half the room recognises as their own.", checks=["S2"])
S("acts[].beats[].slides[].doc_html", what="For a snippet slide, the documentation Mr.Docs generated from that source, shown on the right; for a rendered slide, the documentation generated from the code on the slide before, filling the slide. An HTML fragment under assets/, shown in the deck's own palette.", why="Showing the generated output rather than describing it is the evidence; rendering it in the deck's theme rather than the website's shows that the output adapts to wherever it is embedded.", checks=["S8"])
S("acts[].beats[].slides[].steps", "Code shown in paced discrete steps with the discussed lines highlighted is the form the animation research supports; at most eight lines per step, highlights on every step of a multi-step slide.", ["S12", "S13"])
S("acts[].beats[].slides[].file", what="A path in the repository the code is read from at build time, instead of a copy typed here.", why="An example that already exists as a file is shown from that file, so the plan carries a pointer and not a second copy that could drift from it.", checks=["S8"])
S("acts[].beats[].slides[].include", "How much of the file: tag=name, tags=a;!b, lines=a..b (after the tags), indent=0; the same attributes the docs use.", [], note="Bookkeeping.")
S("acts[].beats[].slides[].steps[].file", "The step's code read from a repository file instead of typed.", ["S8"])
S("acts[].beats[].slides[].steps[].include", "How much of the file, as for the slide.", [], note="Bookkeeping.")
S("acts[].beats[].slides[].steps[].blocks[].file", "The block's code read from a repository file instead of typed.", ["S8"])
S("acts[].beats[].slides[].steps[].blocks[].include", "How much of the file, as for the slide.", [], note="Bookkeeping.")
S("acts[].beats[].slides[].highlights[].lines", "The lines lit in this position: one line, a range, or several lit together.", ["S2", "S12"])
S("acts[].beats[].slides[].highlights[].notes", what="What is said while these lines are lit; the speaker view shows it with the fragment.", why="A highlight is a point being made, and the speaker needs the words for that point when it lights, not the whole slide's notes on every step.", checks=["S12", "S27"])
S("acts[].beats[].slides[].symbols", what="For a rendered slide: the symbols of the generated page to show, by name; the rest of the fragment is left out.", why="A generated page with several symbols is too small to read on one slide; showing one symbol per slide keeps each readable.", checks=[], note="Bookkeeping.")
S("acts[].beats[].slides[].title", what="For a code slide: the file the code comes from, shown as a tab above the frame.", why="One frame is one file. Naming the file in the frame's tab, not in a comment inside the code, keeps the code the code and lets two files on a slide be two frames.", checks=[], note="Bookkeeping.")
S("acts[].beats[].slides[].fit", what="For an image slide: contain (default) keeps the picture inside the slide's margins; bleed fills the slide to its edges; page is a tall picture (a document page) that reaches the top and bottom edges and leaves the canvas visible at the sides.", why="A captured page is shown as the page, edge to edge, so the room sees the site and not a picture of it.", checks=[], note="Bookkeeping.")
S("acts[].beats[].slides[].steps[].title", "One frame is one file; the tab names it.", [], note="Bookkeeping.")
S("acts[].beats[].slides[].steps[].blocks", what="Two files shown on one step, each in its own frame with its own title, stacked.", why="A command and the file it reads are two things; two frames say so, and one block with a comment naming the second file does not.", checks=["S2", "S12"])
S("acts[].beats[].slides[].steps[].blocks[].code", "The code of this block; at most eight lines and sixty columns.", ["S2"])
S("acts[].beats[].slides[].steps[].blocks[].title", "The file, or the shell as $, that this block is.", [], note="Bookkeeping.")
S("acts[].beats[].slides[].steps[].blocks[].language", "For syntax highlighting.", [], note="Bookkeeping.")
S("acts[].beats[].slides[].steps[].blocks[].highlight", "The lines being talked about, brightened.", ["S12"])
S("acts[].beats[].slides[].steps[].code", "The code shown in this step; at most eight lines and sixty columns.", ["S2", "S13"])
S("acts[].beats[].slides[].steps[].language", "For syntax highlighting.", [], note="Bookkeeping.")
S("acts[].beats[].slides[].steps[].highlight", "The lines being talked about, brightened; a step without highlights leaves the eye to wander.", ["S12"])
S("acts[].beats[].slides[].steps[].notes", "What is said over this step when it differs from the slide's notes; counted in the speaking-time estimate.", ["S27"])
S("acts[].beats[].slides[].live_url", "The real page rather than a screenshot; live pages are a medium the beats count, and rehearsals must include them.", ["P24", "D1"])
S("acts[].beats[].slides[].cta_url", "Must be the url in the meta file, so the slide and the ask agree.", ["S17"])
S("acts[].beats[].slides[].qr_asset", "The QR image for the address; it must exist.", ["S17"])
S("acts[].beats[].slides[].table", "For when the comparison itself is the picture; small, with the rows that matter highlighted so the eye goes there.", ["S2", "S25"])
S("acts[].beats[].slides[].table.header", "Column headers; at most four columns.", ["S2"])
S("acts[].beats[].slides[].table.rows", what="The rows, each a list of cells; the first cell is the row label. Cells that are yes, no or maybe render as a check, a cross and a question mark, so the column pattern is seen rather than read; an empty cell renders empty.", why="A table is the picture only when its pattern can be taken in at a glance; eighteen words of yes and no have to be read one by one.", checks=["S2"])
S("acts[].beats[].slides[].table.highlight_cols", what="The columns (counting from 1; the label column is 1) drawn in the accent colour, when one column is the point of the slide.", why="Salience should match importance; when the argument is about one column (the generated one, the tool you have), the eye should be sent there and nowhere else.", checks=["S2", "S25"])
S("acts[].beats[].slides[].table.steps", what="Optional: the same table shown one emphasis at a time, each step its own view in one morph group, with the words said over it. Rows, then columns, in the order they are talked about.", why="Talking through a table while all of it sits still is the slide the room stops watching; moving the emphasis with the speech is informative motion, the one kind audiences reward, and it splits the notes into the pieces they belong to. The steps' words count toward the slide's speaking time.", checks=["S5", "S7", "S27", "H8"])
S("acts[].beats[].slides[].table.steps[].rows", "Rows (from 1) emphasised in this step; the rest of the table dims.", ["S2"])
S("acts[].beats[].slides[].table.steps[].cols", "Columns (from 1, the label column is 1) emphasised in this step; the rest of the table dims.", ["S2"])
S("acts[].beats[].slides[].table.steps[].notes", what="What is said over this step, as it will be spoken. A stepped slide may carry all its speech in its steps and have no notes of its own.", why="Each step's words are its speaking time, so the pacing bar and the estimates follow the emphasis rather than the slide.", checks=["S7", "S27"])
S("acts[].beats[].slides[].table.highlight_rows", what="The rows (counting from 1) drawn in the accent colour, when some rows are the point and the rest are context. Optional: a table whose rows are all equal is shown plain.", why="Salience should match importance; a highlight where nothing is more important than the rest misleads the eye, so none is the right choice there.", checks=["S25"])
S("acts[].beats[].slides[].diagram", "Mermaid source for a diagram slide; diagrams are the recommended alternative to text and must carry labels and a chart kind.", ["S11"])
S("acts[].beats[].slides[].star_moment", "Exactly one slide is the moment the audience will remember, and it must match the story's.", ["S15"])
S("acts[].beats[].slides[].restates_idea", "The idea is cycled; at least three slides say it again.", ["S20"])
S("acts[].beats[].slides[].deliberately_complex", "One slide may be impossible to read, to make the point that the thing is complex; at most one.", ["S19"])
S("acts[].beats[].slides[].emotion_cue", "The speaker models the feeling the slide should produce; reviewed on the recording, not measured.", ["R4"])
S("acts[].beats[].slides[].why", "For people reading the file; not rendered.", [], note="Bookkeeping.")
S("acts[].beats[].slides[].dramatic_question", "A question as headline recalls worse than a sentence; one slide is exempt, the story's dramatic question, and its text must equal it.", ["S24"])

# ---------------------------------------------------------------- logs
S("rehearsals", "Rehearsing out loud, timed, more than once, to someone who does not know the work, and once recorded, is the most repeated advice in the research; the counts are checked.", ["D1", "D2"])
S("rehearsals[].date", "When.", [], note="Bookkeeping.")
S("rehearsals[].live", "Only a standing, out-loud, full-time run counts as a rehearsal.", ["D1"])
S("rehearsals[].timed_minutes", "Timed against the talk time; also gives the measured speaking rate that replaces speaking_wpm.", ["D1", "S27"])
S("rehearsals[].audience", "Who listened, so the naive-listener requirement can be checked.", ["D1"])
S("rehearsals[].audience[].who", "Name or role.", [], note="Bookkeeping.")
S("rehearsals[].audience[].knows_topic", "Listeners who know the work hallucinate content that is not there; at least one rehearsal needs one who does not.", ["D1"])
S("rehearsals[].recorded", "At least one rehearsal is recorded so delivery can be reviewed rather than remembered.", ["D2"])
S("rehearsals[].captions_check", "Automatic captions show where pace and articulation lose words.", ["D2"])
S("rehearsals[].opening_rehearsed", "The opening is prepared better than anything else, until automatic.", ["D1"])
S("rehearsals[].issues", "What to fix, so the next rehearsal has a target.", [], note="Informational.")
S("rehearsals[].delivery_review", "The delivery section of the research applied to the recording; the reviewer records it as R5.", ["D2", "R5"])
S("rehearsals[].delivery_review.energy", "Visible passion is what audiences report being inspired by.", ["R5"])
S("rehearsals[].delivery_review.eye_contact", "One person at a time, not the back wall.", ["R5"])
S("rehearsals[].delivery_review.pace", "The research's one direction on pace is slower, with a varied voice.", ["R5"])
S("rehearsals[].delivery_review.hands", "Free and doing something.", ["R5"])
S("rehearsals[].delivery_review.pauses", "Pauses instead of fillers.", ["R5"])
S("rehearsals[].delivery_review.read_slides", "Reading slides or notes aloud is the failure the whole design exists to avoid.", ["R5"])
S("checklist", "The preparation facts, nullable until known; each check warns until its fact is filled.", ["D3", "D4", "D5", "D6", "D7", "D8"])
S("checklist.equipment_tested", "Test with the actual laptop and deck before the day.", ["D3"])
S("checklist.pdf_backup", "The deck that runs when the deck cannot.", ["D3"])
S("checklist.still_backups", "Stills for every video or live page, for when the network fails.", ["D3"])
S("checklist.clicker_contingency", "Decided in advance, including what to say while recovering.", ["D3"])
S("checklist.room_visited", "Seeing the room removes the unknowns that feed anxiety.", ["D4"])
S("checklist.lights_request", "Lights up so the room can be seen and the audience stays awake; ask in advance.", ["D5"])
S("checklist.pre_talk_ritual", "Saying 'I am excited' out loud beats trying to calm down, in a randomised experiment; the ritual must include it.", ["D6"])
S("checklist.qa_conduct_acknowledged", "The question-and-answer conduct is read and accepted in advance: repeat every question, defer the persistent questioner, say 'I don't know' when true.", ["D7"])
S("checklist.feedback_plan", "Decided before the talk, or it does not happen.", ["D8"])
S("checklist.logistics", "The practical arrangements for the day.", [], note="Informational.")
S("checklist.logistics.directions", "How to get there.", [], note="Bookkeeping.")
S("checklist.logistics.contact", "Who to call.", [], note="Bookkeeping.")
S("checklist.logistics.dressed", "What to wear.", [], note="Bookkeeping.")
S("checklist.logistics.hydration", "The water plan.", [], note="Bookkeeping.")
S("checklist.logistics.return", "Leaving afterwards.", [], note="Bookkeeping.")
S("checklist.room", "Needed for the visit and the equipment test; warns until the schedule publishes it.", ["D4"])
S("reviews", "The verdicts of the read-throughs the pipeline cannot perform; strict mode requires a pass for each.", ["R1", "R2", "R3", "R4", "R5"])
S("reviews[].check", "Which review the verdict belongs to.", ["R1", "R2", "R3", "R4", "R5"])
S("reviews[].reviewer", "Who.", [], note="Bookkeeping.")
S("reviews[].date", "When.", [], note="Bookkeeping.")
S("reviews[].verdict", "The gate strict mode reads.", [], note="Read by every R check.")
S("reviews[].notes", "Especially on a fail.", [], note="Bookkeeping.")

def compose(cur, spec):
    cur = cur.split(" Why: ")[0]
    what = spec["what"] or cur
    what = " ".join(what.split())
    if not what.endswith("."): what += "."
    out = f"{what} Why: {spec['why']}"
    if not out.endswith("."): out += "."
    if spec["note"]: out += " " + spec["note"] + ("" if spec["note"].endswith(".") else ".")
    out += " " + evidence(spec["checks"]) + checks_str(spec["checks"])
    return out

missing, done = [], 0
FILE = {"meta": "1-meta", "story": "2-plan", "logs": "4-logs"}
for name in ("meta", "story", "logs"):
    p = pathlib.Path(f"schemas/{FILE[name]}.schema.yml")
    lines = p.read_text().splitlines(keepends=True)
    hdr = "".join(l for l in lines[:3] if l.startswith("#"))
    Sch = yaml.safe_load(p.read_text())
    def walk(node, path):
        global done
        for k, v in node.get("properties", {}).items():
            full = f"{path}{k}"
            if full in SPEC:
                v["description"] = compose(v.get("description", ""), SPEC[full]); done += 1
            else:
                missing.append(full)
            walk(v, f"{full}.")
            if isinstance(v.get("items"), dict): walk(v["items"], f"{full}[].")
        for key in ("allOf", "anyOf", "oneOf"):
            for sub in node.get(key, []):
                walk(sub, path)
                if "then" in sub: walk(sub["then"], path)
        if "then" in node: walk(node["then"], path)
    walk(Sch, "")
    p.write_text(hdr + yaml.dump(Sch, Dumper=BlockDumper, sort_keys=False, allow_unicode=True, width=110))
print("described", done, "missing", missing)
