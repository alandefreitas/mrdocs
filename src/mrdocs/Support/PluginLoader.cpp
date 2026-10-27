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

#include "PluginLoader.hpp"
#include "AddonRoots.hpp"
#include <mrdocs/Generator.hpp>
#include <mrdocs/Plugin.hpp>
#include <mrdocs/Support/Error/Expected.hpp>
#include <mrdocs/Support/Filesystem/Path.hpp>
#include <mrdocs/Support/Report.hpp>
#include <mrdocs/Transform.hpp>
#include <llvm/ADT/StringRef.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

namespace mrdocs {

namespace {

// Whether this MrDocs can load a plugin at all. One built without
// `MRDOCS_BUILD_PLUGIN_MODULES` (linked with -static, or built as shared
// libraries) reports a plugin it finds rather than handing it to the dynamic
// loader.
#ifdef MRDOCS_WITHOUT_PLUGINS
constexpr bool canLoadPlugins = false;
#else
constexpr bool canLoadPlugins = true;
#endif

// Whether `fileName` ends with `extension`. Windows and macOS ignore case
// in file names by default, so Stats.DLL names a library there.
bool
endsWithExtension(std::string_view fileName, std::string_view extension)
{
#if defined(_WIN32) || defined(__APPLE__)
    return llvm::StringRef(fileName).ends_with_insensitive(extension);
#else
    return fileName.ends_with(extension);
#endif
}

// Whether a file name is that of a loadable library. CMake gives a
// module library the .so extension on macOS, so a plugin there can
// carry either name.
bool
isLibraryName(std::string_view fileName)
{
#ifdef _WIN32
    constexpr std::string_view extensions[] = { ".dll" };
#elif defined(__APPLE__)
    constexpr std::string_view extensions[] = { ".dylib", ".so" };
#else
    constexpr std::string_view extensions[] = { ".so" };
#endif
    return std::ranges::any_of(
        extensions,
        [fileName](std::string_view extension)
        {
            return endsWithExtension(fileName, extension);
        });
}

// Return `utf8` as a path that names the file the UTF-8 text names. A path
// built from a std::string is read in the ANSI code page on Windows, which
// looks for another file when the text has a character the code page lacks,
// so the text is handed over as a std::u8string, which every platform reads
// as UTF-8.
std::filesystem::path
utf8Path(std::string_view const utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

// Return the libraries directly under `dir`, ordered by name.
std::vector<std::string>
scanPluginDir(std::string_view dir)
{
    std::vector<std::string> found;
    Expected<void> const scanned = forEachFile(dir, false,
        [&found](std::string_view path) -> Expected<void>
        {
            if (files::isRegularFile(path) &&
                isLibraryName(files::getFileName(path)))
            {
                found.emplace_back(path);
            }
            return {};
        });
    if (!scanned)
    {
        // Not a failure of the run: the directory may hold nothing MrDocs
        // wanted. Saying so beats leaving a plugin the user installed out
        // of the run without a word.
        report::warn(
            "The plugin directory \"{}\" could not be read: {}",
            dir, scanned.error().reason());
    }
    std::ranges::sort(found);
    return found;
}

// Append `from` to `to`, leaving out the libraries already there. One
// library can be reached through more than one addon root, and running its
// entry point twice would fail on the id it installed the first time.
// Identity comes from the filesystem, so a root spelled differently, or
// reached through a link, is still recognized.
void
appendNewLibraries(
    std::vector<std::string> const& from,
    std::vector<std::string>& to)
{
    for (std::string const& path : from)
    {
        bool const known = std::ranges::any_of(
            to,
            [&path](std::string const& other)
            {
                std::error_code ec;
                return std::filesystem::equivalent(
                    utf8Path(path), utf8Path(other), ec);
            });
        if (!known)
        {
            to.push_back(path);
        }
    }
}

// The context handed to a plugin: it reads the configuration MrDocs
// loaded, forwards a generator to the global registry, and collects the
// transforms for the `PluginRegistry` that `loadPlugins` returns.
class PluginContextImpl final
    : public PluginContext
{
    Config const& config_;
    std::vector<std::unique_ptr<Transform>>& transforms_;

public:
    PluginContextImpl(
        Config const& config,
        std::vector<std::unique_ptr<Transform>>& transforms) noexcept
        : config_(config)
        , transforms_(transforms)
    {
    }

    Config const&
    config() const noexcept override
    {
        return config_;
    }

    Expected<void>
    installGenerator(std::unique_ptr<Generator> G) override
    {
        return mrdocs::installGenerator(std::move(G));
    }

    Expected<void>
    installTransform(std::unique_ptr<Transform> T) override
    {
        MRDOCS_CHECK(T, "cannot install null transform");
        transforms_.push_back(std::move(T));
        return {};
    }
};

#ifdef _WIN32

// Convert the system's message for `code` to UTF-8.
std::string
systemMessage(DWORD const code)
{
    wchar_t* text = nullptr;
    DWORD const length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0,
        reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string message;
    if (length != 0 && text)
    {
        int const size = ::WideCharToMultiByte(
            CP_UTF8, 0, text, static_cast<int>(length),
            nullptr, 0, nullptr, nullptr);
        if (size > 0)
        {
            message.resize(static_cast<std::size_t>(size));
            ::WideCharToMultiByte(
                CP_UTF8, 0, text, static_cast<int>(length),
                message.data(), size, nullptr, nullptr);
        }
        while (!message.empty() &&
            (message.back() == '\n' || message.back() == '\r' ||
                message.back() == ' '))
        {
            message.pop_back();
        }
    }
    if (text)
    {
        ::LocalFree(text);
    }
    if (message.empty())
    {
        message = "error " + std::to_string(code);
    }
    return message;
}

#endif

// A loaded library. Libraries are opened for the life of the process and
// never closed: the generators and transforms a plugin installs live in
// registries that are destroyed after main returns, and their code has to
// still be mapped when that happens.
using LibraryHandle = void*;

// Open the library at `path`. Every symbol it needs is resolved now, so a
// plugin that refers to something MrDocs does not provide is refused here,
// instead of failing at its first call. The POSIX loader's message names the
// missing symbol; the Windows one only says a module or procedure was not
// found. Its symbols stay local to it, so two plugins cannot interpose each
// other's C++ symbols.
Expected<LibraryHandle>
openLibrary(std::string const& path)
{
#ifdef _WIN32
    std::error_code ec;
    std::filesystem::path const absolute =
        std::filesystem::absolute(utf8Path(path), ec);
    MRDOCS_CHECK(!ec, formatError(
        "the plugin \"{}\" could not be loaded: {}", path, ec.message()));
    // The libraries the plugin depends on live in the subdirectory named
    // after it, such as plugins/stats for plugins/stats.dll. That directory
    // joins the search for the duration of this load only, and the search
    // then covers the directory of the plugin, the added one and the system
    // directories, and not the current directory or PATH.
    std::filesystem::path const dependencies =
        absolute.parent_path() / absolute.stem();
    DLL_DIRECTORY_COOKIE cookie = nullptr;
    if (std::filesystem::is_directory(dependencies, ec))
    {
        cookie = ::AddDllDirectory(dependencies.c_str());
        if (!cookie)
        {
            report::warn(
                "The libraries directory of the plugin \"{}\" could not be "
                "added to the search path: {}",
                path, systemMessage(::GetLastError()));
        }
    }
    HMODULE const handle = ::LoadLibraryExW(
        absolute.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    DWORD const error = handle ? 0 : ::GetLastError();
    if (cookie)
    {
        ::RemoveDllDirectory(cookie);
    }
    MRDOCS_CHECK(handle, formatError(
        "the plugin \"{}\" could not be loaded: {}",
        path, systemMessage(error)));
    return reinterpret_cast<LibraryHandle>(handle);
#else
    // Clear any error left over from an earlier call, so the text read
    // below is this call's.
    ::dlerror();
    LibraryHandle const handle = ::dlopen(
        path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        char const* const text = ::dlerror();
        return Unexpected(formatError(
            "the plugin \"{}\" could not be loaded: {}",
            path, text ? text : "unknown error"));
    }
    return handle;
#endif
}

// Return the address of `symbol` in `library`, or null if it has none.
void*
findSymbol(LibraryHandle library, char const* symbol)
{
#ifdef _WIN32
    return reinterpret_cast<void*>(::GetProcAddress(
        static_cast<HMODULE>(library), symbol));
#else
    return ::dlsym(library, symbol);
#endif
}

// Look a plugin entry point up by name, as a function of type `Fn`.
template <class Fn>
Expected<Fn>
findEntryPoint(
    LibraryHandle library,
    char const* symbol,
    std::string_view path)
{
    void* const address = findSymbol(library, symbol);
    MRDOCS_CHECK(address, formatError(
        "the plugin \"{}\" does not export {}", path, symbol));
    return reinterpret_cast<Fn>(address);
}

// Compare the interface the plugin was built against with ours. A
// mismatch means the plugin's view of the context, or of the entry
// points themselves, is not the one it is about to be called with.
Expected<void>
checkApiVersion(
    LibraryHandle library,
    std::string_view path)
{
    MRDOCS_TRY(PluginApiVersionFn const apiVersion,
        findEntryPoint<PluginApiVersionFn>(
            library, "mrdocs_plugin_api_version", path));
    int const version = apiVersion();
    MRDOCS_CHECK(version == MRDOCS_PLUGIN_API_VERSION, formatError(
        "the plugin \"{}\" was built against version {} of the plugin "
        "interface, and this MrDocs provides version {}",
        path, version, MRDOCS_PLUGIN_API_VERSION));
    return {};
}

// Call the entry point and report what the plugin left behind, if it says
// it failed, naming the library.
Expected<void>
runEntryPoint(
    PluginMainFn entryPoint,
    std::string_view path,
    Config const& config,
    std::vector<std::unique_ptr<Transform>>& transforms)
{
    PluginContextImpl context(config, transforms);
    Error error;
    bool const installed = entryPoint(context, &error);
    MRDOCS_CHECK(installed, error.failed()
        ? formatError("the plugin \"{}\" failed: {}", path, error.reason())
        : formatError("the plugin \"{}\" reported a failure", path));
    return {};
}

// Compare the toolchain the plugin was built with against ours. The two
// pass C++ objects between them, so a difference in the compiler, the
// standard library, or how it lays its types out is not something either
// side can survive; refusing here is what keeps it from surfacing as a
// crash somewhere unrelated.
Expected<void>
checkBuildTag(
    LibraryHandle library,
    std::string_view path)
{
    MRDOCS_TRY(PluginBuildTagFn const buildTag,
        findEntryPoint<PluginBuildTagFn>(
            library, "mrdocs_plugin_build_tag", path));
    char const* const tag = buildTag();
    MRDOCS_CHECK(tag, formatError(
        "the plugin \"{}\" reports no toolchain", path));
    MRDOCS_CHECK(std::string_view(tag) == MRDOCS_PLUGIN_BUILD_TAG,
        formatError(
            "the plugin \"{}\" was built with \"{}\", and this MrDocs "
            "with \"{}\"; a plugin has to be built with the toolchain "
            "MrDocs was built with",
            path, tag, MRDOCS_PLUGIN_BUILD_TAG));
    return {};
}

// Load one library and run its entry point.
Expected<void>
loadPlugin(
    std::string const& path,
    Config const& config,
    std::vector<std::unique_ptr<Transform>>& transforms)
{
    MRDOCS_TRY(LibraryHandle const library, openLibrary(path));
    MRDOCS_TRY(checkApiVersion(library, path));
    MRDOCS_TRY(checkBuildTag(library, path));
    MRDOCS_TRY(PluginMainFn const entryPoint,
        findEntryPoint<PluginMainFn>(library, "mrdocs_plugin_main", path));
    MRDOCS_TRY(runEntryPoint(entryPoint, path, config, transforms));
    report::info("Loaded plugin \"{}\"", path);
    return {};
}

} // (anon)

std::vector<std::string>
discoverPlugins(std::vector<std::string> const& roots)
{
    std::vector<std::string> paths;
    for (std::string const& root : roots)
    {
        std::string const dir = files::appendPath(root, "plugins");
        if (files::exists(dir))
        {
            appendNewLibraries(scanPluginDir(dir), paths);
        }
    }
    return paths;
}

PluginRegistry::
PluginRegistry(
    std::vector<std::unique_ptr<Transform>> transforms) noexcept
    : transforms_(std::move(transforms))
{
}

Expected<void>
PluginRegistry::
applyTransforms(Corpus& corpus, Config const& config) const
{
    Expected<void> result;
    for (std::unique_ptr<Transform> const& T : transforms_)
    {
        if (result)
        {
            result = T->apply(corpus, config);
            if (!result)
            {
                result = Unexpected(formatError(
                    "the transform \"{}\" failed: {}",
                    T->id(), result.error().reason()));
            }
        }
    }
    return result;
}

Expected<PluginRegistry>
loadPlugins(Config const& config)
{
    std::vector<std::string> const paths =
        discoverPlugins(addonRoots(config));
    MRDOCS_CHECK(canLoadPlugins || paths.empty(), formatError(
        "the plugin \"{}\" cannot be loaded: this MrDocs was built without "
        "plugin support (linked with -static, or built as shared libraries)",
        paths.front()));
    std::vector<std::unique_ptr<Transform>> transforms;
    for (std::string const& path : paths)
    {
        MRDOCS_TRY(loadPlugin(path, config, transforms));
    }
    return PluginRegistry(std::move(transforms));
}

} // mrdocs
