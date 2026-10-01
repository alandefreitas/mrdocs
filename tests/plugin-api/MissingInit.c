//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Gennaro Prota (gennaro.prota@gmail.com)
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

/* A plugin that reports an ABI this MrDocs accepts and exports no entry
   point, which is what a library written without `MRDOCS_PLUGIN_INIT`, or
   with the entry point misspelled, looks like. Loading it has to fail; the
   ctest entry checks that the run fails, and by the diagnostic that it
   fails for this reason. */

#include <mrdocs/plugin.h>

MRDOCS_PLUGIN_EXPORT uint32_t MRDOCS_PLUGIN_CALL
mrdocs_plugin_abi_version(void)
{
    return MRDOCS_PLUGIN_ABI_TARGET;
}
