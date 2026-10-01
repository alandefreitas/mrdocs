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

#ifndef MRDOCS_LIB_SUPPORT_PLUGINLOADER_HPP
#define MRDOCS_LIB_SUPPORT_PLUGINLOADER_HPP

// `loadPlugins` is part of the public API; see mrdocs/Generator.hpp.
// What lives here is the path logic behind it.

#include <string>
#include <string_view>
#include <vector>

namespace mrdocs {

/** Return the paths of the plugin libraries in the addon roots, in
    load order.

    Each root contributes the files directly under its plugins
    subdirectory whose name ends with the extension the platform uses
    for a loadable library. Anything else in the directory, and a root
    without one, is skipped.

    Roots are searched in order, and the libraries within a root are
    ordered by name, so a set of plugins always loads the same way. A
    library reachable through more than one root is reported once.

    @return The paths of the libraries to load.

    @param roots The addon root directories to search.
*/
std::vector<std::string>
discoverPlugins(std::vector<std::string> const& roots);

/** Return the sentence to append to the message of a failed library load.

    The platform loader resolves every symbol while it opens a library, which
    is before the library can report the ABI it needs. A plugin that calls a
    function of the plugin API that this MrDocs lacks is therefore refused for
    an undefined symbol, and not by the version check. When the message names
    such a function, this is the hint that the plugin may need a newer
    MrDocs. POSIX loaders name the symbol; the Windows one does not, so no
    hint comes out of its messages.

    @return The sentence, starting with a separator, or an empty view.

    @param message The message the loader reported.
*/
std::string_view
missingApiHint(std::string_view message);

} // mrdocs

#endif // MRDOCS_LIB_SUPPORT_PLUGINLOADER_HPP
