//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A plugin whose entry point, defined with `MRDOCS_PLUGIN_INIT_CPP`, throws.
// MrDocs has to refuse it with the message of the exception, and the run has
// to stop; the ctest entry checks the diagnostic.

#include <mrdocs/plugin.hpp>
#include <stdexcept>

MRDOCS_PLUGIN_INIT_CPP(env)
{
    static_cast<void>(env);
    throw std::runtime_error("the entry point gave up");
}
