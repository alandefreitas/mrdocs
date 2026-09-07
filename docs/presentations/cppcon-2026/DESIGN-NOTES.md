# Design notes: built for the CppCon broadcast

The deck is designed for the recording first. A CppCon talk gets maybe a few dozen people in the room and tens of thousands on YouTube, and on YouTube the slide never appears on its own: Bash Films puts it in a fixed frame with CppCon's logo panel top left, the presenter camera bottom left, a lower third under the slide, and CppCon navy filling everything around it. A deck that ignores that frame looks like a rectangle taped onto someone else's design.

So this deck uses CppCon's palette instead of the Mr.Docs brand palette. The Mr.Docs brand still owns the two cover slides, where it should read as ours.

## The palette, and where the numbers came from

Sampled from two sources, not from cppcon.org (the website is a different, older look and does not match the broadcast).

1. `assets/cppcon-title-card-official-old-title.png`, the official 2026 speaker title card Digital Medium produced. This is the authority for 2026.
2. A frame pulled from a 2025 talk recording, for the chrome around the slide.

| Token | Value | What it is |
|---|---|---|
| `--cc-navy-deep` | `#1F2844` | Card edges. Also the chrome around the slide (`#1F2743` in the 2025 broadcast, the same colour through video compression). |
| `--cc-navy` | `#273457` | The card's dominant navy. |
| `--cc-navy-lift` | `#344472` | The bright centre of the card's gradient. |
| `--cc-navy-sink` | `#1B2440` | Not sampled. Sits one step below the canvas so code windows read as recessed. |
| `--cc-gold` | `#FFBB33` | Talk titles and speaker names on the card. |
| `--cc-orange` | `#FF8933` | The 2026 year mark in the top-right corner. |
| `--cc-grey` | `#AAACB5` | The `+` glyph next to the year mark. |
| `--cc-white` | `#F6F6F7` | Logos and the date lockup. |

## The backdrop is their gradient, not an imitation

The card's background is a soft glow, `--cc-navy-lift` in the middle falling off to `--cc-navy-deep` at the edges and roughly flat vertically through the middle two thirds. It is not an ellipse: fitting one leaves an rms error of nearly 16/255, which is plainly visible as a different-shaped glow. So the deck does not approximate it in CSS.

`assets/cppcon-backdrop.png` is the card's own gradient, extracted. Every logo, mark and line of text was masked out, the holes were filled from the card's mirror image (the gradient is symmetric about both axes, so a covered pixel borrows from its reflection), and the result was blurred to kill sampling noise. Across the parts of the card that are bare gradient it matches to a mean of 1.6/255. It is only 480x270 and 9 KB, because a smooth gradient upscales without artefacts.

`.reveal-viewport` stretches it to fill the screen, so on a 16:9 display it lands exactly where the card's gradient lands. Measured on rendered screenshots, slide 1 and slide 2 agree to within 1/255 down the left edge and across the top. Where they differ at all (up to 8/255 on the right and bottom) it is the card's own faint chevron shapes, which are decoration and are deliberately not carried into the deck.

The effect is that moving off the title card does not change the background at all: CppCon's artwork lifts off and the talk's content drops onto the same lit backdrop.

**This is fragile in one specific way:** any slide that sets its own background undoes it. `data-background-gradient` on the two cover slides was doing exactly that, painting a flat linear gradient over the glow. The only slides that may set a background are the title card itself and the demo slides that frame a live website.

## What changes between years, and what doesn't

Checked against the 2024 and 2025 recordings:

- **Constant:** the navy chrome and the gold that talk titles are set in. Identical in 2024, 2025 and 2026 within compression noise.
- **Changes every year:** the year mark accent. Green in 2024, purple in 2025, orange in 2026.

So gold is the primary accent everywhere, and orange is used only as a spot colour (code window dots, the "no" column in the comparison table, one border). If CppCon shifts the orange next year the deck still works.

The three year accents do all appear in one place: the syntax highlighting uses orange for keywords, purple for preprocessor lines and green for strings. That keeps even the code inside CppCon's own world instead of importing a generic editor theme.

## Type

The card's own typeface is a wide geometric sans with a flat-bar `G` and a double-storey `a`. It is not a Google font, but **Lexend** matches it closely at slide sizes, so headings and body both use Lexend. It was also drawn for reading fluency, which helps once YouTube's encoder has had its way with the text.

Code is **JetBrains Mono**.

**Bangers**, the Mr.Docs display face, is kept for the cover slides only (`.cover-title`). Mixing it into content slides fights the CppCon frame; on the covers, taking over is the point.

## Rules that come from the video, not from taste

- **The canvas is 16:9** (reveal is configured 1600x900). The broadcast slide box measures 1377x771, which is 16:9. A 16:10 deck sits in it with bars down both sides.
- **Body text is weight 400, not 300.** Light weights look elegant on a laptop and turn to mush after the encoder.
- **Nothing critical below 4.5:1 contrast.** Gold on the canvas is about 5:1, body text about 7:1.
- **Dimmed code lines stop at 0.42 opacity.** Far enough back to read as secondary, close enough to still be legible on a phone.
- **No pure black and no saturated red.** Neither appears in CppCon's palette anyway, and both are what compression handles worst.

## Checking a change

`local/cppcon-deck-design/` (gitignored) has the tooling: `shoot.sh N…` screenshots the given slides at 1600x900 and composites each into a real 2025 broadcast frame, which is the only honest way to judge whether a change reads on YouTube. `refs/` holds the sampled title card and the extracted broadcast frame.

## The Mr.Docs brand

Still documented in [FIGMA-DESIGN-NOTES.md](FIGMA-DESIGN-NOTES.md). It no longer drives the deck theme, but it is still the reference for the mascot, the banner art, and the cover slides.
