//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A program that loads plugins into one process three times: the `mrdocs`
// tool loads them once per run, so it cannot show what a second load does.
// It is linked the way the tool is, so that a plugin finds MrDocs in it.
//
// The plugin installs a generator, so its entry point failing to be skipped
// the second time shows as a failed load. Its transform appends an X to the
// name of the symbol Widget, so the name counts the runs of the transform:
// the same transform, shared by two registries, runs once for each, and one
// that was loaded into the same registry twice runs once.
//
// Usage: reload-host <addon-root>. The exit status is 0 when all is well.

#include <mrdocs/Config.hpp>
#include <mrdocs/Config/ReferenceDirectories.hpp>
#include <mrdocs/Corpus.hpp>
#include <mrdocs/Extensions/ExtensionRegistry.hpp>
#include <mrdocs/Generator.hpp>
#include <mrdocs/Plugin.hpp>
#include <mrdocs/Support/Error/Error.hpp>
#include <mrdocs/Support/Filesystem/Path.hpp>
#include <mrdocs/Support/Filesystem/Temp.hpp>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

using namespace mrdocs;

namespace {

void
writeFile(std::string_view path, std::string_view content)
{
    std::ofstream os(std::string{path}, std::ios::binary | std::ios::trunc);
    os.write(content.data(), static_cast<std::streamsize>(content.size()));
}

bool
check(bool ok, std::string_view what)
{
    if (!ok)
    {
        std::cerr << "FAILED: " << what << '\n';
    }
    return ok;
}

bool
check(Expected<void> const& result, std::string_view what)
{
    if (!result)
    {
        std::cerr << "FAILED: " << what << ": "
                  << result.error().reason() << '\n';
    }
    return result.has_value();
}

// The name of the symbol that was declared as Widget.
std::string
widgetName(Corpus const& corpus)
{
    std::string name = "none";
    for (Symbol const& symbol : corpus)
    {
        if (symbol.Name.starts_with("Widget"))
        {
            name = symbol.Name;
        }
    }
    return name;
}

} // (anon)

int
main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: reload-host <addon-root>\n";
        return 2;
    }
    ScopedTempDirectory src("mrdocs-reload-src");
    if (!src)
    {
        return 2;
    }
    std::string const srcDir(src.path());
    writeFile(files::appendPath(srcDir, "input.cpp"), "struct Widget {};\n");
    std::string const configPath = files::appendPath(srcDir, "mrdocs.yml");
    writeFile(configPath,
        "source-root: " + srcDir + "\n"
        "addons: " + srcDir + "\n"
        "addons-supplemental:\n  - " + std::string(argv[1]) + "\n"
        "input:\n  - " + srcDir + "\n");
    ReferenceDirectories dirs;
    dirs.cwd = srcDir;
    dirs.mrdocsRoot = srcDir;
    Config config;
    if (!check(Config::load_file(config, configPath, dirs), "config"))
    {
        return 1;
    }
    Expected<Corpus> built = Corpus::build(config);
    if (!built)
    {
        std::cerr << "FAILED: corpus: " << built.error().reason() << '\n';
        return 1;
    }
    Corpus corpus = std::move(*built);

    bool ok = true;
    {
        ExtensionRegistry first;
        ExtensionRegistry second;
        ok &= check(loadPlugins(config, first), "first load");
        ok &= check(loadPlugins(config, second), "load into a second registry");
        ok &= check(loadPlugins(config, first), "load into the first again");
        ok &= check(findGenerator("reload-test") != nullptr,
            "the generator of the plugin is installed");

        ok &= check(widgetName(corpus) == "Widget", "name before");
        ok &= check(first.applyTransforms(corpus, config), "first apply");
        ok &= check(widgetName(corpus) == "WidgetX",
            "the first registry ran the transform once");
        ok &= check(second.applyTransforms(corpus, config), "second apply");
        ok &= check(widgetName(corpus) == "WidgetXX",
            "the second registry got the transform too");
    }
    return ok ? 0 : 1;
}
