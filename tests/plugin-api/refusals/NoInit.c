/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* A library that reports an ABI and has no entry point, which is what a
   plugin that lost its `mrdocs_plugin_init` looks like. MrDocs has to refuse
   it by name: every library in plugins/ is a plugin, and this one lacks
   `mrdocs_plugin_init`. The ABI function is written out by hand, since
   `MRDOCS_PLUGIN_INIT` would define the entry point too. */

#include <mrdocs/plugin.h>

MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT uint32_t
mrdocs_plugin_abi_version(void);

MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT uint32_t
mrdocs_plugin_abi_version(void)
{
    return MRDOCS_PLUGIN_ABI_TARGET;
}
