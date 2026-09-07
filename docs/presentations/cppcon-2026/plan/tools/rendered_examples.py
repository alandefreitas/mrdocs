"""Regenerates the documentation fragments under assets/rendered/ from the example sources on the slides.

Each example is a tiny project (one header, one mrdocs.yml) built here with the release binary; the <div class="symbol">
of the symbol each slide talks about is cut out of the single-page HTML and saved as the fragment a `rendered` slide
shows. Run it whenever an example's code changes on a slide, and paste the same code into the plan.

    python3 tools/rendered_examples.py            # scratch in plan/build/rendered-examples, output to assets/rendered
    python3 tools/rendered_examples.py SCRATCH OUT
"""
import pathlib, subprocess, re, sys, shutil
HERE = pathlib.Path(__file__).resolve().parent.parent
ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "build" / "rendered-examples"
OUT = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else HERE.parent / "assets" / "rendered"
OUT.mkdir(parents=True, exist_ok=True)
MRDOCS = "/Users/alandefreitas/Documents/Code/C++/mrdocs/install/release-macos/bin/mrdocs"
ADDONS = "/Users/alandefreitas/Documents/Code/C++/mrdocs/install/release-macos/share/mrdocs/addons"
EX = {
 "migration-scoped": (["logr::detail::**"], "scoped_context", {"logr/log.hpp": """\
namespace logr::detail {
struct scope_token { ~scope_token(); };
}
namespace logr {
/// Attach a key/value pair to every log line.
detail::scope_token
scoped_context(char const* key, char const* value);
}
"""}, "implementation-defined:\n  - 'logr::detail::**'\n"),
 "migration-clamp": ([], "clamp", {"numerics/clamp.hpp": """\
struct clamp_fn
{
    /// Constrain a value to `[lo, hi]`.
    template <class T>
    T operator()(T const& v, T const& lo,
                 T const& hi) const;
};
inline constexpr clamp_fn clamp = {};
"""}, "auto-function-objects: true\n"),
 "extraction-twice": ([], "twice", {"twice.hpp": """\
#include <type_traits>
/** Multiply by two, but only for integers. */
template <class T>
std::enable_if_t<std::is_integral_v<T>, T>
twice(T x);
"""}, "sfinae: true\n"),
 "buildup-sqrt": ([], "sqrt", {"sqrt.hpp": """\
/** Compute the square root of a
    non-negative number.

    @param x The value; must not be negative.
    @return The non-negative root of `x`.
    @throws std::domain_error if `x` is negative.
*/
template <class T>
T sqrt(T x);
"""}, ""),
 "metadata-relates": ([], "pixel", {"pixel.hpp": """\
/** A pixel record. */
struct pixel { int x, y; };

/** Euclidean distance between two pixels.

    @relates pixel
 */
double distance(pixel const& a, pixel const& b);
"""}, ""),
 "metadata-functionobject": ([], "scale", {"scale.hpp": """\
struct scale_fn {
    /** Returns `x` scaled by the factor. */
    int operator()(int x) const;
    int factor; // defeats auto-detection
};
/** @functionobject
    Scales by a fixed factor. */
constexpr scale_fn scale = {2};
"""}, ""),
 "inline-format": ([], "format_money", {"money.hpp": """\
/** Formats a quantity for *human* reading.

    Returns a **localized** string. Pass the raw
    value in `cents`; the ~~deprecated~~
    `format_dollars` spelling is gone.
 */
char const* format_money(long cents);
"""}, ""),
 "inline-ref": ([], "twice", {"twice.hpp": """\
/** Triples the input. */
int triple(int x);

/** Doubles the input.

    Closely related to @ref triple.
 */
int twice(int x);
"""}, ""),
 "inline-copydoc": ([], "base64", {"b64.hpp": """\
/** Encodes a byte sequence as base-64.

    @param data The bytes to encode.
    @param n    Number of bytes in `data`.
    @returns A null-terminated base-64 string.
 */
char* b64_encode(const char* data, unsigned n);
/** @copydoc b64_encode */ char* base64(...);
"""}, ""),
}
def fragment(html, name):
    for m in re.finditer(r'<div class="symbol">.*?<!-- /symbol -->', html, re.S):
        h = re.search(r'<h2 id="([^"]*)">(.*?)</h2>', m.group(0), re.S)
        if h and re.sub(r"<[^>]+>", "", h.group(2)).strip().split("::")[-1] == name:
            return m.group(0)
    return None
for ex, (_, sym, files, cfg) in EX.items():
    d = ROOT / ex; shutil.rmtree(d, ignore_errors=True); (d / "include").mkdir(parents=True, exist_ok=True)
    for f, src in files.items():
        (d / "include" / f).parent.mkdir(parents=True, exist_ok=True); (d / "include" / f).write_text(src)
    (d / "mrdocs.yml").write_text(f"source-root: .\ninput: [include]\ngenerator: html\nmultipage: false\nshow-namespaces: false\noutput: out\naddons: {ADDONS}\n" + cfg)
    r = subprocess.run([MRDOCS, "--config", str(d / "mrdocs.yml")], capture_output=True, text=True)
    html = (d / "out" / "reference.html").read_text() if (d / "out" / "reference.html").exists() else ""
    frag = fragment(html, sym)
    ids = re.findall(r'<h2 id="([^"]*)">', html)
    if not frag:
        print(f"FAIL {ex}: symbol {sym} not found; ids={ids}\n{r.stderr[-800:]}"); continue
    (OUT / f"{ex}.html").write_text(frag + "\n")
    print(f"ok {ex}: {len(frag)} chars; ids={ids}; warnings={r.stderr.count('warning')}")
