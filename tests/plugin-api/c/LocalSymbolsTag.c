/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* The function LocalSymbols.c calls, in a translation unit of its own so that
   no compiler can see its body from the call. Built with a different
   MRDOCS_TEST_TAG into each of the two libraries. */

#include <mrdocs/plugin.h>

#ifndef MRDOCS_TEST_TAG
#    error "MRDOCS_TEST_TAG names the library"
#endif

MRDOCS_PLUGIN_EXPORT const char*
mrdocs_test_tag(void);

MRDOCS_PLUGIN_EXPORT const char*
mrdocs_test_tag(void)
{
    return MRDOCS_TEST_TAG;
}
