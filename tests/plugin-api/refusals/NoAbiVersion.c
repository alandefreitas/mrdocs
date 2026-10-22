/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* A library that has an entry point and no function that reports the ABI,
   which is what a plugin looks like when the export of
   `mrdocs_plugin_abi_version` is lost (a mangled name, or hidden visibility).
   MrDocs has to refuse it by name: every library in plugins/ is a plugin, and
   this one lacks `mrdocs_plugin_abi_version`. */

#include <mrdocs/plugin.h>

MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT mrdocs_status
mrdocs_plugin_init(mrdocs_env* env);

MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT mrdocs_status
mrdocs_plugin_init(mrdocs_env* env)
{
    (void)env;
    return MRDOCS_STATUS_OK;
}
