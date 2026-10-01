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

# Check that a Linux executable follows the portability policy of the release,
# in five parts: it is dynamically linked against glibc (a static glibc cannot
# load plugins, so a static or static-pie binary is rejected), it refers to no
# glibc symbol newer than the floor, it refers to no GLIBC_ tag that is not a
# version (GLIBC_PRIVATE, the internals of glibc, which change between builds,
# or GLIBC_ABI_DT_RELR, left by packed relative relocations, which older
# loaders do not support), since the floor does not cover such a tag, it
# refers to no GLIBCXX_ or CXXABI_ symbol at all, since the C++ runtime is
# linked statically, and its list of needed libraries holds nothing but the
# libraries of glibc, so that it needs nothing else from the system. This is the check auditwheel makes on a wheel for the
# manylinux policy, in a stricter form. The policy, its origin and the reason
# for the floor are in utils/cmake/release-linux.cmake and
# docs/modules/ROOT/pages/contribute/binary-compatibility.adoc.
#
# If the check fails, fix the binary. Do not raise the floor to make it pass.
#
# Usage: check-linux-portability.sh <executable> [<glibc-floor>]
#
# The floor defaults to 2.31, the glibc of Ubuntu 20.04.

set -euo pipefail

FLOOR_DEFAULT="2.31"

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: $0 <executable> [<glibc-floor>]" >&2
    exit 2
fi

binary="$1"
floor="${2:-$FLOOR_DEFAULT}"

if [[ ! -f "$binary" ]]; then
    echo "error: $binary is not a file" >&2
    exit 2
fi
if ! command -v objdump >/dev/null 2>&1; then
    echo "error: objdump is not installed (package binutils)" >&2
    exit 2
fi
if [[ ! "$floor" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
    echo "error: the glibc floor '$floor' is not a version number" >&2
    exit 2
fi

# The program headers and the dynamic section of the binary.
headers="$(objdump -p "$binary")"

# The binary has to be dynamically linked against glibc: a static glibc cannot
# load plugins, and a static binary has no version tags or needed libraries
# for the other parts of the check to find. A static binary has no program
# interpreter, and a static-pie one has no libc.so.6 in its needed libraries.
if ! grep -q '^ *INTERP ' <<<"$headers" ||
   ! awk '$1 == "NEEDED" && $2 == "libc.so.6" {found = 1} END {exit !found}' <<<"$headers"; then
    echo "error: $binary is not dynamically linked against glibc (a static binary)." >&2
    echo "  A static glibc cannot load plugins. Link dynamically against glibc and" >&2
    echo "  link the C++ runtime statically instead: see utils/cmake/release-linux.cmake" >&2
    echo "Linux portability check failed for $binary (glibc floor $floor)" >&2
    exit 1
fi

# The dynamic symbols the binary refers to or defines, with their versions.
symbols="$(objdump -T "$binary")"

# The libraries glibc is made of, which the floor already covers. Anything else
# in the list of needed libraries is a dependency on the system.
allowed_needed='^(libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|libutil\.so\.1|libresolv\.so\.2|ld-linux[A-Za-z0-9_.-]*\.so\.[0-9]+|ld64\.so\.[0-9]+)$'

failed=0

# True when version $1 is greater than version $2.
version_greater() {
    [[ "$1" != "$2" && "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" == "$1" ]]
}

# Every GLIBC_ tag in the output. A tag that is a version, GLIBC_x.y[.z], is
# compared with the floor. A tag that is not, such as GLIBC_PRIVATE or
# GLIBC_ABI_DT_RELR, is no version of the interface the floor covers, so it
# fails the check on its own.
newer=()
while read -r tag; do
    version="${tag#GLIBC_}"
    if [[ ! "$version" =~ ^[0-9]+(\.[0-9]+)+$ ]]; then
        echo "error: $binary refers to the glibc tag $tag, which is not a version" >&2
        failed=1
        continue
    fi
    if version_greater "$version" "$floor"; then
        newer+=("$tag")
    fi
done < <(grep -o 'GLIBC_[A-Za-z0-9_.]*' <<<"$symbols" | sort -u)

if (( ${#newer[@]} > 0 )); then
    failed=1
    echo "error: $binary needs a glibc newer than $floor:" >&2
    for tag in "${newer[@]}"; do
        echo "  $tag, used by:" >&2
        # objdump -T prints "address flags section address version-tag name".
        # Newer binutils put the tag in parentheses for an undefined symbol and
        # binutils 2.34, the one of Ubuntu 20.04, does not, so the tag is
        # matched as a field with or without them.
        awk -v t="$tag" '{
            for (i = 1; i < NF; ++i) {
                f = $i
                gsub(/[()]/, "", f)
                if (f == t) { print "    " $NF; break }
            }
        }' <<<"$symbols" | sort -u >&2
    done
fi

cxx_refs="$(grep -o -E '(GLIBCXX|CXXABI)_[A-Za-z0-9_.]*' <<<"$symbols" | sort -u || true)"
if [[ -n "$cxx_refs" ]]; then
    failed=1
    echo "error: $binary refers to the C++ runtime of the system, which has to be linked statically:" >&2
    while read -r ref; do
        echo "  $ref" >&2
    done <<<"$cxx_refs"
fi

needed="$(awk '$1 == "NEEDED" {print $2}' <<<"$headers" | sort -u)"
unexpected=()
while read -r lib; do
    [[ -z "$lib" ]] && continue
    if [[ ! "$lib" =~ $allowed_needed ]]; then
        unexpected+=("$lib")
    fi
done <<<"$needed"
if (( ${#unexpected[@]} > 0 )); then
    failed=1
    echo "error: $binary needs libraries other than the ones of glibc:" >&2
    for lib in "${unexpected[@]}"; do
        echo "  $lib" >&2
    done
fi

if (( failed )); then
    echo "Linux portability check failed for $binary (glibc floor $floor)" >&2
    exit 1
fi

echo "Linux portability check passed for $binary (glibc floor $floor)"
