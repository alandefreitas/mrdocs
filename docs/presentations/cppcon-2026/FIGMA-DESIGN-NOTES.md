# Mr.Docs new brand: design notes

Everything I pulled out of the Figma file **"Mr. Docs - External"** while restyling this presentation to match the upcoming website. Written down so that, if the website redesign isn't shipped by Aug 15, these colors and the basic styling can be applied to the landing page and the docs by hand.

Source: <https://www.figma.com/design/en13OqyifXZISc9xGrQSUo/Mr.-Docs---External> (read Aug 9, 2026).

## File structure

The Figma file has these pages:

- **Brand Guidelines** — the source of truth for colors, typography, logo, components. Laid out as a slide-style guide (sections numbered 1.x logo, 2.x colors, 3.x typography, plus miscellaneous + components rows).
- **Style tiles** — several exploration rounds (V1 → V3.1) mixing color, type and buttons in context. History, not canon.
- **Homepage iterations** — homepage layout explorations.
- **Documentation Template iterations** — docs-page layout explorations.
- **Final designs** — the canonical result. Four full mockups: **Homepage (Dark)**, **Documentation (Dark)**, **Homepage (Light)**, **Documentation (Light)**. The site ships **both a dark and a light theme**; dark is the hero look.

## Colors

The palette is essentially Tailwind's, anchored on slate-900 as "Midnight".

### Base (never substitute these)

| Name | Hex | RGB | Role |
| --- | --- | --- | --- |
| Cream | `#FDFAF5` | 253, 250, 245 | Primary background, **light** theme |
| White | `#FFFFFF` | 255, 255, 255 | Surfaces, light theme |
| Midnight | `#0F172A` | 15, 23, 42 | Background + foundational contrast, **dark** theme (Tailwind slate-900) |

### Accents — dark theme (use on Midnight)

| Name | Hex | RGB |
| --- | --- | --- |
| Blue (Dark) | `#3D8BFF` | 61, 139, 255 |
| Yellow (Dark) | `#FACC15` | 250, 204, 21 |
| Red (Dark) | `#FF4C4C` | 255, 76, 76 |

### Accents — light theme (use on Cream/White)

| Name | Hex | RGB |
| --- | --- | --- |
| Blue (Light) | `#93C5FD` | 147, 197, 253 |
| Yellow (Light) | `#F1D874` | 241, 216, 116 |
| Red/Pink (Light) | `#EC4899` | 236, 72, 153 |

Note the "Red - Light" is really a magenta/pink (Tailwind pink-500). Each dark variant is calibrated for dark surfaces and each light variant for light surfaces; the guide says **not to mix a variant across themes**.

Useful neutrals I inferred for dark surfaces (not explicit swatches, but consistent with the mockups): panels/cards around slate-800 `#1E293B`, borders around `rgba(148,163,184,0.18)`, body text near `#E2E8F0`/slate-100, muted text near slate-400 `#94A3B8`.

## Typography

Three families, each with one job:

- **Bangers** (Google Font) — the **display/headline** face. A bold, condensed comic-style font. Used **uppercase only**, for hero text, section titles, headings, and button labels. Carries the brand personality. Never body copy.
- **Special Elite** (Google Font) — the **body** face. A typewriter-inspired slab serif. Paragraphs, UI labels, captions, supporting text. Single weight (400). Warm, "developer tool" texture; works on both light and dark.
- **Menlo** — the **code** face. Monospace, code blocks and technical content only. (Menlo is an Apple system font, so on the web fall back: `'Menlo','Roboto Mono',ui-monospace,monospace`.)

Headlines routinely mix **yellow + blue** across lines/words for a pop-art effect, e.g.:

- "FULL-FIDELITY" (blue) / "C++ DOCS FROM CODE." (yellow)
- "KEEP THE CODE SIMPLE." (yellow) / "GET CLEAR DOCUMENTATION." (blue)
- "MORE CODE," (yellow) / "FEWER WORKAROUNDS" (blue)

They also carry a subtle dark drop-shadow / comic outline for the sticker look.

## Components

- **Buttons** — comic style. Rounded corners (~8px), Bangers uppercase label, a thin border and a hard offset bottom edge (a 3D/sticker shadow). Primary = blue `#3D8BFF` fill, white label. Secondary = Midnight/dark fill, light border, white label. A yellow variant appears as the nav CTA.
- **Cards** — dark elevated panels (~slate-800) on Midnight, subtle border, rounded (~12px), a small blue icon top-left, Bangers or bold title, muted body.
- **Code blocks** — Midnight background, a top title bar with the filename centered (e.g. `terminate.cpp`) flanked by thin divider lines, muted line numbers down the left. Syntax colors: **keywords/types red-pink** (`void`, `bool`, `unsigned long long`, `noexcept`), **function names blue** (`is_prime`, `terminate`), **comments/doc-comments slate-gray**, punctuation/params near-white. Attributes like `[[noreturn]]` render white/pink.
- **Background** — Midnight with a faint texture (dots/halftone) and a subtle city-skyline motif behind the hero, plus soft blue glows.

## Mascot / logo

The mascot is a "C++ detective/superhero" (bald character, red cape, blue armored suit, C++ chest emblem). It sits in the hero of the dark homepage. The existing `assets/MrDocsBanner.png` in this deck is the same character and stays on-brand.

## How this maps onto the presentation

The reveal.js deck is a dark surface, so it follows the **dark theme**:

- Canvas → Midnight gradient (`#0F172A` → `#0B1120`) replacing the old medium-blue `#124b83`.
- Headings → Bangers, uppercase, yellow with blue accents, subtle pop-shadow.
- Body → Special Elite.
- Links → blue `#3D8BFF` (hover light blue `#93C5FD`); `em` → yellow; `strong` → white.
- Code → Midnight surface, brand syntax colors, a small window bar using the three accent dots.
- Cards / tags / chips / tables → slate-800 panels with blue/yellow accents.
- Mermaid diagrams → recolored to blue/yellow/red/pink on Midnight.

Everything lives in `css/mrdocs-theme.css` plus a few inline hooks in `index.html` (cover-slide gradients, mermaid theme variables, font preloads).
