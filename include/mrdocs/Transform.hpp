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

// A pass over the corpus, run between extraction and generation.

#ifndef MRDOCS_API_TRANSFORM_HPP
#define MRDOCS_API_TRANSFORM_HPP

#include <mrdocs/Platform.hpp>
#include <mrdocs/Config.hpp>
#include <mrdocs/Corpus.hpp>
#include <mrdocs/Dom.hpp>
#include <mrdocs/Support/Error/Error.hpp>
#include <mrdocs/Support/Error/Expected.hpp>
#include <string_view>

namespace mrdocs {

/** Base class for corpus transforms.

    A transform runs once, after the corpus is built and finalized and
    before any generator runs, so whatever it changes is what every
    output format sees. It is handed the corpus itself rather than a
    copy, and may read it, change the symbols it finds, or both. What
    it cannot do is create a symbol or destroy one, since a corpus keeps
    its storage to itself.

    Transforms are collected by an @ref ExtensionRegistry, which runs
    them as one pipeline: the transforms a plugin registers and the ones
    an extension script registers go through the same list, in the order
    they were registered.
*/
class MRDOCS_VISIBLE
    Transform
{
public:
    /** Destructor.
    */
    MRDOCS_DECL
    virtual
    ~Transform() noexcept;

    /** Return the symbolic name of the transform.

        A diagnostic about a transform names it with this, so a
        recognizable name is worth choosing. The id also keys the
        `transform-options` block the transform receives as `params`, so
        transforms that share an id share their parameters. Unlike a
        generator id it does not choose which transform runs, and need not
        be unique.
    */
    MRDOCS_DECL
    virtual
    std::string_view
    id() const noexcept = 0;

    /** Transform the corpus.

        @par Thread Safety
        The transforms of one pipeline run one at a time, in the order they
        were registered. A transform held by several registries can run
        concurrently, for different corpora.

        @return The error, if any occurred. An error stops the run,
        before any generator is given the corpus.

        @param corpus The corpus to read and change.
        @param config The configuration that drove the build.
        @param params The transform's own options: the
        `transform-options.<id>` block of the configuration for the
        transform's @ref id, or an empty object when it has none.
    */
    MRDOCS_DECL
    virtual
    Expected<void>
    apply(
        Corpus& corpus,
        Config const& config,
        dom::Object const& params) const = 0;
};

} // mrdocs

#endif // MRDOCS_API_TRANSFORM_HPP
