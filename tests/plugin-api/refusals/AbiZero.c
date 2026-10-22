/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* A plugin that reports ABI 0, a version no MrDocs has ever provided, which
   is what an uninitialized or hand-written version function returns. MrDocs
   has to refuse it by name before it calls the entry point. Both entry
   points are written out by hand, since `MRDOCS_PLUGIN_INIT` would report
   the ABI the header targets. */

#include <mrdocs/plugin.h>

MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT uint32_t
mrdocs_plugin_abi_version(void);

MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT mrdocs_status
mrdocs_plugin_init(mrdocs_env* env);

MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT uint32_t
mrdocs_plugin_abi_version(void)
{
    return 0;
}

MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT mrdocs_status
mrdocs_plugin_init(mrdocs_env* env)
{
    (void)env;
    return MRDOCS_STATUS_OK;
}
