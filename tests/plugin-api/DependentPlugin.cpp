//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A plugin that links a shared library, built twice by the CMake files that
// use it. In one build the library is only reachable through PATH: the Windows
// loader looks for a plugin's dependencies in the plugin's directory, the
// subdirectory named after the plugin, the application directory and the
// system directories, so loading has to fail, and the ctest entry checks that
// the plugin is refused at load time. In the other the library sits in the
// subdirectory named after the plugin, the plugin has to load, and its entry
// point has to get the value that library returns. Where the library sits is
// decided by the CMake target.

#include <mrdocs/plugin.h>
#include <cstdio>

#ifdef _WIN32
#    define MRDOCS_TEST_IMPORT __declspec(dllimport)
#else
#    define MRDOCS_TEST_IMPORT
#endif

extern "C" MRDOCS_TEST_IMPORT int mrdocs_test_dependency_helper();

MRDOCS_PLUGIN_INIT(env)
{
    int const value = mrdocs_test_dependency_helper();
    if (value != 42)
    {
        char message[64];
        std::snprintf(message, sizeof(message),
            "the dependency returned %d, not 42", value);
        mrdocs_set_error(env, message);
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    return MRDOCS_STATUS_OK;
}
