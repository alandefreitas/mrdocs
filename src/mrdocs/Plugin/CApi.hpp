//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

#ifndef MRDOCS_LIB_PLUGIN_CAPI_HPP
#define MRDOCS_LIB_PLUGIN_CAPI_HPP

// The host side of the plugin C API (include/mrdocs/plugin.h): the
// `mrdocs_*` functions, and the adapters that make what a plugin registers
// look like a Generator and a Transform to the rest of MrDocs.

#include <mrdocs/plugin.h>
#include <mrdocs/Config.hpp>
#include <mrdocs/Support/Error/Expected.hpp>
#include <algorithm>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace mrdocs {

/** The type of the `mrdocs_plugin_init` function a plugin exports.
*/
using PluginInitFn = mrdocs_status (MRDOCS_PLUGIN_CALL *)(mrdocs_env*);

/** Call a plugin's `mrdocs_plugin_init` and report how it went.

    The plugin registers its generators and transforms from inside the call.
    The function returns an error if the plugin returns a status other than
    `MRDOCS_STATUS_OK` or reports an error with `mrdocs_set_error`.

    @return The error, if any occurred, naming the plugin.

    @param path The path of the plugin library, for diagnostics.
    @param init The plugin's `mrdocs_plugin_init`.
    @param config The configuration the plugin reads.
*/
Expected<void>
initializePlugin(
    std::string_view path,
    PluginInitFn init,
    Config const& config);

/** Copy a descriptor a plugin passed into the host's layout of it.

    A descriptor starts with its own size, and a plugin built for an earlier
    ABI version passes the size of the layout it knew, which is shorter than
    the host's when a later version appended fields. Only the first
    `struct_size` bytes belong to the plugin: the copy starts zeroed, takes
    those bytes (at most the host's whole layout), and leaves every later
    field unset. The registration code reads the copy and never the
    plugin's descriptor, so a field added to a descriptor cannot be read
    past the end of an older plugin's.

    @tparam Desc A descriptor type whose first member is `size_t struct_size`.
    @param desc The plugin's descriptor, at least `struct_size` bytes long.
*/
template <class Desc>
Desc
copyDescriptor(Desc const& desc) noexcept
{
    static_assert(std::is_trivially_copyable_v<Desc>);
    Desc copy{};
    std::memcpy(&copy, &desc, (std::min)(desc.struct_size, sizeof(Desc)));
    return copy;
}

} // mrdocs

#endif // MRDOCS_LIB_PLUGIN_CAPI_HPP
