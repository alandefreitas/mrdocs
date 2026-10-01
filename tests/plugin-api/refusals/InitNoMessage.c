/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* A plugin whose entry point fails and does not say why. MrDocs has to stop
   the run all the same, with the status the entry point returned. */

#include <mrdocs/plugin.h>

MRDOCS_PLUGIN_INIT(env)
{
    (void)env;
    return MRDOCS_STATUS_PLUGIN_ERROR;
}
