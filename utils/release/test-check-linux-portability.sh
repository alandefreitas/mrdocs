#!/usr/bin/env bash
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# Self-test of check-linux-portability.sh. The release runs the check on a
# binary that is supposed to pass, which shows that the script accepts a good
# binary and nothing else. This runs it against a stub objdump that prints
# fixed listings, in the format of binutils 2.34 (Ubuntu 20.04, which prints
# the version tag of an undefined symbol without parentheses) and in the
# format of binutils 2.35 and later, and expects each of the five rejections to
# fire with its message, and a clean listing to pass. If objdump changes the
# shape of its output in a way that makes a part of the check stop matching,
# the real binary would pass silently; the listings here are what pins that.
#
# Usage: test-check-linux-portability.sh

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
check="$here/check-linux-portability.sh"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin"

# The stub prints the listing the case put in place, for -p and for -T.
cat >"$work/bin/objdump" <<'STUB'
#!/usr/bin/env bash
case "$1" in
    -p) cat "$FIXTURE_DIR/headers" ;;
    -T) cat "$FIXTURE_DIR/symbols" ;;
    *) echo "stub objdump: unexpected option $1" >&2; exit 2 ;;
esac
STUB
chmod +x "$work/bin/objdump"
: >"$work/binary"

# Write the output of `objdump -p`. The first argument says whether the
# binary has a program interpreter (interp or static), the second whether
# libc.so.6 is among its needed libraries (libc or nolibc); the rest are
# further needed libraries.
write_headers() {
    local interp="$1" libc="$2"
    shift 2
    {
        echo
        echo "$work/binary:     file format elf64-x86-64"
        echo
        echo "Program Header:"
        echo "    PHDR off    0x0000000000000040 vaddr 0x0000000000000040 paddr 0x0000000000000040 align 2**3"
        if [[ "$interp" == interp ]]; then
            echo "  INTERP off    0x0000000000000318 vaddr 0x0000000000000318 paddr 0x0000000000000318 align 2**0"
        fi
        echo "    LOAD off    0x0000000000000000 vaddr 0x0000000000000000 paddr 0x0000000000000000 align 2**12"
        echo " DYNAMIC off    0x0000000000002da0 vaddr 0x0000000000003da0 paddr 0x0000000000003da0 align 2**3"
        echo
        echo "Dynamic Section:"
        echo "  NEEDED               libm.so.6"
        if [[ "$libc" == libc ]]; then
            echo "  NEEDED               libc.so.6"
        fi
        for lib in "$@"; do
            echo "  NEEDED               $lib"
        done
        echo "  SONAME               example"
        echo
    } >"$FIXTURE_DIR/headers"
}

# Write the output of `objdump -T`. The first argument is the format of the
# binutils release (old: 2.34, no parentheses around the version tag of an
# undefined symbol; new: 2.35 and later); each of the rest is a version tag
# followed by the name of an undefined symbol that carries it.
write_symbols() {
    local format="$1" tag name
    shift
    {
        echo
        echo "$work/binary:     file format elf64-x86-64"
        echo
        echo "DYNAMIC SYMBOL TABLE:"
        echo "0000000000000000      DF *UND*	0000000000000000 (GLIBC_2.2.5) memcpy"
        echo "0000000000001139 g    DF .text	0000000000000010  Base        entry_point"
        for pair in "$@"; do
            tag="${pair%% *}"
            name="${pair#* }"
            if [[ "$format" == old ]]; then
                echo "0000000000000000      DF *UND*	0000000000000000  $tag $name"
            else
                echo "0000000000000000      DF *UND*	0000000000000000 ($tag) $name"
            fi
        done
    } >"$FIXTURE_DIR/symbols"
}

failures=0
export FIXTURE_DIR="$work"

# expect <name> <status> <text>: runs the check on the listings in place,
# and requires the exit status and, when <text> is not empty, a line of the
# output that holds it.
expect() {
    local name="$1" status="$2" text="$3" output got=0
    output="$(PATH="$work/bin:$PATH" "$check" "$work/binary" "${@:4}" 2>&1)" || got=$?
    if [[ "$got" != "$status" ]]; then
        echo "FAIL $name: exit status $got, expected $status"
        echo "$output" | sed 's/^/    /'
        failures=$((failures + 1))
    elif [[ -n "$text" ]] && ! grep -qF -- "$text" <<<"$output"; then
        echo "FAIL $name: the output lacks: $text"
        echo "$output" | sed 's/^/    /'
        failures=$((failures + 1))
    else
        echo "ok   $name"
    fi
}

for format in new old; do
    write_headers interp libc
    write_symbols "$format" "GLIBC_2.17 clock_gettime" "GLIBC_2.31 pthread_cond_clockwait"
    expect "clean listing ($format)" 0 "Linux portability check passed"

    write_symbols "$format" "GLIBC_2.17 clock_gettime" "GLIBC_2.34 pthread_create"
    expect "glibc newer than the floor ($format)" 1 "needs a glibc newer than 2.31"
    expect "glibc newer than the floor names the symbol ($format)" 1 "    pthread_create"
    expect "glibc newer than the floor names the tag ($format)" 1 "GLIBC_2.34, used by:"
    expect "a higher floor accepts the same listing ($format)" 0 "Linux portability check passed" 2.34

    write_symbols "$format" "GLIBC_2.17 clock_gettime" "GLIBC_PRIVATE _dl_find_object"
    expect "GLIBC_PRIVATE reference ($format)" 1 "refers to the glibc tag GLIBC_PRIVATE, which is not a version"
    expect "GLIBC_PRIVATE is not a version, whatever the floor ($format)" 1 "which is not a version" 2.99

    write_symbols "$format" "GLIBC_ABI_DT_RELR __libc_start_main"
    expect "GLIBC_ABI_DT_RELR reference ($format)" 1 "refers to the glibc tag GLIBC_ABI_DT_RELR, which is not a version"

    write_symbols "$format" "GLIBCXX_3.4.30 _ZNSt6localeC1Ev"
    expect "GLIBCXX reference ($format)" 1 "refers to the C++ runtime of the system"
    expect "GLIBCXX reference names the tag ($format)" 1 "  GLIBCXX_3.4.30"

    write_symbols "$format" "CXXABI_1.3.13 __cxa_throw"
    expect "CXXABI reference ($format)" 1 "refers to the C++ runtime of the system"
    expect "CXXABI reference names the tag ($format)" 1 "  CXXABI_1.3.13"

    write_symbols "$format"
    write_headers interp libc libz.so.1
    expect "needed library outside glibc ($format)" 1 "needs libraries other than the ones of glibc"
    expect "needed library names the library ($format)" 1 "  libz.so.1"

    write_headers static libc
    expect "no program interpreter ($format)" 1 "is not dynamically linked against glibc"

    write_headers interp nolibc
    expect "no libc.so.6 among the needed libraries ($format)" 1 "is not dynamically linked against glibc"
done

write_headers interp libc
write_symbols new
got=0
"$check" >/dev/null 2>&1 || got=$?
if [[ "$got" != 2 ]]; then
    echo "FAIL usage: exit status $got, expected 2"
    failures=$((failures + 1))
fi

if (( failures )); then
    echo "$failures check(s) failed"
    exit 1
fi
echo "All checks passed"
