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

#ifndef MRDOCS_API_EXTENSIONS_EXTENSIONREGISTRY_HPP
#define MRDOCS_API_EXTENSIONS_EXTENSIONREGISTRY_HPP

#include <mrdocs/Platform.hpp>
#include <mrdocs/Dom.hpp>
#include <mrdocs/Generator.hpp>
#include <mrdocs/Transform.hpp>
#include <mrdocs/Support/Error/Error.hpp>
#include <mrdocs/Support/Error/Expected.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mrdocs {

class Corpus;
class Config;

/** The corpus transforms and extension-script generators of one run.

    Extensions are optional and live entirely outside the @ref Corpus: a
    corpus is just extracted symbols. An application that has no extensions
    never needs this type; it does whatever an extension would do in its
    own code. An application that wants to honor a configuration's
    extensions loads them here and runs them as steps in its own workflow.

    The registry owns one pipeline of @ref Transform objects, whatever
    their origin. A plugin adds its transforms with @ref addTransform (see
    @ref loadPlugins), and @ref loadScripts adds the ones the extension
    scripts register. @ref applyTransforms runs the pipeline in the order
    the transforms were added, so a caller that loads plugins first and
    scripts second gets plugin transforms before script transforms.

    A `.lua` or `.js` file under an addon root's `extensions/` directory is
    an extension. Running its top level (once, at @ref loadScripts)
    registers two kinds of things: corpus transforms
    (`mrdocs.register_transform`) and output generators
    (`mrdocs.register_generator`). This object owns the scripting engines
    and the registered functions, so it must outlive both
    @ref applyTransforms and any generator it hands out. Destroying the
    registry closes the engines, and a script may have kept the corpus it
    was handed, so a corpus given to @ref applyTransforms must outlive the
    registry: declare the corpus before the registry.

    Typical workflow, after a corpus is built:

    @code
    // The corpus is declared before the registry, so it is destroyed after
    Corpus corpus = ...;
    ExtensionRegistry ext;
    MRDOCS_TRY(loadPlugins(config, ext));
    MRDOCS_TRY(ext.loadScripts(config));
    MRDOCS_TRY(ext.applyTransforms(corpus, config));
    // then run generators: ext.findGenerator(id), else the built-in registry
    @endcode

    The call to @ref loadPlugins works in the `mrdocs` tool under its own
    name. In another program it needs the plugins to find the `mrdocs_*`
    functions there, which the documentation of @ref loadPlugins describes
    and which is not possible on Windows.

    An application that uses no plugins can write
    `ExtensionRegistry::load(config)` instead of the `ExtensionRegistry`,
    `loadPlugins` and `loadScripts` lines.
*/
class MRDOCS_VISIBLE
    ExtensionRegistry
{
public:
    /** Constructor.

        The registry starts with no transforms and no generators.
    */
    MRDOCS_DECL ExtensionRegistry();

    /** Destructor.
    */
    MRDOCS_DECL ~ExtensionRegistry();

    /** Move constructor.

        The registry owns the scripting engines and the generators loaded
        from them, so it is move-only.
    */
    MRDOCS_DECL ExtensionRegistry(ExtensionRegistry&&) noexcept;

    /** Move assignment.
    */
    MRDOCS_DECL ExtensionRegistry& operator=(ExtensionRegistry&&) noexcept;

    /** Create a registry holding the extension scripts of a configuration.

        Equivalent to a new registry followed by @ref loadScripts. It holds
        no plugin transforms; use the constructor and @ref loadPlugins when
        plugins take part.

        @param config The configuration whose addon roots are searched.
        @return The loaded registry, or an error if a script failed.
    */
    MRDOCS_DECL
    static
    Expected<ExtensionRegistry>
    load(Config const& config);

    /** Discover and load the extension scripts declared by a configuration.

        Walks each addon root's `extensions/` directory, in a stable order,
        and runs each script's top level once so it can register its
        transforms and generators. The transforms are appended to the
        pipeline, after whatever it already holds. The registry owns the
        engines and functions the scripts registered, so it must outlive
        any corpus the scripts can reach through a transform.

        @param config The configuration whose addon roots are searched.
        @return The error, if a script failed.
    */
    MRDOCS_DECL
    Expected<void>
    loadScripts(Config const& config);

    /** Append a transform to the pipeline.

        The transform runs after every transform added before it. Several
        transforms may share an id. A transform the pipeline already holds
        (the same object) is not added again, so adding it twice, for
        example by loading the same plugins twice into one registry, runs
        it once, at the position of the first addition.

        @param transform The transform to add. The registry shares ownership,
        so the same transform may be in the pipeline of several registries.
        @return An error if the transform is null.
    */
    MRDOCS_DECL
    Expected<void>
    addTransform(std::shared_ptr<Transform const> transform);

    /** Apply the pipeline of transforms to a corpus.

        Invokes every transform once, in the order it was added, handing it
        the `transform-options` block of its id as params, and stops at the
        first one that fails. Run this after the corpus is built and
        finalized and before any generator runs, so the mutations are
        visible to every output format. The corpus forgets its remembered
        name lookups afterwards, whether or not a transform failed. The
        corpus must outlive the registry, because a script may keep it.

        @param corpus The corpus to transform.
        @param config The configuration (supplies each transform's params).
        @return The error, if any occurred, naming the transform it came
        from.
    */
    MRDOCS_DECL
    Expected<void>
    applyTransforms(Corpus& corpus, Config const& config) const;

    /** Return the script-defined generators, as @ref Generator objects.

        @return A pointer to each registered generator; empty when the
        loaded scripts registered none.
    */
    MRDOCS_DECL
    std::vector<Generator const*>
    generators() const;

    /** Return the script-defined generator with this id, or `nullptr`.

        @param id The generator id to look up.
        @return The generator, or `nullptr` if none is registered.
    */
    MRDOCS_DECL
    Generator const*
    findGenerator(std::string_view id) const noexcept;

private:
    // One strong handle per loaded script to its engine (a type-erased
    // Context that keeps the VM alive). The transforms of a Lua script
    // and all the generators a script registered keep the engine alive
    // too. The transforms of a JavaScript script do not: their functions
    // point into the interpreter without owning it, so for those scripts
    // these handles are what keeps the engine running. They are declared
    // before transforms_ so the engine outlives the functions that use it.
    std::vector<std::shared_ptr<void>> engines_;
    std::vector<std::unique_ptr<Generator>> generators_;
    std::vector<std::shared_ptr<Transform const>> transforms_;
};

} // mrdocs

#endif // MRDOCS_API_EXTENSIONS_EXTENSIONREGISTRY_HPP
