//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A library with none of the entry points, as a dependency of a plugin would
// be if it were put in the plugins directory instead of in the subdirectory
// named after the plugin. Every library there is taken for a plugin, so it
// has to be refused by the first function the loader looks for.

#include <mrdocs/Plugin.hpp>

extern "C" MRDOCS_PLUGIN_EXPORT int
mrdocs_test_not_a_plugin()
{
    return 1;
}
