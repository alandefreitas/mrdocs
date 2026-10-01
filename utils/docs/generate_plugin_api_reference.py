#!/usr/bin/env python3
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#
"""Generate and check the plugin C API reference table.

`include/mrdocs/plugin.h` is the source of truth for the C interface that
plugins use. This script writes the AsciiDoc partial that the plugin API
reference page includes: one row per `mrdocs_*` function with the ABI version
it first appeared in and the first sentence of its comment.

The compiler checks the layout and the signatures: `tests/plugin-api/abi/
AbiBaseline.c` records them as static assertions and is built for every ABI
version. This script checks what a compiler cannot:

  * every function, type, constant, descriptor field and enumerator of the
    header has a `@since ABI n` line (a field or an enumerator without one
    takes the version of its type, a macro that only selects linkage or
    visibility is exempt, the calling convention macro is not), and no `n`
    is above `MRDOCS_PLUGIN_ABI_VERSION`;
  * every function, and every field or type that is a pointer to a function,
    names `MRDOCS_PLUGIN_CALL`, so that a plugin built with /Gv or /Gz on
    Windows still links and is called correctly;
  * every one of those names appears in AbiBaseline.c;
  * every function appears in tests/plugin-api/LinkProbe.cpp, the module that
    calls it and so checks that the tool exports it on Windows;
  * the partial on disk matches the header, the reference page includes it,
    and the Plugins page links the reference.

The header is read line by line, so it has to keep one declaration per line
for fields and enumerators, and `/* */` comments.

Usage:
    python utils/docs/generate_plugin_api_reference.py --update
    python utils/docs/generate_plugin_api_reference.py --check
    python utils/docs/generate_plugin_api_reference.py --self-test
"""
import argparse
import os
import re
import sys


REPO_ROOT = os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))))

HEADER_PATH = os.path.join(REPO_ROOT, "include/mrdocs/plugin.h")
PROBE_PATH = os.path.join(REPO_ROOT, "tests/plugin-api/abi/AbiBaseline.c")
LINK_PROBE_PATH = os.path.join(REPO_ROOT, "tests/plugin-api/LinkProbe.cpp")
PARTIAL_PATH = os.path.join(
    REPO_ROOT, "docs/modules/ROOT/partials/plugin-api-reference.adoc")
PAGE_PATH = os.path.join(
    REPO_ROOT, "docs/modules/ROOT/pages/extensions/plugin-api-reference.adoc")
PLUGINS_PAGE_PATH = os.path.join(
    REPO_ROOT, "docs/modules/ROOT/pages/extensions/plugins.adoc")

PARTIAL_INCLUDE = "include::partial$plugin-api-reference.adoc"
PAGE_XREF = "xref:extensions/plugin-api-reference.adoc"

# Macros that only select linkage and visibility are not part of the ABI.
# The calling convention is, so MRDOCS_PLUGIN_CALL is not exempt.
EXEMPT_MACROS = {
    "MRDOCS_PLUGIN_API",
    "MRDOCS_PLUGIN_ENUM_BASE",
    "MRDOCS_PLUGIN_EXPORT",
    "MRDOCS_PLUGIN_EXTERN_C",
    "MRDOCS_PLUGIN_H",
}

HEADER = (
    "// Auto-generated from include/mrdocs/plugin.h.\n"
    "// Run `python utils/docs/generate_plugin_api_reference.py --update` to "
    "regenerate.\n"
    "// Do not edit by hand; CI checks this file matches the header.\n"
    "\n"
)

SINCE_RE = re.compile(r"@since\s+ABI\s+(\d+)")
VERSION_RE = re.compile(
    r"^(\s*#\s*define\s+MRDOCS_PLUGIN_ABI_VERSION\s+)(\d+)(u?)\s*$", re.M)


class Item:
    """A declaration of the header: a function, a type, a constant, or a
    field or enumerator of a type (whose `parent` is the type)."""

    def __init__(self, kind, name, line, comment, parent=None):
        self.kind = kind
        self.name = name
        self.line = line
        self.comment = comment or ""
        self.parent = parent
        self.declaration = ""
        m = SINCE_RE.search(self.comment)
        self.own_since = int(m.group(1)) if m else None
        self.is_struct = False

    @property
    def since(self):
        if self.own_since is not None:
            return self.own_since
        return self.parent.since if self.parent is not None else None


def comment_body(raw):
    """Strip the comment markers and the common indentation."""
    text = raw.strip()
    text = text[2:-2] if text.startswith("/*") and text.endswith("*/") else text
    out = []
    for ln in text.split("\n"):
        s = ln.strip()
        out.append(s[1:].strip() if s.startswith("*") else s)
    return "\n".join(out).strip()


def summary_of(body):
    """The first sentence of the first paragraph, as AsciiDoc text."""
    para = re.split(r"\n\s*\n", body, maxsplit=1)[0]
    para = " ".join(para.split())
    m = re.search(r"[.:]( |$)", para)
    sentence = para[:m.start() + 1] if m else para
    if sentence.endswith(":"):
        sentence = sentence[:-1] + "."
    sentence = re.sub(r"@ref\s+(\w+)", r"`\1`", sentence)
    sentence = sentence.replace("|", "\\|")
    return sentence


def declaration(lines, i):
    """The text of the declaration that starts at line i, without comments,
    up to its `;`, and the index of the line after it."""
    text = ""
    while i < len(lines):
        text += " " + lines[i]
        i += 1
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        if "/*" not in text and text.rstrip().endswith(";"):
            break
    return " ".join(text.split()), i


def skip_comment_tail(lines, i):
    """The index after line i, or after the line that closes a comment that
    line leaves open."""
    tail = re.sub(r"/\*.*?\*/", " ", lines[i])
    while "/*" in tail and i + 1 < len(lines):
        i += 1
        tail = lines[i]
        if "*/" in tail:
            break
    return i + 1


def parse_header(text):
    """Read the declarations of the header. Returns the items and the lines
    that were not understood."""
    lines = text.split("\n")
    items, unknown = [], []
    comment = None
    body = None
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        no = i + 1
        if s.startswith("/*"):
            j = i
            while "*/" not in lines[j]:
                j += 1
            comment = comment_body("\n".join(lines[i:j + 1]))
            i = j + 1
            continue
        if not s:
            i += 1
            continue
        if s.startswith("#"):
            m = re.match(r"#\s*define\s+(\w+)", s)
            while lines[i].rstrip().endswith("\\"):
                i += 1
            i = skip_comment_tail(lines, i)
            if m:
                # A macro defined once per platform is one constant, and the
                # comment above the first definition is its comment.
                if m.group(1) not in EXEMPT_MACROS and not any(
                        it.kind == "constant" and it.name == m.group(1)
                        for it in items):
                    items.append(Item("constant", m.group(1), no, comment))
                comment = None
            continue
        if body is not None:
            if s.startswith("}"):
                m = re.match(r"\}\s*(\w+)\s*;", s)
                if not m or m.group(1) != body.name:
                    unknown.append((no, s))
                body = None
                comment = None
                i += 1
                continue
            if body.is_struct:
                decl, i = declaration(lines, i)
                m = (re.search(r"\(\s*(?:\w+\s+)?\*\s*(\w+)\s*\)", decl)
                     or re.search(r"(\w+)\s*(?:\[[^\]]*\])?\s*;$", decl))
                kind = "field"
            else:
                m = re.match(r"(\w+)", s)
                kind = "enumerator"
                i += 1
            if m:
                items.append(Item(kind, m.group(1), no, comment, body))
                if body.is_struct:
                    items[-1].declaration = decl
            else:
                unknown.append((no, s))
            comment = None
            continue
        if s.startswith("MRDOCS_PLUGIN_API"):
            decl, i = declaration(lines, i)
            m = re.search(r"\b(mrdocs_\w+)\s*\(", decl)
            if m:
                items.append(Item("function", m.group(1), no, comment))
                items[-1].declaration = decl
            else:
                unknown.append((no, s))
            comment = None
            continue
        if s.startswith("typedef"):
            m = re.match(r"typedef\s+(struct|enum)\s+(\w+)(?:\s+MRDOCS_PLUGIN_ENUM_BASE)?$", s)
            if m and i + 1 < len(lines) and lines[i + 1].strip() == "{":
                body = Item("type", m.group(2), no, comment)
                body.is_struct = m.group(1) == "struct"
                items.append(body)
                i += 2
            else:
                decl, i = declaration(lines, i)
                m = (re.search(r"\(\s*(?:\w+\s+)?\*\s*(\w+)\s*\)", decl)
                     or re.search(r"(\w+)\s*;$", decl))
                if m:
                    items.append(Item("type", m.group(1), no, comment))
                    items[-1].declaration = decl
                else:
                    unknown.append((no, s))
            comment = None
            continue
        if s.startswith("extern") or s.startswith("}"):
            i += 1
            continue
        unknown.append((no, s))
        i += 1
    return items, unknown


def probe_covers(item, probe):
    """Whether the probe, with its comments removed, mentions the item."""
    name = re.escape(item.name)
    if item.kind == "field":
        parent = re.escape(item.parent.name)
        return all(
            re.search(rf"{macro}\(\s*{parent}\s*,\s*{name}\s*,", probe)
            for macro in ("ABI_OFFSET", "ABI_FIELD"))
    elif item.kind == "type" and item.is_struct:
        pattern = rf"ABI_SIZE\(\s*{name}\s*,"
    else:
        pattern = rf"\b{name}\b"
    return re.search(pattern, probe) is not None


FUNCTION_CALL_RE = re.compile(r"\bMRDOCS_PLUGIN_CALL\s+mrdocs_\w+\s*\(")
CALLBACK_RE = re.compile(r"\(\s*(?:\w+\s+)?\*\s*\w+\s*\)\s*\(")
CALLBACK_CALL_RE = re.compile(r"\(\s*MRDOCS_PLUGIN_CALL\s*\*\s*\w+\s*\)\s*\(")


def missing_call_macro(item):
    """Whether a function, or a field or type that is a pointer to a
    function, is declared without MRDOCS_PLUGIN_CALL. A plugin built with
    /Gv or /Gz on Windows only links and calls correctly when every function
    and every callback of the header names its calling convention."""
    if item.kind == "function":
        return not FUNCTION_CALL_RE.search(item.declaration)
    if item.kind in ("field", "type") and CALLBACK_RE.search(item.declaration):
        return not CALLBACK_CALL_RE.search(item.declaration)
    return False


def validate(text, probe, link_probe):
    """Check the header against the rules. Returns the functions and the
    errors. `probe` is AbiBaseline.c, which has to mention every
    declaration, and `link_probe` is LinkProbe.cpp, the module that has to
    call every function (the only check that the tool exports it on Windows)."""
    errors = []
    m = VERSION_RE.search(text)
    if not m:
        return [], ["MRDOCS_PLUGIN_ABI_VERSION is not defined as a number"]
    version = int(m.group(2))
    items, unknown = parse_header(text)
    for no, s in unknown:
        errors.append(f"line {no}: not a declaration the check understands: {s}")
    probe = re.sub(r"/\*.*?\*/", "", probe, flags=re.S)
    link_probe = re.sub(r"/\*.*?\*/", "", link_probe, flags=re.S)
    link_probe = re.sub(r"//[^\n]*", "", link_probe)
    seen = {}
    for it in items:
        where = f"line {it.line}: {it.kind} {it.name}"
        key = (it.kind, it.parent.name if it.parent else "", it.name)
        if key in seen:
            errors.append(f"{where} is declared twice")
        seen[key] = it
        if it.since is None:
            errors.append(
                f"{where} has no `@since ABI n` line in its doc comment")
            continue
        if missing_call_macro(it):
            errors.append(
                f"{where} is declared without MRDOCS_PLUGIN_CALL, the "
                "calling convention every function and callback names")
        if it.since < 1 or it.since > version:
            errors.append(
                f"{where} says @since ABI {it.since}, but "
                f"MRDOCS_PLUGIN_ABI_VERSION is {version}")
        if not probe_covers(it, probe):
            errors.append(
                f"{where} is missing from tests/plugin-api/abi/AbiBaseline.c")
        if it.kind == "function" and not re.search(
                rf"\b{re.escape(it.name)}\b", link_probe):
            errors.append(
                f"{where} is missing from tests/plugin-api/LinkProbe.cpp")
    functions = [it for it in items if it.kind == "function"]
    return functions, errors


def render(functions, version):
    """Render the partial."""
    out = [HEADER]
    out.append(
        f"The header describes ABI {version}. "
        "Each row names the ABI version a function first appeared in: "
        "a plugin built for ABI _n_ can call it on every Mr.Docs that "
        "provides ABI _n_ or later.\n\n")
    out.append('[cols="3,1,6",options="header"]\n|===\n')
    out.append("|Function |Since |Summary\n\n")
    for fn in sorted(functions, key=lambda f: (f.since or 0, f.line)):
        out.append(
            f"|`{fn.name}` |ABI {fn.since} |{summary_of(fn.comment)}\n")
    out.append("|===\n")
    return "".join(out)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def check_docs(expected):
    errors = []
    if not os.path.exists(PARTIAL_PATH):
        errors.append(
            f"{os.path.relpath(PARTIAL_PATH, REPO_ROOT)} does not exist; run "
            f"with --update")
    elif read(PARTIAL_PATH) != expected:
        errors.append(
            f"{os.path.relpath(PARTIAL_PATH, REPO_ROOT)} is out of date with "
            f"include/mrdocs/plugin.h; run "
            f"`python utils/docs/generate_plugin_api_reference.py --update`")
    if not os.path.exists(PAGE_PATH) or PARTIAL_INCLUDE not in read(PAGE_PATH):
        errors.append(
            f"{os.path.relpath(PAGE_PATH, REPO_ROOT)} must include the "
            f"generated partial ({PARTIAL_INCLUDE})")
    if PAGE_XREF not in read(PLUGINS_PAGE_PATH):
        errors.append(
            f"{os.path.relpath(PLUGINS_PAGE_PATH, REPO_ROOT)} must link the "
            f"reference ({PAGE_XREF})")
    return errors


def with_version(text, version):
    return VERSION_RE.sub(
        lambda m: f"{m.group(1)}{version}{m.group(3)}", text, count=1)


def add_function(text, name, since):
    """The header with a documented function appended before the end of the
    `extern "C"` block."""
    decl = (f"/* Added by the self-test.\n   @since ABI {since}\n*/\n"
            f"MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL\n"
            f"{name}(mrdocs_env* env);\n\n")
    at = text.rindex("#ifdef __cplusplus")
    return text[:at] + decl + text[at:]


def add_field(text, name, since):
    """The header with a field appended to the first structure."""
    items, _ = parse_header(text)
    first = next(it for it in items if it.kind == "type" and it.is_struct)
    lines = text.split("\n")
    close = next(i for i in range(first.line, len(lines))
                 if lines[i].startswith("}"))
    field = [f"    /* Added by the self-test. @since ABI {since} */",
             f"    int {name};"]
    return "\n".join(lines[:close] + field + lines[close:]), first


def add_enumerator(text, name, since):
    """The header with an enumerator appended to the first enumeration."""
    items, _ = parse_header(text)
    first = next(it for it in items if it.kind == "type" and not it.is_struct
                 and any(m.parent is it for m in items))
    lines = text.split("\n")
    close = next(i for i in range(first.line, len(lines))
                 if lines[i].startswith("}"))
    enumerator = [f"    /* Added by the self-test. @since ABI {since} */",
                  f"    {name} = 1000"]
    lines[close - 1] = lines[close - 1].rstrip(",") + ","
    return "\n".join(lines[:close] + enumerator + lines[close:])


def self_test():
    """Check that the checks reject the mistakes they exist to catch. The
    cases are derived from the real header and its current version."""
    header, probe = read(HEADER_PATH), read(PROBE_PATH)
    link = read(LINK_PROBE_PATH)
    version = int(VERSION_RE.search(header).group(2))
    items, _ = parse_header(header)
    failures = []

    def expect(label, errors, needle=None):
        if needle is None:
            if errors:
                failures.append(f"{label}: unexpected errors: {errors}")
        elif not any(needle in e for e in errors):
            failures.append(f"{label}: no error mentions {needle!r}: {errors}")

    def run(h, p=probe, l=link):
        return validate(h, p, l)[1]

    expect("the real header", run(header))

    # A function of the current version, with and without the probe.
    new_fn = "mrdocs_selftest_function"
    linked = link + f"\n{new_fn}(env);\n"
    grown = with_version(add_function(header, new_fn, version), version)
    expect("a function missing from the probe", run(grown), new_fn)
    expect("a function in the probe",
           run(grown, probe + f"\nABI_FN({new_fn}, void);\n", linked))
    expect("a function missing from the link probe",
           run(grown, probe + f"\nABI_FN({new_fn}, void);\n"),
           "LinkProbe.cpp")
    expect("a function only named in a comment of the link probe",
           run(grown, probe + f"\nABI_FN({new_fn}, void);\n",
               link + f"\n// {new_fn}(env);\n/* {new_fn} */\n"),
           "LinkProbe.cpp")

    # A function of the next version needs the version raised.
    ahead = add_function(header, new_fn, version + 1)
    expect("a function above the version", run(ahead), f"ABI {version + 1}")
    expect("a function of the raised version",
           run(with_version(ahead, version + 1),
               probe + f"\nABI_FN({new_fn}, void);\n", linked))

    # Comments in the places a contributor writes them: a `;` in a parameter
    # comment, and a comment that spans two lines after a directive.
    commented = add_function(header, new_fn, version + 1).replace(
        f"{new_fn}(mrdocs_env* env);",
        f"{new_fn}(mrdocs_env* env /* the env; never null */);")
    at = commented.index("/* Added by the self-test.")
    end = commented.index("\n\n", at)
    commented = (commented[:at] + "#if MRDOCS_PLUGIN_ABI_TARGET >= "
                 f"{version + 1}\n" + commented[at:end]
                 + "\n#endif /* ABI\n   additions */" + commented[end:])
    expect("comments around a declaration",
           run(with_version(commented, version + 1),
               probe + f"\nABI_FN({new_fn}, void);\n", linked))

    # A function without a @since line.
    bare = add_function(header, new_fn, version).replace(
        f"   @since ABI {version}\n*/\nMRDOCS_PLUGIN_API mrdocs_status "
        f"MRDOCS_PLUGIN_CALL\n{new_fn}",
        f"*/\nMRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL\n{new_fn}")
    expect("a function without @since",
           run(bare, probe + f"\nABI_FN({new_fn}, void);\n", linked),
           "no `@since")

    # A function, a callback field and a callback type that do not name the
    # calling convention. Each is the real header with the macro taken out.
    probed = probe + f"\nABI_FN({new_fn}, void);\n"
    unconvened = add_function(header, new_fn, version).replace(
        f"MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL\n{new_fn}",
        f"MRDOCS_PLUGIN_API mrdocs_status\n{new_fn}")
    expect("a function without MRDOCS_PLUGIN_CALL",
           run(unconvened, probed, linked), f"function {new_fn}")
    callback_field = next(
        (it for it in items if it.kind == "field"
         and CALLBACK_CALL_RE.search(it.declaration)), None)
    if callback_field is None:
        failures.append("the header has no callback field to take the "
                        "calling convention from")
    else:
        expect("a callback field without MRDOCS_PLUGIN_CALL",
               run(re.sub(rf"\(\s*MRDOCS_PLUGIN_CALL\s*\*\s*"
                          rf"{callback_field.name}\s*\)",
                          f"(*{callback_field.name})", header, count=1)),
               f"field {callback_field.name}")
    callback_type = next(
        (it for it in items if it.kind == "type"
         and CALLBACK_CALL_RE.search(it.declaration)), None)
    if callback_type is None:
        failures.append("the header has no callback type to take the "
                        "calling convention from")
    else:
        expect("a callback type without MRDOCS_PLUGIN_CALL",
               run(re.sub(rf"\(\s*MRDOCS_PLUGIN_CALL\s*\*\s*"
                          rf"{callback_type.name}\s*\)",
                          f"(*{callback_type.name})", header, count=1)),
               f"type {callback_type.name}")

    # A field and an enumerator of the current version.
    field_name = "selftest_field"
    with_field, struct = add_field(header, field_name, version)
    expect("a field missing from the probe", run(with_field), field_name)
    expect("a field in the probe",
           run(with_field, probe + f"\nABI_OFFSET({struct.name}, "
                                   f"{field_name}, 0);\n"
                                   f"ABI_FIELD({struct.name}, "
                                   f"{field_name}, int);\n"))
    expect("a field with an offset and no type in the probe",
           run(with_field, probe + f"\nABI_OFFSET({struct.name}, "
                                   f"{field_name}, 0);\n"), field_name)
    expect("a field above the version",
           run(add_field(header, field_name, version + 1)[0]),
           f"ABI {version + 1}")
    enumerator = "MRDOCS_SELFTEST_ENUMERATOR"
    with_enum = add_enumerator(header, enumerator, version)
    expect("an enumerator missing from the probe", run(with_enum), enumerator)
    expect("an enumerator in the probe",
           run(with_enum, probe + f"\nABI_VALUE({enumerator}, 1000);\n"))

    # A name that the probe stops mentioning.
    for it in items:
        if it.kind in ("function", "field", "enumerator"):
            word = it.name
            stripped = "\n".join(
                ln for ln in probe.split("\n")
                if not re.search(rf"\b{re.escape(word)}\b", ln))
            if stripped != probe:
                expect(f"{it.kind} {it.name} dropped from the probe",
                       run(header, stripped), it.name)
                break

    # A type whose own @since line is gone.
    typed = next(it for it in items if it.kind == "type"
                 and it.own_since is not None)
    lines = header.split("\n")
    at = next(i for i in range(typed.line - 2, 0, -1)
              if SINCE_RE.search(lines[i]))
    lines[at] = ""
    expect("a type without @since", run("\n".join(lines)), typed.name)

    if failures:
        print("self-test failed:")
        for f in failures:
            print("  " + f)
        return 1
    print(f"self-test passed (ABI {version}, {len(items)} declarations)")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--update", action="store_true",
                      help="rewrite the partial from the header")
    mode.add_argument("--check", action="store_true",
                      help="fail if the header or the partial is wrong")
    mode.add_argument("--self-test", action="store_true",
                      help="check that the checks reject bad headers")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    text = read(HEADER_PATH)
    functions, errors = validate(
        text, read(PROBE_PATH), read(LINK_PROBE_PATH))
    if errors:
        print("include/mrdocs/plugin.h breaks the ABI rules:")
        for e in errors:
            print("  " + e)
        print("See docs/modules/ROOT/pages/contribute/workflow.adoc, "
              "'To add a function to the interface'.")
        return 1

    version = int(VERSION_RE.search(text).group(2))
    expected = render(functions, version)

    if args.update:
        os.makedirs(os.path.dirname(PARTIAL_PATH), exist_ok=True)
        with open(PARTIAL_PATH, "w", encoding="utf-8") as f:
            f.write(expected)
        print(f"wrote {os.path.relpath(PARTIAL_PATH, REPO_ROOT)} "
              f"({len(functions)} functions, ABI {version})")
        return 0

    errors = check_docs(expected)
    if errors:
        for e in errors:
            print(e)
        return 1
    print(f"plugin API reference matches plugin.h "
          f"({len(functions)} functions, ABI {version})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
