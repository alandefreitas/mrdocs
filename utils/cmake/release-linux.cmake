#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# Link flags of the Linux release, included by the root CMakeLists.txt when
# MRDOCS_LINUX_RELEASE is ON. The CMake Workflow step of
# .github/workflows/ci-build.yml turns it on for the matrix entry of
# ci-matrix.yml that sets mrdocs-linux-release, the gcc build that produces
# the Linux package.
#
# Policy: the same one Python calls manylinux. The release binary may depend
# on glibc up to a floor, and on nothing else from the system. It is built in
# an old distribution with a current compiler, it links libstdc++ and libgcc
# statically, and a checker rejects the finished binary if it is not
# dynamically linked against glibc, refers to a newer glibc symbol or to a
# GLIBC_ tag that is not a version (GLIBC_PRIVATE), or to any GLIBCXX_ or
# CXXABI_ symbol, or needs a library that is not one of glibc
# (utils/release/check-linux-portability.sh, the counterpart of auditwheel;
# utils/release/test-check-linux-portability.sh shows each rejection firing).
#
# Floor: glibc 2.31, the glibc of Ubuntu 20.04, the container the release is
# built in (.github/workflows/ci-matrix.yml). Debian 11, Fedora 32, RHEL 9
# and everything newer are covered, since glibc is backward compatible.
#
# Reason: plugins must load, and the binary must run on Ubuntu 20.04 and
# newer. A binary linked with -static does neither dependably: a static glibc
# cannot dlopen a plugin without the shared glibc of the same version, and a
# dynamic binary built on a recent distribution needs that distribution's
# glibc. Linking only the C++ runtime statically removes the dependency on the
# libstdc++ of the user's machine while leaving dlopen to the real glibc.
#
# Origin: manylinux (PEP 600) and auditwheel. Their policy allows a short
# whitelist of system libraries up to capped symbol versions, libstdc++ among
# them. This release is stricter and allows none, because it links libstdc++
# and libgcc statically. See docs/modules/ROOT/pages/contribute/binary-compatibility.adoc.
#
# Do not raise the floor by updating the container to a newer Ubuntu. Raising
# it is a decision, taken together with the checker.
#
# What each setting is for:
#
#   -static-libstdc++ -static-libgcc
#       The C++ runtime and the compiler runtime are part of the executable,
#       so the libstdc++ of the user's distribution never matters. Plugins
#       use whatever runtime they like: no C++ object crosses the C API.
#
#   hidden visibility (CMAKE_CXX_VISIBILITY_PRESET, CMAKE_VISIBILITY_INLINES_HIDDEN)
#       Nothing of MrDocs's own targets is exported by default. Only the
#       functions that carry MRDOCS_PLUGIN_API (mrdocs_*) are. The presets
#       say nothing about LLVM and Clang, which bootstrap.py builds with
#       default visibility, or about the static libstdc++ and libgcc, which
#       the compiler ships that way: the version script keeps those out.
#
#   the version script of tools/mrdocs (exports-linux.map)
#       Narrows the dynamic symbol table of the executable to mrdocs_* and
#       nothing else, including what LLVM, Clang and the static C++ runtime
#       define. This is what keeps the statically linked libstdc++ from being
#       visible to, or replaced by, the libstdc++ of a plugin. It is applied when
#       MRDOCS_NARROW_EXPORTS is ON, and tools/mrdocs/CMakeLists.txt makes the
#       configuration fail if MRDOCS_LINUX_RELEASE is ON and it is not.
#
#   not -Wl,--exclude-libs,ALL
#       It would hide the C API too, since the mrdocs_* functions are defined
#       in the static library mrdocs-core: the linker applies it to every
#       archive and it overrides the version script. The version script gives
#       the same result for the executable without that effect.
#
#   no -static
#       See "Reason" above. A -static or -static-pie in the flags is an error here.

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "MRDOCS_LINUX_RELEASE is for the Linux release")
endif ()
if (MRDOCS_BUILD_SHARED)
    message(FATAL_ERROR
        "MRDOCS_LINUX_RELEASE is for the static build that is released; "
        "MRDOCS_BUILD_SHARED must be OFF")
endif ()
# MRDOCS_STATIC_LINK_FLAGS is computed by the root CMakeLists.txt: -static or
# -static-pie in the common flags or in the flags of any configuration.
# utils/release/check-linux-portability.sh checks the binary itself.
if (MRDOCS_STATIC_LINK_FLAGS)
    message(FATAL_ERROR
        "MRDOCS_LINUX_RELEASE does not allow -static: a static glibc cannot "
        "load plugins, and the portability of the release comes from the glibc "
        "floor and the static C++ runtime. See utils/cmake/release-linux.cmake")
endif ()

string(APPEND CMAKE_EXE_LINKER_FLAGS " -static-libstdc++ -static-libgcc")

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
