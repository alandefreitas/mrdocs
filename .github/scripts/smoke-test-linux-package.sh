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
# binary starts, and it loads a plugin built there with the compiler the
# distribution ships. This is the proof of the portability policy in
# utils/cmake/release-linux.cmake: the glibc floor and the static C++ runtime
# let one binary run on distributions it was not built on, and a plugin is
# free to use a C++ runtime that Mr.Docs does not have.
#
# Usage: smoke-test-linux-package.sh <image> <package-dir>
#
# Run it from the root of a checkout, on a host that has docker. <package-dir>
# holds MrDocs-*.tar.gz. The script starts itself in the container with
# --inside.

set -euo pipefail

if [[ "${1:-}" == "--inside" ]]; then
    package_dir="$2"
    export DEBIAN_FRONTEND=noninteractive

    # The image of a distribution that has left support carries commented
    # snapshot.debian.org lines in its sources, for a date on which the mirrors
    # still had every package. The mirrors no longer serve all of them
    # (debian:11 gets no libc6-dev from them), so the snapshot is used when the
    # image has one. The Release files of a snapshot are past their validity
    # date by construction.
    apt_options=()
    if grep -q '^# *deb http://snapshot\.debian\.org/' /etc/apt/sources.list 2>/dev/null; then
        sed -i \
            -e '/^deb http:\/\/deb\.debian\.org/s/^/# /' \
            -e 's/^# *\(deb http:\/\/snapshot\.debian\.org\/\)/\1/' \
            /etc/apt/sources.list
        apt_options+=(-o Acquire::Check-Valid-Until=false)
    fi

    echo "::group::Install the distribution compiler"
    apt-get "${apt_options[@]}" update
    apt-get "${apt_options[@]}" install -y --no-install-recommends g++
    g++ --version | head -n 1
    echo "::endgroup::"

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

    addons=/tmp/plugin-addons
    mkdir -p "$addons/plugins"
    g++ -std=c++17 -fPIC -shared -fvisibility=hidden \
        -I"$prefix/include" \
        tests/cmake/src/plugin.cpp \
        -o "$addons/plugins/consumer_plugin.so"

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

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <image> <package-dir>" >&2
    exit 2
fi

image="$1"
package_dir="$2"

docker run --rm \
    -v "$PWD:/work" -w /work \
    "$image" \
    bash .github/scripts/smoke-test-linux-package.sh --inside "$package_dir"
