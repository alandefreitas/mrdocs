//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Gennaro Prota (gennaro.prota@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// Call the public API from outside the tool, the way a plugin does, so that
// building this file checks that the API is exported. A plugin compiles its
// own copy of every inline and template member, and those copies call
// out-of-line functions that need an export attribute of their own; a
// missing one is an unresolved external here, and nothing else in the tree
// would notice. Sorting the members is what earns the traversal its place:
// it compares every symbol kind, so it reaches far more of the API than it
// names.
//
// The library is linked and then loaded by the tool, in the test
// mrdocs-plugin-api-link-test-loads. It has no entry point, so the load ends
// in a refusal that names the missing build id, and that refusal is reached
// only if every symbol the library names resolved. On Windows linking
// resolves them against the tool's export table, and on macOS against the
// tool the library is linked with as its bundle loader. On Linux a module
// library may leave symbols to the loader, so linking proves little there;
// the load does the check, because the loader opens the library with
// RTLD_NOW and a symbol the version script does not export fails that open
// with an undefined-symbol message.
//
// `NDEBUG` is undefined first, so that the inline code calls `assert_failed`
// the way it does in a plugin built in Debug, against a Release MrDocs.

#undef NDEBUG

#include <mrdocs/Corpus.hpp>
#include <mrdocs/Dom.hpp>
#include <mrdocs/Handlebars/Engine.hpp>
#include <mrdocs/Extensions/ExtensionRegistry.hpp>
#include <mrdocs/Generator.hpp>
#include <mrdocs/Support/Report.hpp>
#include <mrdocs/Metadata.hpp>
#include <mrdocs/Metadata/DomCorpus.hpp>
#include <mrdocs/Plugin.hpp>
#include <mrdocs/Transform.hpp>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <utility>

using namespace mrdocs;

std::size_t
probeTraversal(Corpus const& corpus, NamespaceSymbol const& I)
{
    Corpus::TraverseOptions opts;
    opts.ordered = true;
    opts.recursive = true;
    std::size_t count = corpus.size();
    corpus.traverse(opts, I, [&count](auto const&) { ++count; });
    for ([[maybe_unused]] Symbol const& J : corpus)
    {
        ++count;
    }
    return count;
}

std::string
probeNames(Corpus const& corpus, SymbolID const& id)
{
    Symbol const& I = corpus.get(id);
    return corpus.qualifiedName(I) + corpus.qualifiedName(I, id);
}

Expected<Symbol const&>
probeLookup(Corpus const& corpus)
{
    return corpus.lookup("x");
}

namespace {

class ProbeTransform final
    : public Transform
{
public:
    std::string_view
    id() const noexcept override
    {
        return "probe";
    }

    Expected<void>
    apply(
        Corpus& corpus,
        Config const&,
        dom::Object const&) const override
    {
        Expected<void> result;
        if (corpus.find(SymbolID::global) == nullptr)
        {
            result = Unexpected(formatError("the global namespace is missing"));
        }
        return result;
    }
};

} // (anon)

Expected<void>
probeInstallTransform(PluginContext& context)
{
    return context.installTransform(std::make_unique<ProbeTransform>());
}

// A plugin has no use for a registry of its own, but the type is public,
// and its implicit members are compiled here rather than in the tool.
Expected<void>
probeRegistry(Corpus& corpus, Config const& config)
{
    ExtensionRegistry registry;
    MRDOCS_TRY(registry.addTransform(std::make_shared<ProbeTransform>()));
    ExtensionRegistry moved(std::move(registry));
    registry = std::move(moved);
    return registry.applyTransforms(corpus, config);
}

// The DOM and the template engine, which a plugin that builds a report uses
// the way a generator does, and the error types every function returns.
std::string
probeDomAndTemplates(Corpus const& corpus)
{
    dom::Object object;
    object.set("name", dom::Value("probe"));
    dom::Array array;
    array.push_back(dom::Value(1));
    object.set("items", dom::Value(array));
    dom::Value value(object);
    std::string result = toString(value) + toString(object.get("name"));
    handlebars::Handlebars engine;
    result += engine.render("{{name}}", value);
    // Operators and conversions the DOM defines inline over exported
    // functions, the way a template or a helper in a plugin calls them.
    dom::Value one(1);
    if (one == dom::Value(1) || (value || "x"))
    {
        result += toString(array) + toString(dom::Value(dom::String("s")));
    }
    result += std::format("{}", value);
    result += toString(buildCorpusDom(corpus));
    SymbolID id = SymbolID::createFromString("0000000000000000000000000000000000000000");
    result += id == SymbolID::global ? "global" : "other";
    Expected<std::string> failed = Unexpected(Error("probe"));
    if (!failed)
    {
        result += failed.error().message();
    }
    return result;
}

namespace {

class ProbeGenerator final
    : public Generator
{
public:
    std::string_view
    id() const noexcept override
    {
        return "probe";
    }

    std::string_view
    displayName() const noexcept override
    {
        return "Probe";
    }

    std::string_view
    fileExtension() const noexcept override
    {
        return "txt";
    }

    Expected<void>
    build(Corpus const& corpus, Config const& config) const override
    {
        DomCorpus domCorpus(corpus);
        report::print(report::Level::info, config.configDir());
        return {};
    }
};

} // (anon)

Expected<void>
probeInstallGenerator(PluginContext& context)
{
    return context.installGenerator(std::make_unique<ProbeGenerator>());
}
