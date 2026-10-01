/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* A plugin written in C11, which compiles the public header as C and defines
   the entry points with the macro the header offers. Building this file is
   the test, and nothing links or runs it: see CMakeLists.txt. */

#include <mrdocs/plugin.h>

static mrdocs_status MRDOCS_PLUGIN_CALL
build(mrdocs_env* env, void* data)
{
    char dir[256];
    size_t length = 0;
    (void)data;
    return mrdocs_get_output_dir(env, dir, sizeof dir, &length);
}

MRDOCS_PLUGIN_INIT(env)
{
    mrdocs_generator_desc desc = { .struct_size = sizeof(mrdocs_generator_desc) };
    desc.id = "c-header";
    desc.display_name = "C header check";
    desc.file_extension = "txt";
    desc.build = build;
    return mrdocs_register_generator(env, &desc);
}
