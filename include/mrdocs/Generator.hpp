//
// This is a derivative work. originally part of the LLVM Project.
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2023 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// Generator classes for converting declaration
// information into documentation in a specified format.

#ifndef MRDOCS_API_GENERATOR_HPP
#define MRDOCS_API_GENERATOR_HPP

#include <mrdocs/Platform.hpp>
#include <mrdocs/Config.hpp>
#include <mrdocs/Corpus.hpp>
#include <mrdocs/Support/Error/Error.hpp>
#include <memory>
#include <string>
#include <string_view>


namespace mrdocs {

class ExtensionRegistry;

/** Base class for documentation generators.
*/
class MRDOCS_VISIBLE
    Generator
{
public:
    /** Destructor.
    */
    MRDOCS_DECL
    virtual
    ~Generator() noexcept;

    /** Return the symbolic name of the generator.

        This is a short, unique string which identifies
        the generator in command line options and in
        configuration files.
    */
    MRDOCS_DECL
    virtual
    std::string_view
    id() const noexcept = 0;

    /** Return the display name of the generator.
    */
    MRDOCS_DECL
    virtual
    std::string_view
    displayName() const noexcept = 0;

    /** Return the extension or tag of the generator.

        This should be in all lower case. Examples
        of tags are:

        @li "adoc" Asciidoctor
        @li "xml" XML
        @li "html" HTML

        The returned string should not include
        a leading period.
    */
    MRDOCS_DECL
    virtual
    std::string_view
    fileExtension() const noexcept = 0;

    /** Build the documentation for the corpus.

        The generator reads its configuration from the `config` passed in
        (a corpus does not own its configuration), resolves its own output
        location, and writes whatever files it needs.

        @par Thread Safety
        @li Different `corpus` object: may be called concurrently.
        @li Same `corpus` object: may not be called concurrently.

        @return The error, if any occurred.

        @param corpus The symbols to emit.
        @param config The configuration that drove the build.
    */
    MRDOCS_DECL
    virtual
    Expected<void>
    build(Corpus const& corpus, Config const& config) const = 0;
};

/** Install a custom generator.

    This function registers a generator with the global
    generator registry, making it available for use.

    A plugin registers its generators through the C API in
    `mrdocs/plugin.h`, which calls this function.

    @par Thread Safety
    This function is thread-safe and may be called
    concurrently from multiple threads.

    @return An error if a generator with the same id
    already exists.

    @param G The generator to install. Ownership is
    transferred to the registry.
*/
MRDOCS_DECL
Expected<void>
installGenerator(std::unique_ptr<Generator> G);

/** Find a generator by its id.

    @par Thread Safety
    This function is thread-safe and may be called
    concurrently from multiple threads.

    @return A pointer to the generator, or `nullptr`
    if no generator with the given id exists.

    @param id The symbolic name of the generator.
    The name must be an exact match, including case.
*/
MRDOCS_DECL
Generator const*
findGenerator(std::string_view id) noexcept;

/** Load the plugins the configuration makes visible.

    Each addon root contributes the libraries directly under its
    plugins subdirectory, in root order and then by name within a
    root, and one reachable through more than one root is taken once.
    Every one of them is loaded for the lifetime of the process
    and its `mrdocs_plugin_init` is called once, so that what a plugin
    registers is in place before anything looks for it.

    The generators a plugin registers go to the global generator registry.
    The transforms it registers are added to the pipeline of `registry`,
    in the order the plugins load and the plugin registers them. Load
    plugins before the extension scripts of the same registry, so that
    plugin transforms run first.

    A plugin initializes once per process, however many times this
    function is called. A plugin is identified by the canonical path of
    its library, so a root written another way, or reached through a link,
    still names the same plugin. The transforms a plugin registers belong
    to the process, next to its generators. A later call, for another run
    with another registry, does not run the entry point again: it adds the
    transforms the plugin registered the first time to the pipeline of its
    `registry`, which share the plugin's state with every other registry
    that holds them.

    Only the configuration of the first call reaches the entry point of a
    plugin. What the entry point registers must therefore not depend on
    the configuration: decisions that do belong in the callbacks, which
    receive the configuration of the run they execute in.

    Call this before a generator is looked up by id with
    @ref findGenerator. It is one of the pieces the command-line tool
    composes to run its generate step; the order of that step lives in
    the tool.

    The plugins bind to the `mrdocs_*` functions of the program that loads
    them. The `mrdocs` tool provides them. On Windows a plugin imports them
    from a module named `mrdocs.exe`, so a program that embeds MrDocs or a
    renamed tool cannot load any plugin there: the load fails because that
    module is not found. On the other platforms a program that embeds
    MrDocs has to export the `mrdocs_*` functions from its executable, or
    the plugin is refused for a symbol that nothing provides.

    A library that cannot be loaded, does not export the entry points,
    targets a newer ABI than this MrDocs provides, or reports an error
    of its own fails the call: a plugin is there because the user put
    it there, so one that does nothing is not silently accepted. A
    plugin whose entry point failed may have registered part of what it
    meant to, and MrDocs does not undo that, so the only sensible action
    after such a failure is to exit.

    The call fails once @ref releasePlugins has been called in the
    process.

    @par Thread Safety
    Not thread-safe, like the rest of the setup of a run. No other call
    to this function, to @ref installGenerator or to @ref findGenerator
    may run meanwhile, and nothing else may use `registry`.

    @return The error, if any occurred.

    @param config The resolved configuration whose addon roots are
    walked. The plugins read it in their entry point only on the first
    call that loads them.
    @param registry The registry that receives the transforms the plugins
    register. It may be destroyed at any time: the transforms are shared
    with the process.
*/
MRDOCS_DECL
Expected<void>
loadPlugins(Config const& config, ExtensionRegistry& registry);

/** Give back to the plugins everything they handed to MrDocs.

    Calls the `release` function of every generator and transform a
    plugin registered, once, with the `data` it registered it with, and
    marks the process as released: a later @ref loadPlugins fails. This
    is for process shutdown. The generators stay registered and the
    transforms stay in the pipelines that hold them, but none of them may
    run afterwards.

    The command-line tool calls this before it returns from `main`, so
    that `release` runs while the static objects of every plugin are
    still alive. A program that loads plugins and does not call it gets
    the calls during static destruction, by which time the static
    objects of a plugin loaded after MrDocs first registered something
    may be gone.

    @par Thread Safety
    Not thread-safe. No generator or transform of a plugin may be
    running, and no other call to @ref loadPlugins may be in progress.
*/
MRDOCS_DECL
void
releasePlugins() noexcept;

/** Handlebars-based generators and the pieces that support them.

    The `hbs` namespace groups the operations tied to MrDocs's Handlebars
    output path, including the discovery of data-driven generators an addon
    contributes as manifest directories.
*/
namespace hbs {

/** Discover addon-defined data-driven generators and install them.

    For each configured addon root, the immediate subdirectories of
    `<root>/generator/` that ship an `mrdocs-generator.yml` manifest are
    installed into the global registry as data-driven Handlebars
    generators (see the manifest format documentation). A built-in
    generator of the same id takes precedence, so its addon directory is
    skipped. Directories that hold only shared assets and declare no
    manifest are skipped too.

    Call this once, after the configuration is resolved and before a
    generator is looked up by id with @ref findGenerator. It is one of the
    pieces the command-line tool composes to run its generate step; the
    order of that step lives in the tool.

    @par Thread Safety
    Installs into the process-global registry, so it may not be called
    concurrently with @ref installGenerator.

    @return The error, if any occurred.

    @param config The resolved configuration whose addon roots are walked.
*/
MRDOCS_DECL
Expected<void>
discoverDataDrivenGenerators(Config const& config);

} // hbs

} // mrdocs


#endif // MRDOCS_API_GENERATOR_HPP
