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

// A MrDocs plugin: a shared library that MrDocs loads as it starts up. This
// one registers a generator, which counts the extracted symbols by kind, and
// a transform, which gives an undocumented symbol a placeholder brief. It
// uses the C++ wrapper of mrdocs/plugin.hpp.

#include <mrdocs/plugin.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using mrdocs::plugin::Array;
using mrdocs::plugin::Env;
using mrdocs::plugin::Kind;
using mrdocs::plugin::Value;

// tag::build[]
// Counts the symbols of the corpus by kind, and writes one line per kind.
class StatsGenerator : public mrdocs::plugin::Generator
{
public:
    StatsGenerator()
        : Generator("stats", "Symbol statistics", "txt")
    {
    }

    // An exception thrown here, or by a call to MrDocs, ends the run with
    // its message as the error.
    void
    build(Env env) override
    {
        // Ordered by kind name.
        std::map<std::string, int> counts;
        Array(env.corpus()["symbols"]).each([&](Value symbol)
        {
            ++counts[symbol["kind"].str()];
        });

        std::filesystem::path const dir = env.outputPath();
        std::filesystem::path const file = dir / "stats.txt";
        // Binary mode keeps the line endings the same on every platform.
        std::ofstream os(file, std::ios::binary);
        if (!os)
        {
            throw std::runtime_error(
                "could not open \"" + file.string() + "\" for writing");
        }
        for (auto const& [kind, count] : counts)
        {
            os << kind << ' ' << count << '\n';
        }
    }
};
// end::build[]

// tag::apply[]
// Give a symbol a brief if it has none. A symbol with no documentation at all
// has no `doc` object yet, so one is created first.
void
fillBrief(Env env, Value symbol)
{
    Value doc = symbol["doc"];
    if (doc.kind() != Kind::Object)
    {
        symbol.set("doc", env.object());
        doc = symbol["doc"];
    }
    if (doc["brief"].kind() == Kind::Object)
    {
        return;
    }

    // A brief is a block of inline nodes; here, a single run of text.
    Value text = env.object();
    text.set("kind", "text");
    text.set("literal", "Undocumented.");
    Value children = env.array();
    children.push(text);
    Value brief = env.object();
    brief.set("kind", "brief");
    brief.set("children", children);
    doc.set("brief", brief);
}

// Gives a placeholder brief to every symbol that has none, so that the gap
// shows up in the output instead of a blank space.
class BriefFiller : public mrdocs::plugin::Transform
{
public:
    BriefFiller()
        : Transform("brief-filler")
    {
    }

    void
    apply(Env env) override
    {
        // The handles made for one symbol are released before the next one.
        Array(env.corpus()["symbols"]).each([&](Value symbol)
        {
            fillBrief(env, symbol);
        });
    }
};
// end::apply[]

} // (anon)

// tag::init[]
MRDOCS_PLUGIN_INIT_CPP(env)
{
    env.addGenerator(std::make_unique<StatsGenerator>());
    env.addTransform(std::make_unique<BriefFiller>());
}
// end::init[]
