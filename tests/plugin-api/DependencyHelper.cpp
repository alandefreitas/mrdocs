//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A shared library a plugin depends on, built twice by the CMake files that
// use it. In one build it is kept in a directory that is neither the
// directory of the plugin, nor the subdirectory named after it, nor the one
// holding the `mrdocs` executable, and the ctest entry puts that directory in
// PATH, which the Windows loader must not consult when it resolves a plugin's
// dependencies. In the other build it is kept in the subdirectory named after
// the plugin, where the loader must find it. Where it sits is decided by the
// CMake target. It returns a value only this library knows, so that a plugin
// that calls it can tell the call reached this library.

#ifdef _WIN32
#    define MRDOCS_TEST_EXPORT __declspec(dllexport)
#else
#    define MRDOCS_TEST_EXPORT __attribute__((__visibility__("default")))
#endif

extern "C" MRDOCS_TEST_EXPORT int
mrdocs_test_dependency_helper()
{
    return 42;
}
