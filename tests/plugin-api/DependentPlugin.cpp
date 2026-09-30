//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A plugin that links a shared library which is only reachable through
// PATH. The Windows loader looks for a plugin's dependencies in the
// plugin's directory, the application directory and the system
// directories, so loading has to fail. The ctest entry checks that the
// plugin is refused at load time.

#include <mrdocs/Plugin.hpp>

extern "C" __declspec(dllimport) int mrdocs_test_dependency_helper();

MRDOCS_PLUGIN_MAIN(context)
{
    (void)context;
    mrdocs_test_dependency_helper();
    return {};
}
