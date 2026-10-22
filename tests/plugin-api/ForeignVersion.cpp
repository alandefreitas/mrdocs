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

// A plugin built for an ABI newer than this MrDocs provides, which is what a
// plugin made for a later MrDocs looks like. Loading it has to fail, and the
// run has to stop before anything is extracted; the ctest entry checks that
// by the diagnostic.
//
// The library exports the version function only, written out rather than
// defined with `MRDOCS_PLUGIN_INIT`, since that macro reports the ABI the
// header it is expanded from targets, which is by construction one the host
// accepts. It has no initialization function, as a later ABI may rename it:
// the loader has to check the version before it requires that function, so
// the diagnostic is the one about the newer MrDocs and not about a missing
// entry point.

#include <mrdocs/plugin.h>

extern "C" MRDOCS_PLUGIN_EXPORT uint32_t
mrdocs_plugin_abi_version(void)
{
    return MRDOCS_PLUGIN_ABI_VERSION + 1;
}
