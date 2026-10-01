//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

#include "ExtensionContext.hpp"
#include <mrdocs/Config.hpp>
#include <mrdocs/Metadata/DomCorpus.hpp>
#include <mrdocs/Support/DescribedToDom.hpp>
#include <utility>

namespace mrdocs {

dom::Value
buildExtensionContext(
    dom::Value const& corpusDom, Config const& config, dom::Object const& params)
{
    dom::Object ctx;
    ctx.set("corpus", corpusDom);
    // The configuration is exposed as the reflection projection of its
    // typed schema (describedToDom), not a hand-materialized dom::Object.
    // Only described options exist here; unknown YAML keys are reported at
    // load time (see Config::load) rather than silently passed through.
    ctx.set("config", describedToDom(config));
    ctx.set("params", dom::Value(params));
    return dom::Value(std::move(ctx));
}

} // mrdocs
