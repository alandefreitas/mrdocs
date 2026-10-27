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

# Run the Linux release package in a container of an older distribution: the
# binary starts with the C++ runtime of the package, and it loads a plugin
# that was built elsewhere, in the release container, with GCC 15. This is the
# proof of the policy in utils/cmake/release-linux.cmake: the glibc floor and
# the libstdc++.so.6 and libgcc_s.so.1 shipped in lib/ let one package run on
# distributions it was not built on, and let a plugin that needs a libstdc++
# newer than the distribution's (GLIBCXX_3.4.33, which GCC 14 and later
# headers reach) load there. The plugin is prebuilt because the older
# distributions have no compiler that handles the C++23 of the MrDocs headers:
# debian:11 has GCC 10, and ubuntu:20.04 has GCC 9.
#
# Usage: smoke-test-linux-package.sh <image> <package-dir> <plugin-dir>
#
# Run it from the root of a checkout, on a host that has docker. <package-dir>
# holds MrDocs-*.tar.gz and <plugin-dir> holds libconsumer_plugin.so, the plugin
# of tests/cmake built against that package. The script starts itself in the
# container with --inside. Nothing is installed in the container.

set -euo pipefail

if [[ "${1:-}" == "--inside" ]]; then
    package_dir="$2"
    plugin_dir="$3"

    prefix=/tmp/mrdocs
    mkdir -p "$prefix"
    package="$(find "$package_dir" -maxdepth 1 -name 'MrDocs-*.tar.gz' -print -quit)"
    if [[ -z "$package" ]]; then
        echo "error: no MrDocs-*.tar.gz in $package_dir" >&2
        exit 1
    fi
    tar -xzf "$package" -C "$prefix" --strip-components=1

    # shellcheck source=/dev/null
    . /etc/os-release
    echo "Running $("$prefix/bin/mrdocs" --version | head -n 1) on $PRETTY_NAME"
    echo "The libstdc++ of this distribution:"
    ls -l /usr/lib/x86_64-linux-gnu/libstdc++.so.6* || true

    # The executable has to take its C++ runtime from the package, whatever
    # the distribution carries.
    for runtime in libstdc++.so.6 libgcc_s.so.1; do
        resolved="$(ldd "$prefix/bin/mrdocs" | awk -v lib="$runtime" '$1 == lib {print $3}')"
        if [[ "$(readlink -f "$resolved")" != "$(readlink -f "$prefix")/lib/"* ]]; then
            echo "error: mrdocs resolves $runtime to '$resolved', not to $prefix/lib" >&2
            exit 1
        fi
    done

    addons=/tmp/plugin-addons
    mkdir -p "$addons/plugins"
    cp "$plugin_dir/libconsumer_plugin.so" "$addons/plugins/"

    # MrDocs resolves a relative compilation database against the directory of
    # the config file, so the paths are absolute.
    output=/tmp/plugin-reference
    "$prefix/bin/mrdocs" \
        --config="$PWD/tests/cmake/mrdocs.yml" \
        --compilation-database="$PWD/tests/cmake/docs/compile_commands.json" \
        --output="$output" \
        --addons-supplemental="$addons" \
        --generator=consumer-probe

    if [[ ! -s "$output/probe.txt" ]]; then
        echo "error: the plugin did not write $output/probe.txt" >&2
        exit 1
    fi
    echo "The plugin loaded and ran: probe.txt says $(cat "$output/probe.txt") symbols"
    exit 0
fi

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <image> <package-dir> <plugin-dir>" >&2
    exit 2
fi

image="$1"
package_dir="$2"
plugin_dir="$3"

docker run --rm \
    -v "$PWD:/work" -w /work \
    "$image" \
    bash .github/scripts/smoke-test-linux-package.sh --inside "$package_dir" "$plugin_dir"
