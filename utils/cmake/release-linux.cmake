#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
#
# Official repository: https://github.com/cppalliance/mrdocs
#

# Settings of the Linux release, included by the root CMakeLists.txt when
# MRDOCS_LINUX_RELEASE is ON. The CMake Workflow step of
# .github/workflows/ci-build.yml turns it on for the matrix entry of
# ci-matrix.yml that sets mrdocs-linux-release, the gcc build that produces
# the Linux package.
#
# Policy: the one Python calls manylinux, with one difference. The release
# binary may depend on glibc up to a floor, and on nothing else from the
# system except the C++ runtime that the package ships itself. It is built in
# an old distribution with a current compiler, the package carries the
# libstdc++.so.6 and libgcc_s.so.1 of that compiler in lib/, and the
# executable finds them through RUNPATH $ORIGIN/../lib. A checker rejects the
# finished package if it is not dynamically linked against glibc, refers to a
# newer glibc symbol or to a GLIBC_ tag that is not a version (GLIBC_PRIVATE),
# needs a library that is neither one of glibc nor one of the bundled two,
# or refers to a GLIBCXX_, CXXABI_ or GCC_ version that the bundled libraries
# do not define (utils/release/check-linux-portability.sh, the counterpart of
# auditwheel; utils/release/test-check-linux-portability.sh shows each
# rejection firing).
#
# Floor: glibc 2.31, the glibc of Ubuntu 20.04, the container the release is
# built in (.github/workflows/ci-matrix.yml). Debian 11, Fedora 32, RHEL 9
# and everything newer are covered, since glibc is backward compatible.
#
# Reason: plugins must load, and the binary must run on Ubuntu 20.04 and
# newer. A binary linked with -static does neither dependably: a static glibc
# cannot dlopen a plugin without the shared glibc of the same version, and a
# dynamic binary built on a recent distribution needs that distribution's
# glibc. A plugin includes the C++23 headers of MrDocs, so it is built with
# GCC 14 or later, and its libstdc++ references reach GLIBCXX_3.4.33, which
# the libstdc++ of Ubuntu 20.04 (GCC 10, up to GLIBCXX_3.4.28) does not
# define. The package therefore carries the libstdc++ of the compiler that
# built it. The executable loads that copy first, and when it loads a plugin
# the dependency of the plugin on libstdc++.so.6 resolves to the copy that is
# already loaded, so one runtime serves both and the plugin loads on a
# distribution whose own libstdc++ is older.
#
# Origin: manylinux (PEP 600) and auditwheel. Their policy allows a short
# whitelist of system libraries up to capped symbol versions, libstdc++ among
# them. This release allows none from the system and ships the two runtime
# libraries instead. See docs/modules/ROOT/pages/contribute/binary-compatibility.adoc.
#
# Do not raise the floor by updating the container to a newer Ubuntu. Raising
# it is a decision, taken together with the checker.
#
# What each setting is for:
#
#   lib/libstdc++.so.6 and lib/libgcc_s.so.1
#       Copied from the compiler that builds the release (the files named by
#       `${CMAKE_CXX_COMPILER} -print-file-name=...`, followed to the real
#       file and installed under the soname), so the runtime is at least as
#       new as the headers and the code the compiler produced.
#
#   RUNPATH $ORIGIN/../lib on the executable (CMAKE_INSTALL_RPATH)
#       Where the executable finds the two libraries, wherever the package is
#       unpacked. It is a RUNPATH and not a hard-coded path, so the package
#       is relocatable. -Wl,--enable-new-dtags makes the linker write a
#       RUNPATH, which is what the check expects.
#
#   the version script of tools/mrdocs (exports-linux.map)
#       Narrows the dynamic symbol table of the executable to the MrDocs API,
#       and nothing that LLVM, Clang, Lua or the libraries it links define.
#       The root CMakeLists.txt turns it on by default for this build, and
#       this file makes the configuration fail if it is not on.
#
#   no -static
#       See "Reason" above. A -static or -static-pie in the flags is an error here.

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "MRDOCS_LINUX_RELEASE is for the Linux release")
endif ()
if (MRDOCS_BUILD_SHARED)
    message(FATAL_ERROR
        "MRDOCS_LINUX_RELEASE is for the build with static MrDocs libraries that is released; "
        "MRDOCS_BUILD_SHARED must be OFF")
endif ()
# MRDOCS_STATIC_LINK_FLAGS is computed by the root CMakeLists.txt: -static,
# --static, -static-pie or -Wl,-static in the common flags or in the flags of
# any configuration. utils/release/check-linux-portability.sh checks the
# binary itself.
if (MRDOCS_STATIC_LINK_FLAGS)
    message(FATAL_ERROR
        "MRDOCS_LINUX_RELEASE does not allow -static: a static glibc cannot "
        "load plugins, and the portability of the release comes from the glibc "
        "floor and the C++ runtime shipped in lib/. See "
        "utils/cmake/release-linux.cmake")
endif ()
if (NOT MRDOCS_PLUGIN_MODULES_SUPPORTED)
    message(FATAL_ERROR
        "MRDOCS_LINUX_RELEASE needs a MrDocs that loads plugins, and "
        "MRDOCS_PLUGIN_MODULES_SUPPORTED is off")
endif ()
# The release exports the MrDocs API and nothing else, so that a plugin that
# bundles its own LLVM or Lua binds to its own copy. The root CMakeLists.txt
# resolves MRDOCS_NARROW_EXPORTS before including this file.
if (NOT MRDOCS_NARROW_EXPORTS)
    message(FATAL_ERROR
        "MRDOCS_LINUX_RELEASE needs the narrowed export table of the mrdocs "
        "executable: configure with MRDOCS_NARROW_EXPORTS=ON (or AUTO) and "
        "without a sanitizer. See utils/cmake/release-linux.cmake")
endif ()
# The checker expects RUNPATH $ORIGIN/../lib and the libraries in lib/.
if (NOT CMAKE_INSTALL_LIBDIR STREQUAL "lib")
    message(FATAL_ERROR
        "MRDOCS_LINUX_RELEASE expects CMAKE_INSTALL_LIBDIR to be \"lib\", "
        "not \"${CMAKE_INSTALL_LIBDIR}\": the check of the package looks for "
        "RUNPATH $ORIGIN/../lib. Configure with -DCMAKE_INSTALL_LIBDIR=lib")
endif ()

# The C++ runtime of the compiler, bundled into the package.
function(mrdocs_find_compiler_runtime_library name out)
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=${name}
        OUTPUT_VARIABLE path
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE result)
    if (NOT result EQUAL 0 OR NOT IS_ABSOLUTE "${path}" OR NOT EXISTS "${path}")
        message(FATAL_ERROR
            "MRDOCS_LINUX_RELEASE: ${CMAKE_CXX_COMPILER} -print-file-name=${name} "
            "did not name a file (it printed \"${path}\")")
    endif ()
    get_filename_component(real "${path}" REALPATH)
    set(${out} "${real}" PARENT_SCOPE)
endfunction()
mrdocs_find_compiler_runtime_library(libstdc++.so.6 MRDOCS_BUNDLED_LIBSTDCXX)
mrdocs_find_compiler_runtime_library(libgcc_s.so.1 MRDOCS_BUNDLED_LIBGCC_S)
message(STATUS "Linux release bundles ${MRDOCS_BUNDLED_LIBSTDCXX} and ${MRDOCS_BUNDLED_LIBGCC_S}")

# The executable looks for the bundled libraries next to bin/. The variable
# applies to the targets created after this file is included, which includes
# the mrdocs executable.
set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
# Ask for a RUNPATH rather than an RPATH, whatever the default of the linker is.
string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,--enable-new-dtags")

if (MRDOCS_INSTALL)
    install(FILES "${MRDOCS_BUNDLED_LIBSTDCXX}"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RENAME libstdc++.so.6
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE
                    WORLD_READ WORLD_EXECUTE)
    install(FILES "${MRDOCS_BUNDLED_LIBGCC_S}"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RENAME libgcc_s.so.1
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE
                    WORLD_READ WORLD_EXECUTE)
endif ()
