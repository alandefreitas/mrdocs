//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A plugin that calls a function of the plugin API that this MrDocs does not
// have, which is what a plugin made for a later MrDocs looks like once it
// uses something added after the ABI this MrDocs provides. The loader
// resolves every symbol when it opens a library, so the library has to be
// refused right there, the message has to name the function, and it has to
// say that the plugin may need a newer MrDocs, since the plugin never gets
// far enough to report the ABI it needs. The ctest entry checks that.

#include <mrdocs/plugin.h>

extern "C" int mrdocs_function_of_a_later_abi();

MRDOCS_PLUGIN_INIT(env)
{
    (void)env;
    mrdocs_function_of_a_later_abi();
    return MRDOCS_STATUS_OK;
}
