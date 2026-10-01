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
// uses the C API of mrdocs/plugin.h directly.

#include <mrdocs/plugin.h>
#include <filesystem>
#include <fstream>
#include <exception>
#include <map>
#include <string>
#include <vector>

namespace {

// tag::helpers[]
// Return from the enclosing function if a call to MrDocs did not succeed.
#define TRY(call)                                    \
    do                                               \
    {                                                \
        mrdocs_status const status_ = (call);        \
        if (status_ != MRDOCS_STATUS_OK)             \
        {                                            \
            return status_;                          \
        }                                            \
    } while (false)

// Read a string value. The first call asks for the length, the second one
// copies the text.
mrdocs_status
getString(mrdocs_env* env, mrdocs_value value, std::string& text)
{
    std::size_t length = 0;
    TRY(mrdocs_get_string_utf8(env, value, nullptr, 0, &length));
    text.resize(length + 1);
    TRY(mrdocs_get_string_utf8(env, value, text.data(), text.size(), &length));
    text.resize(length);
    return MRDOCS_STATUS_OK;
}
// end::helpers[]

// tag::build[]
// Count the symbols of the corpus by kind, ordered by kind name.
mrdocs_status
countByKind(mrdocs_env* env, std::map<std::string, int>& counts)
{
    mrdocs_value corpus;
    mrdocs_value symbols;
    std::size_t size = 0;
    TRY(mrdocs_get_corpus(env, &corpus));
    TRY(mrdocs_object_get(env, corpus, "symbols", &symbols));
    TRY(mrdocs_array_length(env, symbols, &size));
    for (std::size_t i = 0; i < size; ++i)
    {
        // The handles of one symbol are released before the next one.
        mrdocs_scope scope;
        TRY(mrdocs_scope_open(env, &scope));
        mrdocs_value symbol;
        mrdocs_value kind;
        std::string name;
        TRY(mrdocs_array_get(env, symbols, i, &symbol));
        TRY(mrdocs_object_get(env, symbol, "kind", &kind));
        TRY(getString(env, kind, name));
        ++counts[name];
        TRY(mrdocs_scope_close(env, scope));
    }
    return MRDOCS_STATUS_OK;
}

// Write one line per symbol kind, indicating the kind and how many symbols of
// that kind the corpus has.
mrdocs_status
writeStats(mrdocs_env* env)
{
    std::map<std::string, int> counts;
    TRY(countByKind(env, counts));

    std::string dir(1024, '\0');
    std::size_t length = 0;
    TRY(mrdocs_get_output_dir(env, dir.data(), dir.size(), &length));
    if (length >= dir.size())
    {
        dir.resize(length + 1);
        TRY(mrdocs_get_output_dir(env, dir.data(), dir.size(), &length));
    }
    dir.resize(length);

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path const file = std::filesystem::path(dir) / "stats.txt";
    // Binary mode keeps the line endings the same on every platform.
    std::ofstream os(file, std::ios::binary);
    if (!os)
    {
        std::string const message =
            "could not open \"" + file.string() + "\" for writing";
        mrdocs_set_error(env, message.c_str());
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    for (auto const& [kind, count] : counts)
    {
        os << kind << ' ' << count << '\n';
    }
    return MRDOCS_STATUS_OK;
}

// A callback never lets an exception leave: MrDocs and the plugin may not
// share a runtime that could unwind through the frames between them. The
// exception becomes the error of the call instead.
mrdocs_status
buildStats(mrdocs_env* env, void*)
{
    try
    {
        return writeStats(env);
    }
    catch (std::exception const& e)
    {
        mrdocs_set_error(env, e.what());
    }
    catch (...)
    {
        mrdocs_set_error(env, "unexpected exception");
    }
    return MRDOCS_STATUS_PLUGIN_ERROR;
}
// end::build[]

// tag::apply[]
// Give a symbol a brief if it has none. A symbol with no documentation at all
// has no `doc` object yet, so one is created first.
mrdocs_status
fillBrief(mrdocs_env* env, mrdocs_value symbol)
{
    mrdocs_value doc;
    mrdocs_value_kind kind;
    TRY(mrdocs_object_get(env, symbol, "doc", &doc));
    TRY(mrdocs_get_kind(env, doc, &kind));
    if (kind != MRDOCS_VALUE_OBJECT)
    {
        mrdocs_value empty;
        TRY(mrdocs_create_object(env, &empty));
        TRY(mrdocs_object_set(env, symbol, "doc", empty));
        TRY(mrdocs_object_get(env, symbol, "doc", &doc));
    }

    mrdocs_value brief;
    TRY(mrdocs_object_get(env, doc, "brief", &brief));
    TRY(mrdocs_get_kind(env, brief, &kind));
    if (kind == MRDOCS_VALUE_OBJECT)
    {
        return MRDOCS_STATUS_OK;
    }

    // A brief is a block of inline nodes; here, a single run of text.
    mrdocs_value text;
    mrdocs_value literal;
    mrdocs_value children;
    mrdocs_value type;
    mrdocs_value node;
    TRY(mrdocs_create_object(env, &text));
    TRY(mrdocs_create_string(env, "text", MRDOCS_AUTO_LENGTH, &type));
    TRY(mrdocs_create_string(env, "Undocumented.", MRDOCS_AUTO_LENGTH, &literal));
    TRY(mrdocs_object_set(env, text, "kind", type));
    TRY(mrdocs_object_set(env, text, "literal", literal));
    TRY(mrdocs_create_array(env, &children));
    TRY(mrdocs_array_push(env, children, text));
    TRY(mrdocs_create_object(env, &node));
    TRY(mrdocs_create_string(env, "brief", MRDOCS_AUTO_LENGTH, &type));
    TRY(mrdocs_object_set(env, node, "kind", type));
    TRY(mrdocs_object_set(env, node, "children", children));
    return mrdocs_object_set(env, doc, "brief", node);
}

// Give a placeholder brief to every symbol that has none, so that the gap
// shows up in the output instead of a blank space.
mrdocs_status
fillBriefs(mrdocs_env* env)
{
    mrdocs_value corpus;
    mrdocs_value symbols;
    std::size_t size = 0;
    TRY(mrdocs_get_corpus(env, &corpus));
    TRY(mrdocs_object_get(env, corpus, "symbols", &symbols));
    TRY(mrdocs_array_length(env, symbols, &size));
    for (std::size_t i = 0; i < size; ++i)
    {
        mrdocs_scope scope;
        mrdocs_value symbol;
        TRY(mrdocs_scope_open(env, &scope));
        TRY(mrdocs_array_get(env, symbols, i, &symbol));
        TRY(fillBrief(env, symbol));
        TRY(mrdocs_scope_close(env, scope));
    }
    return MRDOCS_STATUS_OK;
}

// As in `buildStats`, no exception leaves the callback.
mrdocs_status
applyBriefFiller(mrdocs_env* env, void*)
{
    try
    {
        return fillBriefs(env);
    }
    catch (std::exception const& e)
    {
        mrdocs_set_error(env, e.what());
    }
    catch (...)
    {
        mrdocs_set_error(env, "unexpected exception");
    }
    return MRDOCS_STATUS_PLUGIN_ERROR;
}
// end::apply[]

} // (anon)

// tag::init[]
MRDOCS_PLUGIN_INIT(env)
{
    mrdocs_generator_desc generator = {};
    generator.struct_size = sizeof(generator);
    generator.id = "stats";
    generator.display_name = "Symbol statistics";
    generator.file_extension = "txt";
    generator.build = buildStats;
    TRY(mrdocs_register_generator(env, &generator));

    mrdocs_transform_desc transform = {};
    transform.struct_size = sizeof(transform);
    transform.id = "brief-filler";
    transform.apply = applyBriefFiller;
    return mrdocs_register_transform(env, &transform);
}
// end::init[]
