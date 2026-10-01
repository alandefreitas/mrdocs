//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A MrDocs plugin that reports what the code marks as deprecated. It
// registers a generator, which lists every symbol that carries
// `[[deprecated]]` with its message and its location, and a transform, which
// uses the message as the brief of a deprecated symbol that has none. It uses
// the C++ wrapper of mrdocs/plugin.hpp.

#include <mrdocs/plugin.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

using mrdocs::plugin::Array;
using mrdocs::plugin::Env;
using mrdocs::plugin::Kind;
using mrdocs::plugin::Value;

// tag::find[]
// The message of the `[[deprecated]]` attribute of a symbol: nullopt for a
// symbol that is not deprecated, and an empty string for one that is
// deprecated without giving a reason.
std::optional<std::string>
deprecationMessage(Value symbol)
{
    std::optional<std::string> result;
    Array(symbol["attributes"]).each([&](Value attribute)
    {
        if (attribute["kind"].str() == "deprecated")
        {
            // A `[[deprecated]]` with no message may have no `message`.
            Value message = attribute["message"];
            result = message.isString() ? message.str() : std::string();
        }
    });
    return result;
}

// Whether the documentation shows the symbol: a symbol extracted as `regular`
// or `see-below` (which has a page that shows its type as "see below"). That
// leaves out a symbol that was extracted only because a documented one uses it
// (a `dependency`, such as a base class from another library) and a symbol the
// configuration hides as `implementation-defined` (such as one in a `detail`
// namespace). The copy of a base class member that a derived class lists is
// shown, and so it counts.
bool
isShown(Value symbol)
{
    std::string const extraction = symbol["extraction"].str();
    return extraction == "regular" || extraction == "see-below";
}

// Whether the report lists the symbol: a symbol the documentation shows, other
// than a copy of a base class member made for a derived class. The copy would
// point at the line of the derived class, and the member it was made from is
// reported where it is declared.
bool
isReported(Value symbol)
{
    return isShown(symbol) && !symbol.has("inheritedFrom");
}
// end::find[]

// tag::names[]
// A name made of identifiers, with the scopes in front of it that were written
// (`a::Key`): nullopt for a name that has anything else in it, such as a
// template-id.
std::optional<std::string>
plainName(Value name)
{
    if (name["kind"].str() != "identifier")
    {
        return std::nullopt;
    }
    std::string result = name["identifier"].str();
    Value prefix = name["prefix"];
    if (prefix.isObject())
    {
        std::optional<std::string> scope = plainName(prefix);
        if (!scope)
        {
            return std::nullopt;
        }
        result = *scope + "::" + result;
    }
    return result;
}

// One template argument as written: a constant by its value, and a type that
// is just a name, with its scopes if it has them, by that name. Any other type
// (a pointer, a reference, a template-id) is printed as "...".
std::string
templateArgument(Value arg)
{
    std::string const kind = arg["kind"].str();
    if (kind == "constant" && arg["value"].isString())
    {
        return arg["value"].str();
    }
    if (kind == "type")
    {
        Value type = arg["type"];
        if (type["kind"].str() == "named" &&
            !(type.has("isConst") && type["isConst"].boolean()) &&
            !(type.has("isVolatile") && type["isVolatile"].boolean()))
        {
            if (std::optional<std::string> name = plainName(type["name"]))
            {
                return *name;
            }
        }
    }
    return "...";
}

// The name of a symbol, followed by its template arguments when it is an
// explicit or partial specialization: `Box<int>` and not `Box`, which is the
// name of the primary template. A symbol with no `name` is "(anonymous)".
std::string
displayName(Value symbol)
{
    std::string result =
        symbol.has("name") ? symbol["name"].str() : "(anonymous)";
    Value info = symbol["template"];
    if (info.isObject() && info["args"].isArray() && info["args"].size() != 0)
    {
        Value args = info["args"];
        result += '<';
        bool first = true;
        Array(args).each([&](Value arg)
        {
            if (!first)
            {
                result += ", ";
            }
            first = false;
            result += templateArgument(arg);
        });
        result += '>';
    }
    return result;
}

// The name of a symbol with the names of the scopes around it, as written in
// code, with the template arguments of a specialization. The walk ends at the
// global namespace, the only symbol whose parent is not an id. An anonymous
// namespace is not a scope here: its members belong to the enclosing one.
std::string
qualifiedName(Env env, Value symbol)
{
    std::string result = displayName(symbol);
    Value current = symbol;
    while (true)
    {
        Value parent = current["parent"];
        if (!parent.isString())
        {
            break;
        }
        std::optional<Value> scope = env.find(parent.str());
        if (!scope || !(*scope)["parent"].isString())
        {
            break;
        }
        result = displayName(*scope) + "::" + result;
        current = *scope;
    }
    return result;
}

// Where to send the reader: the first declaration, which is the one a user of
// the header sees and usually the one that carries the attribute, or else the
// definition for a symbol that has no separate declaration. The path is empty
// and the line is zero for a symbol with no location, and so is the path of a
// file that is not under `source-root`: the DOM leaves out an empty string.
struct Location
{
    std::string path;
    std::int64_t line = 0;
};

Location
locationOf(Value where)
{
    Location result;
    if (where.has("sourcePath"))
    {
        result.path = where["sourcePath"].str();
    }
    if (where.has("lineNumber"))
    {
        result.line = where["lineNumber"].integer();
    }
    return result;
}

Location
location(Value symbol)
{
    Value loc = symbol["loc"];
    Value declarations = loc["loc"];
    if (declarations.isArray() && declarations.size() != 0)
    {
        return locationOf(declarations[0]);
    }
    Value definition = loc["defLoc"];
    if (definition.isObject())
    {
        return locationOf(definition);
    }
    return {};
}
// end::names[]

// tag::build[]
// One deprecated symbol of the report.
struct Entry
{
    std::string name;
    std::string kind;
    std::string path;
    std::int64_t line = 0;
    std::string message;
};

// Lists the deprecated symbols, one entry each, ordered by name.
class DeprecationsGenerator : public mrdocs::plugin::Generator
{
public:
    DeprecationsGenerator()
        : Generator("deprecations", "Deprecation report", "txt")
    {
    }

    void
    build(Env env) override
    {
        std::vector<Entry> entries;
        Array(env.corpus()["symbols"]).each([&](Value symbol)
        {
            // An overload set is deprecated when all its members are, and
            // the members are reported on their own.
            if (symbol["kind"].str() == "overloads" || !isReported(symbol))
            {
                return;
            }
            std::optional<std::string> message = deprecationMessage(symbol);
            if (!message)
            {
                return;
            }
            // Everything is copied into the entry: the handles are released
            // when this lambda returns.
            Location const where = location(symbol);
            entries.push_back({
                qualifiedName(env, symbol),
                symbol["kind"].str(),
                where.path,
                where.line,
                *message});
        });
        // The line is a number, so that line 9 comes before line 12.
        std::sort(entries.begin(), entries.end(),
            [](Entry const& a, Entry const& b)
            {
                return std::tie(a.name, a.path, a.line) <
                    std::tie(b.name, b.path, b.line);
            });

        std::filesystem::path const dir = env.outputPath();
        std::filesystem::path const file = dir / "deprecations.txt";
        // Binary mode keeps the line endings the same on every platform.
        std::ofstream os(file, std::ios::binary);
        if (!os)
        {
            throw std::runtime_error(
                "could not open \"" + file.string() + "\" for writing");
        }
        for (Entry const& entry : entries)
        {
            // A symbol with no known location has no " at " part.
            os << entry.name << " (" << entry.kind << ')';
            if (!entry.path.empty())
            {
                os << " at " << entry.path << ':' << entry.line;
            }
            os << '\n';
            if (entry.message.empty())
            {
                os << "    no message\n";
            }
            else
            {
                os << "    " << entry.message << '\n';
            }
        }
        // A failed write (a full disk, say) shows in the stream state only.
        os.flush();
        if (!os)
        {
            throw std::runtime_error(
                "could not write \"" + file.string() + "\"");
        }
    }
};
// end::build[]

// tag::apply[]
// A brief is a block of inline nodes; here, a single run of text.
Value
makeBrief(Env env, std::string const& text)
{
    Value run = env.object();
    run.set("kind", "text");
    run.set("literal", text);
    Value children = env.array();
    children.push(run);
    Value brief = env.object();
    brief.set("kind", "brief");
    brief.set("children", children);
    return brief;
}

// The text of the brief for an overload set whose members are all deprecated:
// their message when they share one, and a plain "Deprecated." when they do
// not. nullopt when a member is not deprecated, or the set has no members.
std::optional<std::string>
overloadsMessage(Env env, Value symbol)
{
    Value members = symbol["members"];
    if (!members.isArray() || members.size() == 0)
    {
        return std::nullopt;
    }
    std::optional<std::string> common;
    bool deprecated = true;
    bool same = true;
    Array(members).each([&](Value id)
    {
        std::optional<Value> member = env.find(id.str());
        std::optional<std::string> message;
        if (member)
        {
            message = deprecationMessage(*member);
        }
        if (!message)
        {
            deprecated = false;
        }
        else if (!common)
        {
            common = message;
        }
        else if (*common != *message)
        {
            same = false;
        }
    });
    if (!deprecated || !common)
    {
        return std::nullopt;
    }
    return same ? *common : std::string();
}

// Gives a deprecated symbol that has no brief the message of its attribute
// as one, so that the output says why the symbol is deprecated even where the
// author did not write it down.
class DeprecationBrief : public mrdocs::plugin::Transform
{
public:
    DeprecationBrief()
        : Transform("deprecation-brief")
    {
    }

    void
    apply(Env env) override
    {
        Array(env.corpus()["symbols"]).each([&](Value symbol)
        {
            // The copy of a base class member that a derived class lists
            // gets a brief like any other member, but a symbol the
            // documentation does not show does not.
            if (!isShown(symbol))
            {
                return;
            }

            // MrDocs makes the brief of an overload set from its members
            // before the transforms run, so the set needs its own: the
            // namespace page lists it in a row of its own.
            std::optional<std::string> message =
                symbol["kind"].str() == "overloads" ?
                    overloadsMessage(env, symbol) :
                    deprecationMessage(symbol);
            if (!message)
            {
                return;
            }

            // A symbol with no documentation at all has no `doc` object yet.
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
            doc.set("brief", makeBrief(env,
                message->empty() ? "Deprecated." : "Deprecated: " + *message));
        });
    }
};
// end::apply[]

} // (anon)

MRDOCS_PLUGIN_INIT_CPP(env)
{
    env.addGenerator(std::make_unique<DeprecationsGenerator>());
    env.addTransform(std::make_unique<DeprecationBrief>());
}
