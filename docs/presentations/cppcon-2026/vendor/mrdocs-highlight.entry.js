// Browser entry for the deck: the docs' own highlight.js 9 instance with Mr.Docs's shared language definitions
// (docs/ui/src/js/vendor/mrdocs-highlight-languages.js), so doc comments in C++ blocks get the same colouring as
// on the documentation site. Bundled with browserify from docs/ui, where highlight.js 9 lives:
//
//   cd docs/ui && npx browserify ../presentations/cppcon-2026/vendor/mrdocs-highlight.entry.js \
//       -o ../presentations/cppcon-2026/vendor/mrdocs-highlight.js
//
// The deck's page script registers `cpp` from this instance on reveal's highlighter, then lets reveal's plugin do
// the line numbers and the highlight steps as before.
window.mrdocsHighlight = require('../../../ui/src/js/vendor/mrdocs-highlight-languages.js').create()
