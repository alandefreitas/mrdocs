/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* A library with a function of its own and neither entry point, which is what
   a dependency of a plugin looks like. A library directly in a plugins
   directory is a plugin, so MrDocs has to refuse it by name. */

#ifdef _WIN32
#    define MRDOCS_TEST_EXPORT __declspec(dllexport)
#else
#    define MRDOCS_TEST_EXPORT __attribute__((__visibility__("default")))
#endif

MRDOCS_TEST_EXPORT int
mrdocs_test_helper_function(void);

MRDOCS_TEST_EXPORT int
mrdocs_test_helper_function(void)
{
    return 0;
}
