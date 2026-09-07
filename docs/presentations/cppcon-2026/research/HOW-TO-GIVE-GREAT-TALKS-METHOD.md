# How the synthesis was made

[HOW-TO-GIVE-GREAT-TALKS.md](HOW-TO-GIVE-GREAT-TALKS.md) is a literature synthesis on giving talks, built by reading sources one at a time and folding each into a single document organised by claim. This file records the instructions it was built under and the few things that were changed or added along the way. [HOW-TO-GIVE-GREAT-TALKS-LOG.md](HOW-TO-GIVE-GREAT-TALKS-LOG.md) is the per-source record; [HOW-TO-GIVE-GREAT-TALKS-SUMMARY.md](HOW-TO-GIVE-GREAT-TALKS-SUMMARY.md) is a one-page summary of the synthesis.

## The instructions

Given by Alan on 2026-09-10, after a first draft of "research" turned out to be search-engine snippets plus a reading list:

1. Start a document called "How to give great talks". It is about giving talks in general, not only about the CppCon talk.
2. Read every paper that can be accessed. Peer-reviewed papers first; where they are scarce, other sources are acceptable, and a source that is not a paper but is excellent may be used.
3. Give each source a short tag, normally author name and year.
4. For each source read: (a) consolidate its information into the document, putting the tag next to every piece of information taken from it; (b) list the works it references and the works that reference it; (c) filter those for relevance and add the relevant ones to the queue of sources that will go through step (a).
5. Consolidation is the critical step. The document must not simply grow: new information goes into the section it belongs to, never into a section about the paper. Each sentence is written so that how much evidence stands behind it is clear, with the tag of the source. When another source corroborates something already there, adjust the tags and the stated strength of evidence rather than adding a new sentence.
6. When a section grows too large, re-evaluate the whole structure and make the hierarchy consistent again according to the categories of claims.
7. Seed the document with the content of Alan's own "Giving talks" note (North task 778, note 110) and with the existing reading list.

## What was added or changed

- **An explicit evidence scale.** The instruction was that each sentence make its evidence clear. To do that consistently, every claim carries one of six marks: (meta), (exp), (obs), (expert), (own), and (abstract) for a source of which only the abstract could be read. "Expert consensus" with a percentage marks a recommendation counted in the systematic review of advice articles (Blome 2017). Effect sizes are quoted when the source gives them.
- **The per-source material lives in a separate log.** The instructions describe one document. The per-source entries (what was read and how, its references, who cites it, what was queued) are exactly the kind of per-paper text that instruction 5 says must not accumulate in the synthesis, so they went into the log, and the synthesis holds only tagged claims. The log also holds the queue.
- **Disagreements are recorded, not resolved.** Where sources conflict (opening with a joke, ending with "thank you", sentences versus phrases on slides, light versus dark backgrounds, animated visuals), both positions are stated with their marks, in one entry, so the reader sees the disagreement rather than one side of it.
- **A Myths section.** Popular beliefs that a source tested and found wanting, or repeats without evidence, are collected in the last section rather than scattered as negations.
- **Second-hand findings are marked as such.** When a source reports another study's result, the claim is tagged `[Source, citing Other]` and marked "second-hand", and the other study goes on the queue to be read at first hand.
- **Talks as sources.** Four practitioner talks (Winston, Duarte, Anderson, Conway, Phillips) were read as caption transcripts pulled with yt-dlp, since the TED pages render transcripts client-side. Two of the TED tracks are back-translations into English, so they are paraphrased and not quoted.
- **Access limits are recorded rather than worked around.** PNAS, the Physiology journals, PMC and ResearchGate block downloads; Semantic Scholar rate-limits after a few requests, so cited-by lists are incomplete and say so; Elsevier and APA titles are paywalled. Two papers were read as abstracts only and are marked (abstract) wherever used. Europe PMC's full-text API served open-access articles.
- **Two cautions at the top.** Most experiments are laboratory studies of students learning from narrated animations, a large share of the slide-design experiments come from one research group, and almost no format study before 2017 randomised presenters. These are stated once, up front, so individual claims do not have to repeat them.
- **Restructuring done so far.** Section 2 gained a "Choosing what to say" subsection and Section 3 was split into six subsections when they outgrew their headings; Section 6 was split into building, rehearsal, nerves, and the room.
- **One correction.** The 2023 redundancy review was first tagged after a paper cited inside it (`[Albers2023]`); the authors are Trypke, Stebner and Wirth and the tag was corrected throughout.
- **Where the work is kept.** Full texts and transcripts are under `local/research/how-to-give-great-talks/papers/` (gitignored). The synthesis and log are committed after each batch of sources.
