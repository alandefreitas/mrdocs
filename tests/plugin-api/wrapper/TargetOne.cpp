//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A plugin that targets ABI 1 explicitly. The wrapper has to compile when a
// plugin asks for the lowest ABI it needs, so every member of the wrapper that
// uses a function added after ABI 1 sits under the matching
// `MRDOCS_PLUGIN_ABI_TARGET` check, and the wrapper's own machinery only uses
// functions of ABI 1. Building this file is the test.

#define MRDOCS_PLUGIN_ABI_TARGET 1u
#include <mrdocs/plugin.hpp>
#include <string>

static_assert(MRDOCS_PLUGIN_ABI_TARGET == 1u);

namespace {

using namespace mrdocs::plugin;

[[maybe_unused]] std::size_t
countProperties(Value const& value)
{
    std::size_t n = 0;
    for (auto const& entry : Object(value))
    {
        static_cast<void>(entry);
        ++n;
    }
    return n;
}

} // (anon)

MRDOCS_PLUGIN_INIT_CPP(env)
{
    static_cast<void>(env);
}
