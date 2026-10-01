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

#include <mrdocs/Extensions/ExtensionRegistry.hpp>
#include "AddonDiscovery.hpp"
#include "ExtensionContext.hpp"
#include "JsBinding.hpp"
#include "LoadedExtensions.hpp"
#include "LuaBinding.hpp"
#include <mrdocs/Corpus.hpp>
#include <mrdocs/Support/Error/Error.hpp>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mrdocs {

ExtensionRegistry::ExtensionRegistry() = default;
ExtensionRegistry::~ExtensionRegistry() = default;
ExtensionRegistry::ExtensionRegistry(ExtensionRegistry&&) noexcept = default;

// The members are released in the reverse of their declaration order, as the
// destructor does: the transforms and generators that call into a script
// engine go before the engines. A defaulted assignment would replace the
// engines first.
ExtensionRegistry&
ExtensionRegistry::operator=(ExtensionRegistry&& other) noexcept
{
    if (this != &other)
    {
        transforms_.clear();
        generators_.clear();
        engines_.clear();
        engines_ = std::move(other.engines_);
        generators_ = std::move(other.generators_);
        transforms_ = std::move(other.transforms_);
        other.engines_.clear();
        other.generators_.clear();
        other.transforms_.clear();
    }
    return *this;
}

namespace {

// A transform an extension script registered: the script's function, run
// with the `ctx` object the scripting API documents. The corpus DOM is
// O(symbols) to build, so a pipeline run builds it once and hands it to
// every script transform; it is a set of proxies over the live corpus, not
// a copy of it.
class ScriptTransform final
    : public Transform
{
    std::string id_;
    dom::Function function_;

public:
    ScriptTransform(std::string id, dom::Function function)
        : id_(std::move(id))
        , function_(std::move(function))
    {
    }

    std::string_view
    id() const noexcept override
    {
        return id_;
    }

    // A transform sees `ctx.corpus` (per-symbol proxies plus `get(id)` /
    // `lookup(name)`), `ctx.config`, and its own `ctx.params`. Writes
    // (`ctx.corpus.symbols[i].name = "..."`) mutate the live Symbol through
    // the DOM's set path. The corpus object is built for each call: it is a
    // plain DOM object that the script may write to, and its symbol list is
    // a snapshot, so sharing one between transforms would let a script
    // change what the next one sees, or leave it a list that an earlier
    // transform has made out of date.
    Expected<void>
    apply(
        Corpus& corpus,
        Config const& config,
        dom::Object const& params) const override
    {
        dom::Value const ctx = buildExtensionContext(
            buildCorpusDom(corpus), config, params);
        Expected<dom::Value> const invoked = function_.try_invoke(ctx);
        if (!invoked.has_value())
        {
            return Unexpected(Error(invoked.error()));
        }
        return {};
    }
};

Expected<LoadedExtensions>
loadExtensionsFromScript(std::string const& scriptPath, Config const& config)
{
    if (scriptPath.ends_with(".lua"))
    {
        return loadLuaExtensions(scriptPath, config);
    }
    if (scriptPath.ends_with(".js"))
    {
        return loadJsExtensions(scriptPath, config);
    }
    // collectExtensionScripts only emits .lua / .js paths, so reaching
    // here would mean an internal mismatch.
    return Unexpected(formatError(
        "extension '{}': unsupported file extension", scriptPath));
}

} // (anon)

Expected<ExtensionRegistry>
ExtensionRegistry::load(Config const& config)
{
    ExtensionRegistry registry;
    MRDOCS_TRY(registry.loadScripts(config));
    return registry;
}

Expected<void>
ExtensionRegistry::loadScripts(Config const& config)
{
    MRDOCS_TRY(std::vector<std::string> scripts,
        collectExtensionScripts(config));

    for (std::string const& path : scripts)
    {
        MRDOCS_TRY(LoadedExtensions loaded,
            loadExtensionsFromScript(path, config));
        engines_.push_back(std::move(loaded.vm));
        for (auto& [id, function] : loaded.transforms)
        {
            transforms_.push_back(std::make_unique<ScriptTransform>(
                std::move(id), std::move(function)));
        }
        for (std::unique_ptr<Generator>& g : loaded.generators)
        {
            generators_.push_back(std::move(g));
        }
    }
    return {};
}

Expected<void>
ExtensionRegistry::addTransform(std::shared_ptr<Transform const> transform)
{
    MRDOCS_CHECK(transform, "cannot add a null transform");
    if (std::ranges::find(transforms_, transform) != transforms_.end())
    {
        return {};
    }
    transforms_.push_back(std::move(transform));
    return {};
}

Expected<void>
ExtensionRegistry::applyTransforms(Corpus& corpus, Config const& config) const
{
    // Invoke each transform once, in the order it was added, stopping at
    // the first failure. A transform may have renamed symbols, so the
    // corpus drops its remembered lookups after each one, and the next
    // transform resolves names against what the previous ones wrote.
    Expected<void> result;
    for (std::shared_ptr<Transform const> const& transform : transforms_)
    {
        std::string const id(transform->id());
        auto const options = config.transformOptions.find(id);
        dom::Object const params = options != config.transformOptions.end()
            ? options->second
            : dom::Object();
        result = transform->apply(corpus, config, params);
        corpus.invalidateLookupCache();
        if (!result)
        {
            result = Unexpected(formatError(
                "the transform \"{}\" failed: {}",
                id, result.error().reason()));
            break;
        }
    }
    return result;
}

std::vector<Generator const*>
ExtensionRegistry::generators() const
{
    std::vector<Generator const*> result;
    result.reserve(generators_.size());
    for (std::unique_ptr<Generator> const& g : generators_)
    {
        result.push_back(g.get());
    }
    return result;
}

Generator const*
ExtensionRegistry::findGenerator(std::string_view id) const noexcept
{
    for (std::unique_ptr<Generator> const& g : generators_)
    {
        if (g->id() == id)
        {
            return g.get();
        }
    }
    return nullptr;
}

} // mrdocs
