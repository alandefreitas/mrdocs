# Content notes: what the talk is for and how it is built

[DESIGN-NOTES.md](DESIGN-NOTES.md) covers how the slides look. This file covers what they say: the objectives, the one idea, the structure, the rules for what goes on a slide versus into the speaker notes, and the reading those rules draw on. The slide-by-slide plan that follows from it is [script.yml](script.yml), and `index.html` is built from the script, not the other way round. [SPEAKING-NOTES.md](SPEAKING-NOTES.md) is the general craft (openings, closings, delivery, Q&A) and applies to every talk, not just this one.

## Objectives

When the talk ends, someone in the room or on YouTube should:

1. Be able to say the one idea back in a sentence: **Native C++. No workarounds.** Other tools read C++ as text and get close enough to do the job, at the price of workarounds in your headers; Mr.Docs reads it with the compiler's own front end, so nothing is lost and nothing has to be faked.
2. Know what it looks like in practice: real Boost reference pages on boost.org come out of it today.
3. Know how to try it tonight: one download, one YAML file, one command, on their own library.

Everything that does not serve one of those three is cut or moved into the notes.

The talk is 60 minutes on the CppCon schedule (Thursday, September 17, 2026, 2:00 PM). Plan for 45 minutes of talking, 10 of questions, 5 of slack for the live demos.

## Audience

In the room: library authors, tooling people, and a fair number of Boost people who have lived with Doxygen's macros themselves. On YouTube afterwards, and that is the bigger audience by far: anyone who searched "Doxygen alternative" or "C++ documentation generator". Both groups already know Doxygen, so Doxygen is the familiar thing the new idea is built on (Anderson's third rule, below). What is in it for them: less work, and documentation that cannot lie about the code.

## The one idea, and the shape of the talk

The spine is Nancy Duarte's sparkline: alternate between *what is* and *what could be*, with the gap between them doing the persuading, and end on the new normal. The opening act is a story, built on Kenn Adams's story spine so it has real causal steps rather than a list of complaints.

| Act | Minutes | Beat | What it is doing |
|---|---|---|---|
| 1. What is | 8 | A story: we wrote Boost libraries, documented them with Doxygen, Doxygen could not read our C++, so we started lying to it with `#ifdef` declarations, and the lies drifted. It ends on a question, and the answer is the tool. | The Mr.Docs banner is the *payoff* of the story, not slide two. |
| 2. What could be | 6 | One corpus from the real AST; why automate at all (design notes); the same doc comment serving the reader, the IDE and the AI assistant. | The contrast that makes the tour worth watching. |
| 3. The tour | 27 | The documentation, in its own order: Getting Started, Commands, Configuration, Generators, Extensions, Contribute. | Cycles back to the idea at every section seam. |
| 4. New normal | 4 | Full circle: the Boost header from the story, live on boost.org today, rendered by Mr.Docs. The one idea in its positive form. Download link. Thanks. | The last sentence is the idea. |

The tour follows `docs/modules/ROOT/nav.adoc` exactly. That is deliberate: someone who liked the talk opens the docs and finds the same map. The audience never sees those section names on screen; they are the first line of each slide's speaker notes ("Getting Started / Demo Gallery") so the speaker always knows where in the docs the current slide lives.

## What goes on a slide, and what goes in the notes

The style is the one from GECCO 2019 and from Apple keynotes: the presenter is the content, the screen is the illustration.

- **A slide is one image, one word, or one line.** Never a list. If the audience can read the slide they will read it, and stop listening.
- **The content lives in the speaker notes.** Every slide has notes, and the notes are written out as what gets said, not as bullet reminders. The audience hears it; they never read it.
- **Consecutive slides are connected by motion.** reveal.js `data-auto-animate` moves a shared element (a code block, a diagram node, a word) from one slide to the next, so the picture changes in step with the sentence. Fragments reveal one thing at a time inside a slide.
- **Code appears only when the code is the picture.** Then it is short (eight lines or fewer), stepped with `data-line-numbers`, and the notes carry the claim. A morphing code block is an animation, not a listing.
- **Live pages are backgrounds.** The demo gallery, boost.org and mrdocs.com are shown as interactive iframe backgrounds, because the real thing is more convincing than a screenshot of it.
- **One word can be a number.** "261" on an otherwise empty slide is the most persuasive slide in the deck (see Made to Stick, below).
- **Two hero moments only:** the reveal of the banner at the end of the story, and the one idea at the close. Both use the Mr.Docs display type; nothing else does.

## The evidence the story rests on

The story has to be true and checkable, because half the room has worked on these libraries.

- Boost carries documentation-only declarations behind preprocessor macros: `GENERATING_DOCUMENTATION` in Asio, `BOOST_BEAST_DOXYGEN` in Beast, `BOOST_URL_DOCS` in URL. In the local Boost checkout (develop, `boost-1.82.0.beta1-5116-g9ce204d4d7`) they appear in **180, 63 and 18 headers**: 261 headers with a second version of the truth. Re-run before the talk:

  ```sh
  cd ~/Documents/Code/C++/boost
  for m in GENERATING_DOCUMENTATION BOOST_BEAST_DOXYGEN BOOST_URL_DOCS; do
    printf '%s %s\n' "$(grep -rl --include='*.hpp' --include='*.ipp' "$m" libs | wc -l)" "$m"
  done
  ```

- A quotable example: `libs/beast/include/boost/beast/websocket/stream.hpp`, where the class is declared `class stream` and then `#if ! BOOST_BEAST_DOXYGEN : private stream_base #endif`. The compiler sees a base class; the documentation tool is told there isn't one.
- The "what could be" side: `docs/modules/ROOT/pages/migration-notes.adoc`, section "Idioms without workarounds", shows the same idioms (implementation-defined types, see-below, algorithm function objects, excluded symbols) expressed as configuration on the real declaration.
- Boost.URL's published reference on boost.org is generated by Mr.Docs. The deck already bundles that page (`assets/boost-url-live/url_view.html`).

Items marked `verify:` in `script.yml` are the facts only Alan can confirm from memory: the year the story starts, who was in the room, which page broke first.

## Craft principles, and where each one is applied

How these were gathered, so nobody mistakes it for more than it is: the works below were chosen from memory as the usual references for this style of talk, and each was checked only against a web-search result summary on 2026-09-10. None of the originals (papers, books, videos, pages) was read for this document. The description of each idea is therefore what I already knew plus what the summary confirmed; the specific figure quoted for Alley comes from a summary, not from the paper. The "applied" sentences are my own reasoning about this talk and do not depend on the sources. Anything here that will be said out loud or put on a slide should be checked against the original first.

**Duarte, the sparkline.** Persuasive talks move back and forth between what is and what could be, and end with the new bliss. Applied as the four-act structure; the tour keeps tacking (a Doxygen workaround, then the Mr.Docs option that replaces it).

**Adams, the story spine** ("Once upon a time... Every day... But one day... Because of that... Because of that... Until finally... Ever since then"). The one Pixar's writers were taught. Act 1 is written as one spine, one beat per slide, so the story has causes and not just grievances. Act 4 is the "ever since then".

**Anderson (TED), the four rules.** One idea only; give them a reason to care before you build; build from what they already know; make it worth sharing. Applied: one idea; the "261" slide is the reason to care; Doxygen is the familiar foundation; the call to action is "run it on your library", not "star our repo".

**Heath, Made to Stick (SUCCESs).** Simple, unexpected, concrete, credible, emotional, story. The 261-headers number is unexpected and concrete, and it is credible because the grep is on the slide's notes and anyone can run it. The story supplies the emotion: the reviewers who caught a documentation bug the compiler would have caught in code.

**Gallo, on Jobs's keynotes.** A headline you could tweet; the rule of three; a villain before the hero; one hero moment. Applied: the headline is the one idea; the three objectives above; Doxygen's macro layer is the villain, named plainly; the banner reveal is the hero moment. Apple's format (dark stage, one image per slide, the speaker carrying the content) is the format this deck copies.

**Reynolds, Presentation Zen.** Signal-to-noise; picture superiority (pictures are remembered better than words, especially with brief exposure); empty space as an active element. Applied: no bullets anywhere; one element per slide; the navy is allowed to be empty.

**Mayer, multimedia learning.** The **redundancy principle**: people learn better from graphics plus narration than from graphics plus narration plus the same words on screen. This is the scientific version of "don't put your speech on the slide", and it is the justification for putting the content in the notes. Also **coherence** (cut everything extraneous) and **temporal contiguity** (show the picture at the moment you say the words, which is what magic move does).

**Alley, assertion-evidence.** When a slide must carry a technical claim, state the claim as a full sentence headline and support it with a visual, not bullets. A summary of the study reports that audiences understood and remembered more with that layout (p < .01); the paper itself was not read for this document. Applied to the few slides that show code or the comparison table: the *notes* carry the assertion sentence, the slide carries the evidence.

**Winston, How to Speak (MIT).** Open with an empowerment promise (what they will be able to do at the end); cycle back to the idea several times; put verbal punctuation at the seams so people who drifted can get back on; end on the contribution, not on "questions?". Applied: the promise closes Act 1; every tour section re-states the idea in one sentence; the six documentation sections are the seams.

**Lessig and Takahashi.** Many slides, one word or one picture each, shown briefly. Applied to the tour, where the slide count is high and each slide is on screen for well under a minute.

**Conway, Instantly Better Presentations.** Animate code so the eye follows the change; one idea per reveal; rehearse three times in front of a live human. Applied to every code morph in the deck.

**Holman, speaking.io.** The practical developer-talk checklist: big type, high contrast, design for the room and the recording rather than for the slide download. This deck already designs for the recording first (DESIGN-NOTES).

## How the script and the slides stay in sync

`script.yml` is the source of truth for content. Each slide entry carries what is on screen, the exact on-screen text, what animates from the previous slide, the speaker notes, the docs page it covers, and *why* the slide exists. `index.html` gives every `<section>` an `id` equal to the script's slide id and an `<aside class="notes">` with the same notes. `check-script.py` compares the two: same ids, same order, notes present, no section that the script does not know about.

The order of work is therefore: edit the script, then edit the slides, then run the check. A slide that has no entry in the script is a slide without a reason.

## Reading list

The works referred to above. Not verified against the originals for this document; see the note at the top of the previous section.

- Nancy Duarte, [The secret structure of great talks](https://www.ted.com/talks/nancy_duarte_the_secret_structure_of_great_talks) (TED), and [the sparkline explained](https://www.duarte.com/blog/ultimate-guide-to-contrast/).
- Kenn Adams, the Story Spine; [its route into Pixar](https://www.aerogrammestudio.com/2013/03/22/the-story-spine-pixars-4th-rule-of-storytelling/) and [an interview with Adams](https://www.kunc.org/2026-06-23/meet-the-creator-of-the-story-spine-an-8-sentence-tool-to-create-and-analyze-stories).
- Chris Anderson, [TED's secret to great public speaking](https://www.ted.com/talks/chris_anderson_ted_s_secret_to_great_public_speaking); the book is *TED Talks: The Official TED Guide to Public Speaking*.
- Chip and Dan Heath, *Made to Stick*; [the SUCCESs model](https://heathbrothers.com/member-content/made-to-stick-model/).
- Carmine Gallo, *The Presentation Secrets of Steve Jobs*; [the rule of three in Apple keynotes](https://www.forbes.com/sites/carminegallo/2014/09/10/one-simple-rule-that-makes-apple-presentations-apple-esque/).
- Garr Reynolds, *Presentation Zen*; [his design tips](https://www.garrreynolds.com/design-tips).
- Richard Mayer, [Principles for reducing extraneous processing in multimedia learning](https://www.cambridge.org/core/books/abs/cambridge-handbook-of-multimedia-learning/principles-for-reducing-extraneous-processing-in-multimedia-learning-coherence-signaling-redundancy-spatial-contiguity-and-temporal-contiguity-principles/CD5B7AE1279A9AB81F8EEBB53DBEC86E) (Cambridge Handbook of Multimedia Learning).
- Michael Alley et al., [How the design of presentation slides affects audience comprehension: a case for the assertion-evidence approach](https://pure.psu.edu/en/publications/how-the-design-of-presentation-slides-affects-audience-comprehens/), and [Penn State's assertion-evidence resources](https://writing.engr.psu.edu/research.html).
- Patrick Winston, [How to Speak](https://ocw.mit.edu/courses/res-tll-005-how-to-speak-january-iap-2018/) (MIT OpenCourseWare).
- [Takahashi method](https://en.wikipedia.org/wiki/Takahashi_method) and [the Lessig method](https://ethos3.com/designstyles-and-approachespresenting-lessig-way/).
- Damian Conway, [Instantly Better Presentations](http://damian.conway.org/IBP.pdf) (notes) and [the YOW! 2014 talk](https://www.youtube.com/watch?v=W_i_DrWic88).
- Zach Holman, [speaking.io](https://speaking.io/) and [Slide design for developers](https://zachholman.com/posts/slide-design-for-developers/).
- reveal.js, [auto-animate](https://revealjs.com/auto-animate/) and [fragments](https://revealjs.com/fragments/), the two features the whole style depends on.
