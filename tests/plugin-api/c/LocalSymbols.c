/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* A plugin whose symbols have to stay its own. Built twice, with a different
   MRDOCS_TEST_TAG, it exports a function of the same name from each library.
   The loader opens libraries so that their symbols are not offered to the
   libraries loaded later, so the generator of each one has to reach its own
   function. A loader that made the symbols global would let the library that
   loads second call the function of the first. */

#include <mrdocs/plugin.h>
#include <string.h>

#ifndef MRDOCS_TEST_TAG
#    error "MRDOCS_TEST_TAG names the library"
#endif

/* Defined in LocalSymbolsTag.c, a translation unit of its own. The call in
   `build` has to stay a call through the symbol table of the library: a
   compiler that sees the body, as Clang does for a definition in the same
   file even with -fPIC, folds the call into the tag and the test could no
   longer tell a loader that kept the symbols local from one that did not. */
MRDOCS_PLUGIN_EXPORT const char*
mrdocs_test_tag(void);

static mrdocs_status MRDOCS_PLUGIN_CALL
build(mrdocs_env* env, void* data)
{
    (void)data;
    if (strcmp(mrdocs_test_tag(), MRDOCS_TEST_TAG) != 0)
    {
        mrdocs_set_error(env,
            "local-symbols: the call reached the function of another library");
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    return mrdocs_log(env, MRDOCS_LOG_INFO, "local-symbols: kept to itself");
}

MRDOCS_PLUGIN_INIT(env)
{
    mrdocs_generator_desc desc = { .struct_size = sizeof(mrdocs_generator_desc) };
    desc.id = "local-symbols-" MRDOCS_TEST_TAG;
    desc.display_name = "Local symbols";
    desc.file_extension = "txt";
    desc.build = build;
    return mrdocs_register_generator(env, &desc);
}
