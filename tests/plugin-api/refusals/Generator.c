/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* A plugin that registers a generator under the id the compiler definition
   MRDOCS_TEST_GENERATOR_ID gives. Built twice with the same id, or once with
   the id of a generator that ships with MrDocs, it is a plugin that asks for
   an id that is taken, and MrDocs has to refuse it. */

#include <mrdocs/plugin.h>

#ifndef MRDOCS_TEST_GENERATOR_ID
#    error "MRDOCS_TEST_GENERATOR_ID names the id of the generator"
#endif

static mrdocs_status MRDOCS_PLUGIN_CALL
build(mrdocs_env* env, void* data)
{
    (void)env;
    (void)data;
    return MRDOCS_STATUS_OK;
}

MRDOCS_PLUGIN_INIT(env)
{
    mrdocs_generator_desc desc = { .struct_size = sizeof(mrdocs_generator_desc) };
    desc.id = MRDOCS_TEST_GENERATOR_ID;
    desc.display_name = "Refusal test";
    desc.file_extension = "txt";
    desc.build = build;
    return mrdocs_register_generator(env, &desc);
}
