# CppCon 2026 deck: open items after the review

Written 2026-09-12, after Alan finished reviewing every slide. The deck is in good shape to present. Everything below is what the checker still complains about, what each complaint means in plain words, and whether it is worth doing anything about before 2026-09-17. None of it is urgent.

## How to pick this work up again

The Claude Code session that built this deck is named `presentation` (session id `37c1737a-869b-4082-8fbb-7f33150ca68f`). To continue it with its full context, run this from the repository root and pick the session named `presentation` in the list:

```
cd ~/Documents/Code/C++/mrdocs
claude --resume
```

If the session is gone, this file plus `plan/PLAN.md` and `README.md` are enough to start over. The branch is `docs/cppcon-2026-presentation`; nothing has been committed since the last commit on it, so the first thing to do in a fresh session is `git status` and a commit.

Everyday commands, all run from `docs/presentations/cppcon-2026/plan/`:

```
./check.sh                          # rebuild build/index.html and build/rehearsal.html, print the pace table, run every check
python3 tools/pipeline.py pace      # the pace table alone
python3 tools/pipeline.py validate --strict   # the week of the talk: warnings become failures
python3 tools/pipeline.py build --artifacts   # also writes build/deck.pdf and build/contact-sheet.png
```

The deck to present is `plan/build/index.html`. The practice deck with slide numbers is `plan/build/rehearsal.html`. The hand-made `index.html` one level up is the old deck and is not regenerated.

## What check.sh reports today

Two failures. Both are the same thing: the first slides of the Generators and Extensions chapters have a one-word assertion ("Generators", "Extensions") and the schema wants a sentence of at least 15 characters. The assertion is never shown to the audience; it is the one-sentence claim each slide is supposed to make, and the research behind the check says slides designed around a sentence beat slides designed around a topic word. For a chapter title slide that rule adds nothing. Cheapest fix: write "The Generators chapter opens." and "The Extensions act opens." Or drop the minimum length for title and word slides in the schema.

These two failures stop every other check from running. The list below is what appears once they are fixed. It was produced by running the checks on a copy of the plan with those two assertions padded.

## Failures that would appear next

Grouped by how much they matter.

### Worth fixing because the audience would see it

- **S2, tall code blocks.** Six code slides are taller than the 21 lines the frame shows at full size, with no highlight steps to scroll them: `handlebars-reorder-symbol` (32 lines), `handlebars-reorder-input` (27), `data-driven-md-output` (54), `data-driven-tex-output` (60), `transforms-modify-lua` (37), `transforms-files-input` (37). The bottom of each is simply cut off on screen. Three ways out per slide: highlight steps of at most 20 lines each, one sentence per step, as the other long files have; an `include: lines=a..b` that shows only the part being talked about; or accepting the clipped block. The two data-driven outputs now have a GitHub and a PDF slide right after them, so `lines=1..20` is probably enough there.
- **S2, noop-agent-loop.** One highlight step, lines 42 to 62, is 21 lines, one over the cap. Trim to 42 to 61.
- **H4, uppercase.** The Copy button on the handlebars override page is styled in uppercase and the check forbids uppercase outside cover slides. Remove `text-transform: uppercase` from `.copy-button` in `css/mrdocs-theme.css`. Two minutes.

### Bookkeeping the checker wants and the audience never sees

- **P19, transition words on four beats.** Each beat records the words that open it, so a listener who drifted can rejoin, and the check requires those words to appear in the notes of the beat's first slide. The notes of `extensions-title`, `ext-generators-title`, `library-title` and `full-circle` were rewritten during the review and no longer contain the recorded words. Alan's view, which is fair: if any sentence can be declared the transition words, the check has no effect; if it has to be a specific phrase, it is busywork. The cheapest way to make it pass is to set each beat's `transition_words` to the sentence that is now said first, which is what we did for the broke beat. Or remove the check.
- **M3, the take-home action.** The meta file lists three take-home sentences that must be said somewhere in the notes. The action one, "One download, one YAML file, one command, and it runs on your own library tonight", is no longer said anywhere after the call-to-action notes were rewritten. Either say it there or change the meta sentence to what is actually said.
- **S15, the star moment.** The extensions act is flagged as the act with the star moment, but no slide inside it carries `star_moment: true` any more (the describe sidecar slide used to). The check comes from Duarte's advice that a talk has one deliberately memorable moment. Alan's view: tagging a slide creates nothing; the moment either exists or it does not. If the tag is kept, put it on the slide that already is the moment. Otherwise remove the flag from the act and the check goes quiet.
- **S20, restating the idea.** Two slides are flagged `restates_idea: true` and the check wants three, from the advice to repeat the one idea in at least three acts. The idea is on the negative line, the positive line and the-trade in practice; flag one more or lower the count.
- **P12, unverified facts.** Two claims in the plan carry `verify: true, verified: false`: the Doxygen rendering of the Boost.URL header (`story/boost-doxygen`) and the buffers_cat drift in Beast (`story/drift`). Both were checked while building the slides that show them. Flip `verified: true` if satisfied.

## Warnings

All of these are logistics recorded in `plan/logs/logs.yml`, and they stay warnings until `--strict` is used.

- **D1** wants three live, timed rehearsals within the slot, at least one to someone who does not know the work. Each is an entry under `rehearsals:` with `date`, `live: true`, `timed_minutes`, and `listener`. The pace table uses the timed minutes to report the real speaking rate, which is the one number worth logging: the 130 words per minute in the meta file came from three short measurements, not a full run.
- **D2** wants one rehearsal recorded and reviewed with a captions check.
- **D3** wants `equipment_tested`, `pdf_backup` and `still_backups` filled in. The PDF comes from `build --artifacts`.
- **D4** room visited; **D5** lights-up request recorded; **D7** `qa_conduct_acknowledged: true`.
- **R1 to R5** are five review passes with a `reviews:` entry each: narrative read-through, humour placement, familiar metaphors, one more from the research file, and a delivery review of the recording.

## Pace

The talk is sized at 50 minutes (60-minute slot, 10 for questions, no buffer). At 130 words per minute plus look time the notes estimate at 48.1 minutes. The timings the speaker view paces against are not these raw estimates: when the deck is built, every section's estimate is multiplied by one factor so that the sections sum to exactly the talk time (a rule of three), so the pacing bar always ends with the talk. The pace table keeps showing the raw estimates, which is what tells you where the content is long or short. Two beats are away from their targets: configuration is 3.4 minutes under (7.5 target, 4.2 estimated) and ext-hooks 2.3 over (4.0 target, 6.3 estimated). The content is final, so the targets should follow it: change `target_minutes` on those beats so the pacing bar in the speaker view is honest. Beats without a target share what the other targets leave, in proportion to their notes.

## What went wrong, in Alan's words and mine

- The checks were written before the content, from a research file that neither of us had time to absorb, so several of them are rules about tags rather than about the talk (star moment, transition words, restates count). They force the plan to describe itself instead of improving the slides. Next time: read the research first, keep only the checks that would change a slide, and write the plan from the talk outward.
- The tour slides were generated from the documentation pages in bulk and then reviewed one by one. The bulk step produced the right material but the wrong voice (every note started "This is"), and every rule of thumb the generator applied (screenfuls of code, placeholder notes, auto-shrunk code) had to be undone by hand. Generating the material was right; generating the sentences was not.
- Time was modelled from word counts with a speaking rate measured on two slides. It was useful for finding the beats that were far too long, and useless for anything finer than that. One timed run replaces all of it.
- Alan's own note: start earlier next time, so there is time to absorb the research and organise the plan before the slides exist.
