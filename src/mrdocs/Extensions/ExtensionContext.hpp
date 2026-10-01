//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

#ifndef MRDOCS_LIB_EXTENSIONS_EXTENSIONCONTEXT_HPP
#define MRDOCS_LIB_EXTENSIONS_EXTENSIONCONTEXT_HPP

#include <mrdocs/Dom.hpp>

namespace mrdocs {

class Corpus;
class Config;

/** Build the context object passed to a registered extension script.

    Every extension kind receives one object, so new capabilities can be
    added without changing its signature:

    - `ctx.corpus` -- the navigable corpus (see @ref buildCorpusDom) the
      script reads and, for a transform, mutates in place.
    - `ctx.config` -- the generation configuration.
    - `ctx.params` -- the script's own options block, keyed by the `id` it
      registered under; an empty object when unset.

    `corpus` and `config` are the same for every extension kind. `params`
    is whatever the caller resolved for the script: the transform pipeline
    passes the `transform-options.<id>` block of the transform.

    The corpus DOM is `O(symbols)` to build, so the caller builds it and
    passes it in as `corpusDom`; the context itself is cheap.
*/
dom::Value
buildExtensionContext(
    dom::Value const& corpusDom, Config const& config, dom::Object const& params);

} // mrdocs

#endif
