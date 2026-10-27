//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Gennaro Prota (gennaro.prota@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// The interface a shared library implements to extend MrDocs from
// outside the tool.

#ifndef MRDOCS_API_PLUGIN_HPP
#define MRDOCS_API_PLUGIN_HPP

#include <mrdocs/Platform.hpp>
#include <mrdocs/Config.hpp>
#include <mrdocs/Generator.hpp>
#include <mrdocs/Support/Error/Error.hpp>
#include <mrdocs/Support/Error/Expected.hpp>
#include <mrdocs/Transform.hpp>
#include <mrdocs/Version.hpp>
#include <memory>
#include <vector>

#define MRDOCS_DETAIL_PLUGIN_STRINGIZE_I(x) #x
#define MRDOCS_DETAIL_PLUGIN_STRINGIZE(x) MRDOCS_DETAIL_PLUGIN_STRINGIZE_I(x)

#if defined(_DLL) && defined(_DEBUG)
#    define MRDOCS_DETAIL_PLUGIN_CRT_TAG "dynamic-debug"
#elif defined(_DLL)
#    define MRDOCS_DETAIL_PLUGIN_CRT_TAG "dynamic"
#elif defined(_DEBUG)
#    define MRDOCS_DETAIL_PLUGIN_CRT_TAG "static-debug"
#else
#    define MRDOCS_DETAIL_PLUGIN_CRT_TAG "static"
#endif

#if defined(_GLIBCXX_DEBUG)
#    define MRDOCS_DETAIL_PLUGIN_GLIBCXX_DEBUG_TAG "on"
#else
#    define MRDOCS_DETAIL_PLUGIN_GLIBCXX_DEBUG_TAG "off"
#endif

// The value is computed in a helper so that the documented macro below has
// a single #define.
#if defined(_MSVC_STL_VERSION)
#    define MRDOCS_DETAIL_PLUGIN_STDLIB_TAG_VALUE \
         "msvc-stl, crt " MRDOCS_DETAIL_PLUGIN_CRT_TAG                   \
         ", iterator-debug-level "                                       \
         MRDOCS_DETAIL_PLUGIN_STRINGIZE(_ITERATOR_DEBUG_LEVEL)
#elif defined(_LIBCPP_VERSION)
#    define MRDOCS_DETAIL_PLUGIN_STDLIB_TAG_VALUE \
         "libc++, abi " MRDOCS_DETAIL_PLUGIN_STRINGIZE(_LIBCPP_ABI_VERSION)
#elif defined(_GLIBCXX_RELEASE)
#    define MRDOCS_DETAIL_PLUGIN_STDLIB_TAG_VALUE \
         "libstdc++, cxx11-abi "                                         \
         MRDOCS_DETAIL_PLUGIN_STRINGIZE(_GLIBCXX_USE_CXX11_ABI)          \
         ", debug " MRDOCS_DETAIL_PLUGIN_GLIBCXX_DEBUG_TAG
#else
#    define MRDOCS_DETAIL_PLUGIN_STDLIB_TAG_VALUE \
         "unrecognized standard library"
#endif

/** The standard library a plugin was built with.

    A plugin and MrDocs pass C++ objects between them: the plugin
    allocates a generator that MrDocs destroys, and both inline the
    standard library types the API exposes. That holds together only when
    both use the same standard library, configured so that it lays those
    types out the same way. A plugin reports this through
    `mrdocs_plugin_stdlib_tag`, and MrDocs refuses one whose value
    differs from its own. The compiler is not part of the tag: a plugin
    built with Clang loads into a MrDocs built with GCC when both use
    libstdc++.

    The text names the library and the settings that matter for it, such
    as `libstdc++, cxx11-abi 1, debug off`, `libc++, abi 1`, or
    `msvc-stl, crt dynamic, iterator-debug-level 0`, where the runtime
    is `dynamic`, `dynamic-debug`, `static` or `static-debug`.

    Only the settings that change how the standard library's types are
    laid out, or which heap they allocate from, are part of the tag: the
    libstdc++ dual ABI and debug mode, the libc++ ABI version, and the
    MSVC iterator debug level and C runtime, release or debug. The
    release of the library and the compiler are left out. libstdc++ stays
    compatible across releases, and compilers that share a standard
    library, such as GCC and Clang on Linux, lay its types out the same
    way. Assertion and hardening settings change what the inline code
    checks rather than what it operates on. A static C runtime on both
    sides passes the check, yet gives each module a heap of its own, so
    MrDocs and its plugins have to use the DLL runtime.
*/
#define MRDOCS_PLUGIN_STDLIB_TAG MRDOCS_DETAIL_PLUGIN_STDLIB_TAG_VALUE

/** The identity of the MrDocs build a plugin is compiled against.

    A plugin sees MrDocs types by their layout, and that layout changes
    in most commits, so a plugin loads only into the exact build it was
    compiled against. A plugin reports this value through
    `mrdocs_plugin_build_id`, and MrDocs refuses one whose value differs
    from its own instead of letting it fail later, somewhere unrelated.

    The text is the one of `mrdocs::project_release_with_build`, such as
    `2026.10.26+662773e585c8`.
*/
#define MRDOCS_PLUGIN_BUILD_ID MRDOCS_PROJECT_RELEASE_WITH_BUILD

#if defined(_MSC_VER)
#    define MRDOCS_DETAIL_PLUGIN_EXPORT __declspec(dllexport)
#else
#    define MRDOCS_DETAIL_PLUGIN_EXPORT \
         __attribute__((__visibility__("default")))
#endif

/** The attribute that makes a plugin's entry points visible.

    MrDocs looks the entry points up by name in the loaded library, so
    they have to be exported from it. @ref MRDOCS_PLUGIN_MAIN applies it
    to the functions it defines, and a plugin that writes them out by
    hand applies it itself.
*/
#define MRDOCS_PLUGIN_EXPORT MRDOCS_DETAIL_PLUGIN_EXPORT

namespace mrdocs {

/** The interface a plugin uses to extend MrDocs.

    MrDocs creates a context and passes it to the plugin entry point,
    which installs through it whatever the plugin provides. The
    reference is valid for the duration of that call only.
*/
class MRDOCS_VISIBLE
    PluginContext
{
protected:
    /** Destructor.

        MrDocs owns the context, so a plugin never destroys it.
    */
    ~PluginContext() noexcept = default;

public:
    /** Return the configuration MrDocs is running with.

        The configuration is already loaded and normalized when the
        plugin is called, so a plugin can decide what to install from
        the settings the user wrote.
    */
    virtual
    Config const&
    config() const noexcept = 0;

    /** Install a generator.

        The generator becomes selectable under its own id, on the
        command line and in the configuration, alongside the ones that
        ship with MrDocs.

        @return An error if the generator is null, or if one with the
        same id is already installed.

        @param G The generator to install. Ownership is transferred to
        MrDocs, which keeps it for the rest of the run.
    */
    virtual
    Expected<void>
    installGenerator(std::unique_ptr<Generator> G) = 0;

    /** Install a corpus transform.

        Transforms a plugin installs run before the ones an extension
        script registers.

        @return An error if the transform is null.

        @param T The transform to install. Ownership is transferred to
        MrDocs, which keeps it for the rest of the run.
    */
    virtual
    Expected<void>
    installTransform(std::unique_ptr<Transform> T) = 0;
};

/** The type of the build identity function a plugin exports.

    The function is `extern "C"`, exported with @ref MRDOCS_PLUGIN_EXPORT,
    and named `mrdocs_plugin_build_id`. It returns
    @ref MRDOCS_PLUGIN_BUILD_ID as the plugin saw it, in storage that
    outlives the call.
*/
using PluginBuildIdFn = char const* (*)();

/** The type of the standard library function a plugin exports.

    The function is `extern "C"`, exported with @ref MRDOCS_PLUGIN_EXPORT,
    and named `mrdocs_plugin_stdlib_tag`. It returns
    @ref MRDOCS_PLUGIN_STDLIB_TAG as the plugin saw it, in storage that
    outlives the call.
*/
using PluginStdlibTagFn = char const* (*)();

/** The type of the entry point a plugin exports.

    The function is `extern "C"`, exported with @ref MRDOCS_PLUGIN_EXPORT,
    and named `mrdocs_plugin_main`. MrDocs calls it once, while it starts
    up, with a context and the address of an error to fill in. A plugin
    that returns `false` stops the run, reporting the error it left
    behind.

    A plugin defines the entry point with @ref MRDOCS_PLUGIN_MAIN rather
    than writing this signature out: the body of the macro returns an
    `Expected<void>`, which the macro turns into the two.
*/
using PluginMainFn = bool (*)(PluginContext&, Error*);

/** The corpus transforms the plugins installed.

    @ref loadPlugins returns one, holding what each plugin installed
    through @ref PluginContext::installTransform, and the caller applies
    it once the corpus is built. The generators a plugin installs go
    instead to the registry @ref findGenerator searches, next to the
    ones that ship with MrDocs.

    @code
    MRDOCS_TRY(PluginRegistry plugins, loadPlugins(config));
    MRDOCS_TRY(Corpus corpus, Corpus::build(config));
    MRDOCS_TRY(plugins.applyTransforms(corpus, config));
    @endcode
*/
class MRDOCS_VISIBLE
    PluginRegistry
{
public:
    /** Constructor.

        @param transforms The transforms to apply, in the order they
        were installed.
    */
    MRDOCS_DECL
    explicit
    PluginRegistry(
        std::vector<std::unique_ptr<Transform>> transforms) noexcept;

    /** Apply the transforms to a corpus.

        Invokes each transform once, in the order they were installed,
        and stops at the first one that fails.

        Call this after the corpus is built and finalized and before a
        generator runs.

        @return The error, if any occurred, naming the transform it came
        from.

        @param corpus The corpus to transform.
        @param config The configuration that drove the build.
    */
    MRDOCS_DECL
    Expected<void>
    applyTransforms(Corpus& corpus, Config const& config) const;

private:
    std::vector<std::unique_ptr<Transform>> transforms_;
};

/** Load the plugins the configuration makes visible.

    Each library directly under the plugins directory of an addon root
    is loaded for the lifetime of the process and its entry point is
    called once, so that what a plugin installs is in place before
    anything looks for it. The roots are taken in the order the
    configuration lists them, `addons` first and then each entry of
    `addons-supplemental`, and the libraries within a root by name. A
    library reachable through more than one root is loaded once.

    Call this before a generator is looked up by id with
    @ref findGenerator. It is one of the pieces the command-line tool
    composes to run its generate step; the order of that step lives in
    the tool.

    A library that cannot be loaded, does not export the entry points,
    was built against another build of MrDocs or with another standard
    library, or reports an error of its own fails the call: a
    plugin is there because the user put it there, so one that does
    nothing is not silently accepted.

    @par Thread Safety
    The generator registry this installs into is synchronized, so a
    concurrent @ref installGenerator is safe. Two concurrent calls to this
    function are not: both would run the same entry points, and the second
    installation of a generator id fails.

    @return The transforms the plugins installed, or the error, if
    any occurred.

    @param config The resolved configuration whose addon roots are
    walked, and which the plugins read.
*/
MRDOCS_DECL
Expected<PluginRegistry>
loadPlugins(Config const& config);

} // mrdocs

/** Define the entry point of a plugin.

    Name the @ref mrdocs::PluginContext parameter and write a body that
    returns what came of the work:

    @code
    MRDOCS_PLUGIN_MAIN(ctx)
    {
        return ctx.installGenerator(std::make_unique<MyGenerator>());
    }
    @endcode

    Along with the entry point, which passes the error the body returned
    back to MrDocs, the macro defines the functions reporting the
    MrDocs build and the standard library the plugin was built with, so
    that none of the three can disagree with each other.

    @param context The name the body uses for the context it receives.
*/
#define MRDOCS_PLUGIN_MAIN(context)                                 \
    static ::mrdocs::Expected<void>                                 \
    installMrdocsPlugin(::mrdocs::PluginContext&);                  \
                                                                    \
    extern "C" MRDOCS_PLUGIN_EXPORT char const*                     \
    mrdocs_plugin_build_id()                                        \
    {                                                               \
        return MRDOCS_PLUGIN_BUILD_ID;                              \
    }                                                               \
                                                                    \
    extern "C" MRDOCS_PLUGIN_EXPORT char const*                     \
    mrdocs_plugin_stdlib_tag()                                      \
    {                                                               \
        return MRDOCS_PLUGIN_STDLIB_TAG;                            \
    }                                                               \
                                                                    \
    extern "C" MRDOCS_PLUGIN_EXPORT bool                            \
    mrdocs_plugin_main(                                             \
        ::mrdocs::PluginContext& mrdocsContext,                     \
        ::mrdocs::Error* mrdocsError)                               \
    {                                                               \
        ::mrdocs::Expected<void> const mrdocsResult =               \
            installMrdocsPlugin(mrdocsContext);                     \
        if (!mrdocsResult)                                          \
        {                                                           \
            *mrdocsError = mrdocsResult.error();                    \
        }                                                           \
        return mrdocsResult.has_value();                            \
    }                                                               \
                                                                    \
    static ::mrdocs::Expected<void>                                 \
    installMrdocsPlugin(::mrdocs::PluginContext& context)

#endif // MRDOCS_API_PLUGIN_HPP
