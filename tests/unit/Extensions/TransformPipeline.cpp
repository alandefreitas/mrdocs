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
#include <mrdocs/Config.hpp>
#include <mrdocs/Config/ReferenceDirectories.hpp>
#include <mrdocs/Corpus.hpp>
#include <mrdocs/Metadata.hpp>
#include <mrdocs/Support/Error/Error.hpp>
#include <mrdocs/Support/Error/Expected.hpp>
#include <mrdocs/Support/Filesystem/Path.hpp>
#include <mrdocs/Support/Filesystem/Temp.hpp>
#include <mrdocs/Transform.hpp>
#include <test_suite/test_suite.hpp>
#include <fstream>
#include <functional>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mrdocs {

namespace {

// Write `content` verbatim to `path`. Pre-existing files are truncated.
void
writeFile(std::string_view path, std::string_view content)
{
    std::ofstream os(std::string{path}, std::ios::binary | std::ios::trunc);
    os.write(content.data(),
             static_cast<std::streamsize>(content.size()));
}

// The name of the symbol that `corpus` finds for `name`, or "none". Only the
// lookup of a corpus that is not const remembers its results, so that is the
// one used.
std::string
lookedUp(Corpus& corpus, std::string_view name)
{
    Expected<Symbol const&> const found =
        corpus.lookup(SymbolID::global, name);
    return found ? found->Name : std::string("none");
}

// A transform that runs `body` with the corpus and its params, and fails
// when `fails` says so. What `body` records is what a test checks.
class ProbeTransform final
    : public Transform
{
public:
    using Body = std::function<void(Corpus&, dom::Object const&)>;

private:
    std::string id_;
    Body body_;
    bool fails_;

public:
    ProbeTransform(std::string id, Body body, bool fails = false)
        : id_(std::move(id))
        , body_(std::move(body))
        , fails_(fails)
    {
    }

    std::string_view
    id() const noexcept override
    {
        return id_;
    }

    Expected<void>
    apply(
        Corpus& corpus,
        Config const&,
        dom::Object const& params) const override
    {
        body_(corpus, params);
        Expected<void> result;
        if (fails_)
        {
            result = Unexpected(Error("deliberate failure"));
        }
        return result;
    }
};

// A transform that appends its id to `log` when it runs, and fails when
// `fails` says so.
std::shared_ptr<Transform const>
loggingTransform(
    std::string id,
    std::vector<std::string>& log,
    bool fails = false)
{
    return std::make_shared<ProbeTransform>(
        id,
        [id, &log](Corpus&, dom::Object const&) { log.push_back(id); },
        fails);
}

// A transform that appends "<id>:<tag>:<Widget>" to `log`: its own id, the
// `tag` of its params, and the name the corpus gives the symbol that was
// declared as `Widget`, which an earlier transform may have renamed.
std::shared_ptr<Transform const>
reportingTransform(std::string id, std::vector<std::string>& log)
{
    return std::make_shared<ProbeTransform>(
        id,
        [id, &log](Corpus& corpus, dom::Object const& params)
        {
            std::string tag = "no-tag";
            if (params.exists("tag"))
            {
                tag = std::string(params.get("tag").getString());
            }
            std::string widget = "no-widget";
            for (Symbol const& symbol : corpus)
            {
                if (symbol.Name.starts_with("Widget") ||
                    symbol.Name.starts_with("Gadget"))
                {
                    widget = symbol.Name;
                }
            }
            log.push_back(id + ":" + tag + ":" + widget);
        });
}

// A transform that renames the symbol named `from` to `to`.
std::shared_ptr<Transform const>
renamingTransform(std::string from, std::string to)
{
    return std::make_shared<ProbeTransform>(
        "renamer",
        [from, to](Corpus& corpus, dom::Object const&)
        {
            Expected<Symbol const&> const found =
                corpus.lookup(SymbolID::global, from);
            if (found)
            {
                corpus.find(found->id)->Name = to;
            }
        });
}

} // (anon)

// The pipeline `ExtensionRegistry::applyTransforms` runs, over a small real
// corpus. The source pulls in no headers, so extraction needs no toolchain
// resource directory.
struct TransformPipelineTest
{
    std::optional<ScopedTempDirectory> src_;
    std::optional<ScopedTempDirectory> addons_;
    std::optional<Config> config_;

    // Build the corpus of the sample project. Tests that change symbols
    // build their own, so that nothing carries over between them.
    Expected<Corpus>
    buildCorpus() const
    {
        return Corpus::build(*config_);
    }

    void
    setup()
    {
        src_.emplace("mrdocs-pipeline-src");
        addons_.emplace("mrdocs-pipeline-addons");
        BOOST_TEST(*src_);
        BOOST_TEST(*addons_);
        std::string const srcDir(src_->path());
        std::string const addonsDir(addons_->path());
        writeFile(files::appendPath(srcDir, "input.cpp"),
            "struct Widget {};\n");

        // A script transform that appends the `suffix` of its own options
        // to the name of the Widget.
        std::string const extensions =
            files::appendPath(addonsDir, "extensions");
        BOOST_TEST(files::createDirectory(extensions).has_value());
        writeFile(files::appendPath(extensions, "suffixer.lua"),
            "mrdocs.register_transform(\"suffixer\", function(ctx)\n"
            "    for _, sym in ipairs(ctx.corpus.symbols) do\n"
            "        if sym.name == \"Widget\" then\n"
            "            sym.name = sym.name .. ctx.params.suffix\n"
            "        end\n"
            "    end\n"
            "end)\n");

        // A real config file with absolute paths, so no config-relative
        // placeholders need resolving. The addon root holds the script
        // above; extraction never reads it.
        std::string const configPath = files::appendPath(srcDir, "mrdocs.yml");
        writeFile(configPath,
            "source-root: " + srcDir + "\n"
            "addons: " + addonsDir + "\n"
            "input:\n  - " + srcDir + "\n");

        ReferenceDirectories dirs;
        dirs.cwd = srcDir;
        dirs.mrdocsRoot = srcDir;

        Config config;
        BOOST_TEST(Config::load_file(config, configPath, dirs).has_value());
        config_ = std::move(config);
    }

    void
    testAddOrder()
    {
        Expected<Corpus> corpus = buildCorpus();
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        std::vector<std::string> log;
        ExtensionRegistry registry;
        for (std::string_view id : { "first", "second", "third" })
        {
            BOOST_TEST(registry.addTransform(
                loggingTransform(std::string(id), log)).has_value());
        }
        BOOST_TEST(registry.applyTransforms(*corpus, *config_).has_value());
        BOOST_TEST((log == std::vector<std::string>{
            "first", "second", "third" }));
    }

    void
    testSameTransformIsAddedOnce()
    {
        Expected<Corpus> corpus = buildCorpus();
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        std::vector<std::string> log;
        std::shared_ptr<Transform const> const first =
            loggingTransform("first", log);
        ExtensionRegistry registry;
        BOOST_TEST(registry.addTransform(first).has_value());
        BOOST_TEST(registry.addTransform(loggingTransform("second", log))
            .has_value());
        BOOST_TEST(registry.addTransform(first).has_value());
        BOOST_TEST(!registry.addTransform(nullptr).has_value());
        BOOST_TEST(registry.applyTransforms(*corpus, *config_).has_value());
        BOOST_TEST((log == std::vector<std::string>{ "first", "second" }));
    }

    void
    testStopsAtFirstError()
    {
        Expected<Corpus> corpus = buildCorpus();
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        std::vector<std::string> log;
        ExtensionRegistry registry;
        BOOST_TEST(registry.addTransform(
            loggingTransform("first", log)).has_value());
        BOOST_TEST(registry.addTransform(
            loggingTransform("second", log, true)).has_value());
        BOOST_TEST(registry.addTransform(
            loggingTransform("third", log)).has_value());
        Expected<void> const result =
            registry.applyTransforms(*corpus, *config_);
        BOOST_TEST(!result.has_value());
        BOOST_TEST((log == std::vector<std::string>{ "first", "second" }));
        if (!result)
        {
            BOOST_TEST(result.error().reason() ==
                "the transform \"second\" failed: deliberate failure");
        }
    }

    // A transform added before the scripts, one registered by a script, and
    // one added after, run in that order, each with the options of its own
    // id. The script renames the Widget between the two native transforms,
    // so the second one sees the new name only if the script ran before it.
    void
    testScriptsAndNativeInLoadOrder()
    {
        Expected<Corpus> corpus = buildCorpus();
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        Config config = *config_;
        auto options = [](std::string_view key, std::string_view value)
        {
            dom::Object object;
            object.set(key, value);
            return object;
        };
        config.transformOptions["before"] = options("tag", "a");
        config.transformOptions["suffixer"] = options("suffix", "-s");
        config.transformOptions["after"] = options("tag", "b");

        std::vector<std::string> log;
        ExtensionRegistry registry;
        BOOST_TEST(registry.addTransform(
            reportingTransform("before", log)).has_value());
        BOOST_TEST(registry.loadScripts(config).has_value());
        BOOST_TEST(registry.addTransform(
            reportingTransform("after", log)).has_value());
        BOOST_TEST(registry.applyTransforms(*corpus, config).has_value());
        BOOST_TEST((log == std::vector<std::string>{
            "before:a:Widget", "after:b:Widget-s" }));
    }

    // Loading the scripts a second time adds nothing: the suffix is appended
    // once, not twice.
    void
    testScriptsAreLoadedOnce()
    {
        Expected<Corpus> corpus = buildCorpus();
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        Config config = *config_;
        dom::Object suffix;
        suffix.set("suffix", "-s");
        config.transformOptions["suffixer"] = suffix;
        dom::Object tag;
        tag.set("tag", "a");
        config.transformOptions["after"] = tag;

        std::vector<std::string> log;
        ExtensionRegistry registry;
        BOOST_TEST(registry.loadScripts(config).has_value());
        BOOST_TEST(registry.loadScripts(config).has_value());
        BOOST_TEST(registry.addTransform(
            reportingTransform("after", log)).has_value());
        BOOST_TEST(registry.applyTransforms(*corpus, config).has_value());
        BOOST_TEST((log == std::vector<std::string>{ "after:a:Widget-s" }));
    }

    // A transform without a block of options gets an empty object.
    void
    testMissingParamsAreEmpty()
    {
        Expected<Corpus> corpus = buildCorpus();
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        std::vector<std::string> log;
        ExtensionRegistry registry;
        BOOST_TEST(registry.addTransform(
            reportingTransform("alone", log)).has_value());
        BOOST_TEST(registry.applyTransforms(*corpus, *config_).has_value());
        BOOST_TEST((log == std::vector<std::string>{
            "alone:no-tag:Widget" }));
    }

    // A lookup by name is remembered, and a rename makes what was
    // remembered wrong. The pipeline forgets it after each transform, so
    // the next transform and the generators see the new name.
    void
    testLookupCacheSeesRename()
    {
        Expected<Corpus> corpus = buildCorpus();
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        BOOST_TEST(lookedUp(*corpus, "Widget") == "Widget");
        BOOST_TEST(lookedUp(*corpus, "Gadget") == "none");

        std::vector<std::string> seen;
        ExtensionRegistry registry;
        BOOST_TEST(registry.addTransform(
            renamingTransform("Widget", "Gadget")).has_value());
        BOOST_TEST(registry.addTransform(
            std::make_shared<ProbeTransform>(
                "observer",
                [&seen](Corpus& c, dom::Object const&)
                {
                    seen.push_back(lookedUp(c, "Widget"));
                    seen.push_back(lookedUp(c, "Gadget"));
                })).has_value());
        BOOST_TEST(registry.applyTransforms(*corpus, *config_).has_value());
        BOOST_TEST((seen == std::vector<std::string>{ "none", "Gadget" }));
        BOOST_TEST(lookedUp(*corpus, "Widget") == "none");
        BOOST_TEST(lookedUp(*corpus, "Gadget") == "Gadget");
    }

    // The lookups are forgotten when a transform fails as well.
    void
    testLookupCacheIsClearedOnFailure()
    {
        Expected<Corpus> corpus = buildCorpus();
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        BOOST_TEST(lookedUp(*corpus, "Gadget") == "none");
        ExtensionRegistry registry;
        BOOST_TEST(registry.addTransform(
            std::make_shared<ProbeTransform>(
                "renames-then-fails",
                [](Corpus& c, dom::Object const&)
                {
                    Expected<Symbol const&> const found =
                        c.lookup(SymbolID::global, "Widget");
                    if (found)
                    {
                        c.find(found->id)->Name = "Gadget";
                    }
                },
                true)).has_value());
        BOOST_TEST(!registry.applyTransforms(*corpus, *config_).has_value());
        BOOST_TEST(lookedUp(*corpus, "Gadget") == "Gadget");
    }

    void
    run()
    {
        setup();
        if (config_)
        {
            testAddOrder();
            testSameTransformIsAddedOnce();
            testStopsAtFirstError();
            testScriptsAndNativeInLoadOrder();
            testScriptsAreLoadedOnce();
            testMissingParamsAreEmpty();
            testLookupCacheSeesRename();
            testLookupCacheIsClearedOnFailure();
        }
    }
};

TEST_SUITE(
    TransformPipelineTest,
    "clang.mrdocs.Extensions.TransformPipeline");

} // mrdocs
