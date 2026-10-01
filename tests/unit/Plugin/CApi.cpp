//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

#include <mrdocs/Plugin/CApi.hpp>
#include <mrdocs/plugin.h>
#include <mrdocs/Config.hpp>
#include <mrdocs/Config/ReferenceDirectories.hpp>
#include <mrdocs/Corpus.hpp>
#include <mrdocs/Extensions/ExtensionRegistry.hpp>
#include <mrdocs/Generator.hpp>
#include <mrdocs/Support/Filesystem/Path.hpp>
#include <mrdocs/Support/Filesystem/Temp.hpp>
#include <mrdocs/Support/Report.hpp>
#include <mrdocs/Transform.hpp>
#include <test_suite/test_suite.hpp>
#include <filesystem>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mrdocs {

namespace {

// The plugins in this file are plain C functions, since that is what a
// plugin is. What they observe is left in these variables for the test that
// ran them to check.
struct Observed
{
    int releases = 0;
    int referencesDeleted = 0;
    std::vector<std::string> names;
    std::string outputDir;
    std::int64_t answer = 0;
    bool readOnlyRefused = false;
    bool foundWidget = false;
    bool missingRefused = false;
    bool lookedUpWidget = false;
    bool sourceRootStored = false;
    std::string sourceRoot;
    // What the transform that runs after the rename found by name.
    bool lookupFound = false;
    std::string lookupName;
    bool lookupStale = false;
    bool lookupContext = false;
    bool lookupArguments = false;
};

Observed observed;

// Read a string value into a std::string.
std::string
text(mrdocs_env* env, mrdocs_value value)
{
    size_t length = 0;
    if (mrdocs_get_string_utf8(env, value, nullptr, 0, &length) !=
        MRDOCS_STATUS_OK)
    {
        return "<not a string>";
    }
    std::string result(length + 1, '\0');
    mrdocs_get_string_utf8(env, value, result.data(), result.size(), &length);
    result.resize(length);
    return result;
}

mrdocs_value_kind
kindOf(mrdocs_env* env, mrdocs_value value)
{
    mrdocs_value_kind kind = MRDOCS_VALUE_UNDEFINED;
    mrdocs_get_kind(env, value, &kind);
    return kind;
}

bool
visitCounting(mrdocs_env*, char const*, mrdocs_value, void* data)
{
    ++*static_cast<int*>(data);
    return true;
}

bool
visitOnce(mrdocs_env*, char const*, mrdocs_value, void* data)
{
    ++*static_cast<int*>(data);
    return false;
}

bool
visitSumming(mrdocs_env* env, char const* key, mrdocs_value value, void* data)
{
    int64_t number = 0;
    if (std::string_view(key) != "skip" &&
        mrdocs_get_int64(env, value, &number) == MRDOCS_STATUS_OK)
    {
        *static_cast<int64_t*>(data) += number;
    }
    return true;
}

// Add a key for every property seen, to the object being visited.
bool
visitAdding(mrdocs_env* env, char const* key, mrdocs_value value, void* data)
{
    mrdocs_value const object = *static_cast<mrdocs_value*>(data);
    std::string const added = std::string(key) + "-copy";
    mrdocs_object_set(env, object, added.c_str(), value);
    return true;
}

// Change a later property of a symbol from inside the first callback of a
// visit of it, and note what the callback for `name` receives.
struct VisitWrite
{
    mrdocs_value symbol = nullptr;
    bool wrote = false;
    std::string name;
};

bool
visitWriting(mrdocs_env* env, char const* key, mrdocs_value value, void* data)
{
    auto& state = *static_cast<VisitWrite*>(data);
    if (!state.wrote)
    {
        state.wrote = true;
        mrdocs_value changed = nullptr;
        mrdocs_create_string(env, "Visited", MRDOCS_AUTO_LENGTH, &changed);
        mrdocs_object_set(env, state.symbol, "name", changed);
    }
    if (std::string_view(key) == "name")
    {
        state.name = text(env, value);
    }
    return true;
}

// Close a scope that was opened before the visit, from inside the callback.
bool
visitClosing(mrdocs_env* env, char const*, mrdocs_value, void* data)
{
    mrdocs_scope_close(env, *static_cast<mrdocs_scope*>(data));
    return true;
}

// Open a scope and return without closing it.
bool
visitLeaking(mrdocs_env* env, char const*, mrdocs_value, void*)
{
    mrdocs_scope scope = 0;
    mrdocs_scope_open(env, &scope);
    return true;
}

// Count the `$meta` properties that accept a write.
bool
visitWritingMeta(mrdocs_env* env, char const* key, mrdocs_value value, void* data)
{
    if (std::string_view(key) == "$meta")
    {
        mrdocs_value number = nullptr;
        mrdocs_create_int64(env, 1, &number);
        if (mrdocs_object_set(env, value, "k", number) == MRDOCS_STATUS_OK)
        {
            ++*static_cast<int*>(data);
        }
    }
    return true;
}

// Visit an object the plugin made, and from the callback for `a` change its
// `b`, and write to a symbol too if asked. The value of `b` the callback
// receives is what it was when the visit started in either case.
struct VisitOwn
{
    mrdocs_value own = nullptr;
    mrdocs_value symbol = nullptr;
    bool touchSymbol = false;
    std::int64_t seenB = -1;
};

bool
visitOwn(mrdocs_env* env, char const* key, mrdocs_value value, void* data)
{
    auto& state = *static_cast<VisitOwn*>(data);
    if (std::string_view(key) == "a")
    {
        mrdocs_value changed = nullptr;
        mrdocs_create_int64(env, 99, &changed);
        mrdocs_object_set(env, state.own, "b", changed);
        if (state.touchSymbol)
        {
            mrdocs_value name = nullptr;
            mrdocs_create_string(env, "Gadget", MRDOCS_AUTO_LENGTH, &name);
            mrdocs_object_set(env, state.symbol, "name", name);
        }
    }
    if (std::string_view(key) == "b")
    {
        mrdocs_get_int64(env, value, &state.seenB);
    }
    return true;
}

// Build the brief of a symbol out of values the plugin made. The brief holds
// `depth` nested `strong` nodes, or a single `strong` node that is its own
// child when `cyclic`. The values are shared handles, so a node that holds
// itself keeps itself alive: when `cyclic`, `loop` receives that node and the
// caller breaks the cycle with breakLoop once the test is done with it.
mrdocs_value
makeBrief(
    mrdocs_env* env,
    int const depth,
    bool const cyclic,
    mrdocs_value* const loop = nullptr)
{
    auto const node = [&]
    {
        mrdocs_value result = nullptr;
        mrdocs_value kind = nullptr;
        mrdocs_create_object(env, &result);
        mrdocs_create_string(env, "strong", MRDOCS_AUTO_LENGTH, &kind);
        mrdocs_object_set(env, result, "kind", kind);
        return result;
    };
    auto const holding = [&](mrdocs_value child)
    {
        mrdocs_value children = nullptr;
        mrdocs_create_array(env, &children);
        mrdocs_array_push(env, children, child);
        return children;
    };
    mrdocs_value inner = node();
    if (cyclic)
    {
        mrdocs_object_set(env, inner, "children", holding(inner));
        if (loop)
        {
            *loop = inner;
        }
    }
    for (int i = 0; i < depth; ++i)
    {
        mrdocs_value outer = node();
        mrdocs_object_set(env, outer, "children", holding(inner));
        inner = outer;
    }
    mrdocs_value brief = nullptr;
    mrdocs_value kind = nullptr;
    mrdocs_create_object(env, &brief);
    mrdocs_create_string(env, "brief", MRDOCS_AUTO_LENGTH, &kind);
    mrdocs_object_set(env, brief, "kind", kind);
    mrdocs_object_set(env, brief, "children", holding(inner));
    return brief;
}

// Empty the children of `node`, which ends the cycle makeBrief made through it.
void
breakLoop(mrdocs_env* env, mrdocs_value node)
{
    mrdocs_value none = nullptr;
    mrdocs_create_array(env, &none);
    mrdocs_object_set(env, node, "children", none);
}

// Create, read and change every kind of value a plugin can make.
mrdocs_status
valuesInit(mrdocs_env* env)
{
    mrdocs_value value = nullptr;

    BOOST_TEST(mrdocs_create_bool(env, true, &value) == MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, value) == MRDOCS_VALUE_BOOLEAN);
    bool flag = false;
    BOOST_TEST(mrdocs_get_bool(env, value, &flag) == MRDOCS_STATUS_OK);
    BOOST_TEST(flag);
    int64_t number = 0;
    BOOST_TEST(
        mrdocs_get_int64(env, value, &number) == MRDOCS_STATUS_TYPE_MISMATCH);

    BOOST_TEST(mrdocs_create_int64(env, 42, &value) == MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, value) == MRDOCS_VALUE_INTEGER);
    BOOST_TEST(mrdocs_get_int64(env, value, &number) == MRDOCS_STATUS_OK);
    BOOST_TEST(number == 42);
    BOOST_TEST(mrdocs_get_bool(env, value, &flag) ==
        MRDOCS_STATUS_TYPE_MISMATCH);

    BOOST_TEST(mrdocs_create_null(env, &value) == MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, value) == MRDOCS_VALUE_NULL);

    // A string reports its full length and is cut to fit the buffer.
    BOOST_TEST(mrdocs_create_string(
        env, "hello", MRDOCS_AUTO_LENGTH, &value) == MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, value) == MRDOCS_VALUE_STRING);
    BOOST_TEST(text(env, value) == "hello");
    size_t length = 0;
    char small[4] = {};
    BOOST_TEST(mrdocs_get_string_utf8(
        env, value, small, sizeof small, &length) == MRDOCS_STATUS_OK);
    BOOST_TEST(length == 5);
    BOOST_TEST(std::string_view(small) == "hel");
    BOOST_TEST(mrdocs_get_string_utf8(
        env, value, nullptr, 3, &length) == MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_create_string(env, "abcdef", 3, &value) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(text(env, value) == "abc");
    BOOST_TEST(mrdocs_get_string_utf8(
        env, value, nullptr, 0, nullptr) == MRDOCS_STATUS_OK);

    // An object keeps what is set on it, and an absent property is
    // undefined rather than an error.
    mrdocs_value object = nullptr;
    mrdocs_value one = nullptr;
    mrdocs_value two = nullptr;
    mrdocs_value found = nullptr;
    BOOST_TEST(mrdocs_create_object(env, &object) == MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, object) == MRDOCS_VALUE_OBJECT);
    BOOST_TEST(mrdocs_create_int64(env, 1, &one) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_int64(env, 2, &two) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_has(env, object, "a", &flag) == MRDOCS_STATUS_OK);
    BOOST_TEST(!flag);
    BOOST_TEST(mrdocs_object_get(env, object, "a", &found) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, found) == MRDOCS_VALUE_UNDEFINED);
    BOOST_TEST(mrdocs_object_set(env, object, "a", one) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_set(env, object, "skip", value) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_set(env, object, "b", two) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_has(env, object, "a", &flag) == MRDOCS_STATUS_OK);
    BOOST_TEST(flag);
    BOOST_TEST(mrdocs_object_get(env, object, "b", &found) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_get_int64(env, found, &number) == MRDOCS_STATUS_OK);
    BOOST_TEST(number == 2);
    BOOST_TEST(mrdocs_object_get(env, one, "a", &found) ==
        MRDOCS_STATUS_TYPE_MISMATCH);

    int visited = 0;
    BOOST_TEST(mrdocs_object_visit(env, object, visitCounting, &visited) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(visited == 3);
    visited = 0;
    BOOST_TEST(mrdocs_object_visit(env, object, visitOnce, &visited) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(visited == 1);
    int64_t sum = 0;
    BOOST_TEST(mrdocs_object_visit(env, object, visitSumming, &sum) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(sum == 3);

    // An array grows by pushing, and an index past its end is refused.
    mrdocs_value array = nullptr;
    size_t size = 0;
    BOOST_TEST(mrdocs_create_array(env, &array) == MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, array) == MRDOCS_VALUE_ARRAY);
    BOOST_TEST(mrdocs_array_push(env, array, one) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_array_push(env, array, two) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_array_length(env, array, &size) == MRDOCS_STATUS_OK);
    BOOST_TEST(size == 2);
    BOOST_TEST(mrdocs_array_get(env, array, 1, &found) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_get_int64(env, found, &number) == MRDOCS_STATUS_OK);
    BOOST_TEST(number == 2);
    BOOST_TEST(mrdocs_array_get(env, array, 2, &found) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_array_length(env, object, &size) ==
        MRDOCS_STATUS_TYPE_MISMATCH);

    // Closing a scope releases the handles made inside it.
    mrdocs_scope scope = 0;
    mrdocs_value inside = nullptr;
    BOOST_TEST(mrdocs_scope_open(env, &scope) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_int64(env, 7, &inside) == MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, inside) == MRDOCS_VALUE_INTEGER);
    BOOST_TEST(mrdocs_scope_close(env, scope) == MRDOCS_STATUS_OK);
    mrdocs_value_kind kind = MRDOCS_VALUE_NULL;
    BOOST_TEST(mrdocs_get_kind(env, inside, &kind) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_get_kind(env, object, &kind) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_scope_close(env, 1000000) == MRDOCS_STATUS_INVALID_ARG);

    // A scope closed twice releases nothing the second time, and handles made
    // after the first close stay valid.
    mrdocs_value outside = nullptr;
    BOOST_TEST(mrdocs_scope_open(env, &scope) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_int64(env, 5, &inside) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_scope_close(env, scope) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_int64(env, 6, &outside) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_scope_close(env, scope) == MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(kindOf(env, outside) == MRDOCS_VALUE_INTEGER);

    // An outer scope cannot be closed before the inner one.
    mrdocs_value early = nullptr;
    mrdocs_value late = nullptr;
    mrdocs_scope outer = 0;
    mrdocs_scope nested = 0;
    BOOST_TEST(mrdocs_scope_open(env, &outer) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_int64(env, 7, &early) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_scope_open(env, &nested) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_int64(env, 8, &late) == MRDOCS_STATUS_OK);
    BOOST_TEST(outer != nested);
    BOOST_TEST(mrdocs_scope_close(env, outer) == MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(kindOf(env, early) == MRDOCS_VALUE_INTEGER);
    BOOST_TEST(kindOf(env, late) == MRDOCS_VALUE_INTEGER);
    BOOST_TEST(mrdocs_scope_close(env, nested) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_scope_close(env, outer) == MRDOCS_STATUS_OK);

    // A handle that was released stays invalid after newer handles take its
    // place.
    mrdocs_value first = nullptr;
    mrdocs_value second = nullptr;
    BOOST_TEST(mrdocs_scope_open(env, &scope) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_int64(env, 11, &first) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_scope_close(env, scope) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_int64(env, 12, &second) == MRDOCS_STATUS_OK);
    BOOST_TEST(first != second);
    BOOST_TEST(mrdocs_get_int64(env, first, &number) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_get_int64(env, second, &number) == MRDOCS_STATUS_OK);
    BOOST_TEST(number == 12);

    // A callback may change the object it is visiting, and the visit still
    // sees the properties the object had when it began.
    int seen = 0;
    BOOST_TEST(mrdocs_object_visit(env, object, visitAdding, &object) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_has(env, object, "a-copy", &flag) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(flag);
    BOOST_TEST(mrdocs_object_visit(env, object, visitCounting, &seen) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(seen == 6);

    // A callback that closes a scope opened before the visit does not
    // corrupt the table.
    BOOST_TEST(mrdocs_scope_open(env, &scope) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_int64(env, 13, &first) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_visit(env, object, visitClosing, &scope) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_get_int64(env, first, &number) ==
        MRDOCS_STATUS_INVALID_ARG);

    // A scope the callback opens and leaves open ends with the callback, so
    // a scope opened before the visit can still be closed afterwards.
    mrdocs_scope outerScope = 0;
    BOOST_TEST(mrdocs_scope_open(env, &outerScope) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_visit(env, object, visitLeaking, nullptr) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_scope_close(env, outerScope) == MRDOCS_STATUS_OK);

    // A `$meta` property of an object the plugin made is the plugin's own,
    // and stays writable.
    {
        mrdocs_value owner = nullptr;
        mrdocs_value meta = nullptr;
        mrdocs_value got = nullptr;
        mrdocs_value metaValue = nullptr;
        int writable = 0;
        mrdocs_create_object(env, &owner);
        mrdocs_create_object(env, &meta);
        mrdocs_create_int64(env, 1, &metaValue);
        BOOST_TEST(mrdocs_object_set(env, owner, "$meta", meta) ==
            MRDOCS_STATUS_OK);
        BOOST_TEST(mrdocs_object_get(env, owner, "$meta", &got) ==
            MRDOCS_STATUS_OK);
        BOOST_TEST(mrdocs_object_set(env, got, "k", metaValue) ==
            MRDOCS_STATUS_OK);
        BOOST_TEST(mrdocs_object_visit(
            env, owner, visitWritingMeta, &writable) == MRDOCS_STATUS_OK);
        BOOST_TEST(writable == 1);
    }

    // A reference outlives its scope and is deleted by its owner.
    mrdocs_ref ref = nullptr;
    BOOST_TEST(mrdocs_scope_open(env, &scope) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_string(
        env, "kept", MRDOCS_AUTO_LENGTH, &inside) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_ref_create(env, inside, &ref) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_scope_close(env, scope) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_ref_get(env, ref, &found) == MRDOCS_STATUS_OK);
    BOOST_TEST(text(env, found) == "kept");
    BOOST_TEST(mrdocs_ref_delete(env, ref) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_ref_delete(env, nullptr) == MRDOCS_STATUS_OK);

    // Arguments that cannot be valid are refused, not dereferenced.
    BOOST_TEST(mrdocs_create_null(nullptr, &value) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_create_null(env, nullptr) == MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_get_kind(env, nullptr, &kind) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_object_set(env, object, nullptr, one) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_create_string(env, nullptr, 0, &value) ==
        MRDOCS_STATUS_INVALID_ARG);
    return MRDOCS_STATUS_OK;
}

// Ask for what the context of an initializer offers, and for what only the
// other callbacks have.
mrdocs_status
contextInit(mrdocs_env* env)
{
    mrdocs_value value = nullptr;
    mrdocs_value other = nullptr;
    char buffer[8] = {};
    size_t length = 0;
    BOOST_TEST(mrdocs_get_corpus(env, &value) == MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_corpus_find(env, "x", &value) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_corpus_lookup(env, "x", nullptr, &value) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_get_output_dir(env, buffer, sizeof buffer, &length) ==
        MRDOCS_STATUS_INVALID_ARG);

    // The configuration and the parameters are views nobody may write to or
    // keep.
    BOOST_TEST(mrdocs_get_config(env, &value) == MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, value) == MRDOCS_VALUE_OBJECT);
    bool flag = false;
    BOOST_TEST(mrdocs_object_has(env, value, "generator", &flag) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(flag);
    BOOST_TEST(mrdocs_create_int64(env, 1, &other) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_set(env, value, "generator", other) ==
        MRDOCS_STATUS_READ_ONLY);
    mrdocs_ref ref = nullptr;
    BOOST_TEST(mrdocs_ref_create(env, value, &ref) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(ref == nullptr);

    mrdocs_value params = nullptr;
    BOOST_TEST(mrdocs_get_params(env, &params) == MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, params) == MRDOCS_VALUE_OBJECT);
    BOOST_TEST(mrdocs_object_set(env, params, "k", other) ==
        MRDOCS_STATUS_READ_ONLY);

    // A container the plugin made can be kept, and a view cannot be stored
    // in one, so a container never holds a view.
    mrdocs_value holder = nullptr;
    BOOST_TEST(mrdocs_create_object(env, &holder) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_ref_create(env, holder, &ref) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_ref_delete(env, ref) == MRDOCS_STATUS_OK);
    ref = nullptr;
    BOOST_TEST(mrdocs_object_set(env, holder, "config", value) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_object_set(env, holder, "params", params) ==
        MRDOCS_STATUS_INVALID_ARG);
    mrdocs_value list = nullptr;
    BOOST_TEST(mrdocs_create_array(env, &list) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_array_push(env, list, value) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_array_push(env, list, other) == MRDOCS_STATUS_OK);
    bool flag2 = true;
    BOOST_TEST(mrdocs_object_has(env, holder, "config", &flag2) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(!flag2);

    // What a view holds is copied out as scalars, which can be stored.
    mrdocs_value generator = nullptr;
    BOOST_TEST(mrdocs_object_get(env, value, "generator", &generator) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_set(env, holder, "generator", generator) ==
        MRDOCS_STATUS_OK);

    // The keys of the configuration are the option names in camelCase, so
    // `warn-as-error` is read as `warnAsError`, and a key spelled like the
    // option reads nothing. A generator reads a string option below.
    mrdocs_value warnAsError = nullptr;
    BOOST_TEST(mrdocs_object_get(env, value, "warnAsError", &warnAsError) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, warnAsError) == MRDOCS_VALUE_BOOLEAN);
    mrdocs_value dashed = nullptr;
    BOOST_TEST(mrdocs_object_get(env, value, "warn-as-error", &dashed) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, dashed) == MRDOCS_VALUE_UNDEFINED);

    // A container reached through a container the plugin made is the
    // plugin's own, and a reference to it shares the storage.
    mrdocs_value inner = nullptr;
    mrdocs_value reached = nullptr;
    mrdocs_value found = nullptr;
    BOOST_TEST(mrdocs_create_object(env, &inner) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_set(env, holder, "inner", inner) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_get(env, holder, "inner", &reached) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_set(env, reached, "k", other) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_ref_create(env, reached, &ref) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_ref_get(env, ref, &found) == MRDOCS_STATUS_OK);
    flag2 = false;
    BOOST_TEST(mrdocs_object_has(env, found, "k", &flag2) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(flag2);
    BOOST_TEST(mrdocs_ref_delete(env, ref) == MRDOCS_STATUS_OK);
    ref = nullptr;

    // Anything reached through a view is a view, whatever its depth.
    mrdocs_value deep = nullptr;
    BOOST_TEST(mrdocs_get_params(env, &deep) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_ref_create(env, deep, &ref) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(ref == nullptr);

    // A failing function leaves its output parameters untouched, the ABI
    // included.
    uint32_t abi = 7;
    size_t untouched = 5;
    BOOST_TEST(mrdocs_host_info(env, &abi, nullptr, 8, &untouched) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(abi == 7);
    BOOST_TEST(untouched == 5);

    abi = 0;
    BOOST_TEST(mrdocs_host_info(env, &abi, buffer, sizeof buffer, &length) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(abi == MRDOCS_PLUGIN_ABI_VERSION);
    BOOST_TEST(length != 0);
    BOOST_TEST(mrdocs_log(env, MRDOCS_LOG_TRACE, "from a unit test") ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_log(env, static_cast<mrdocs_log_level>(99), "x") ==
        MRDOCS_STATUS_INVALID_ARG);
    return MRDOCS_STATUS_OK;
}

// Log one message at the warning level.
mrdocs_status
loggingInit(mrdocs_env* env)
{
    BOOST_TEST(mrdocs_log(env, MRDOCS_LOG_WARN, "a warning from a unit test") ==
        MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_OK;
}

// Log one message at the error level.
mrdocs_status
errorLoggingInit(mrdocs_env* env)
{
    BOOST_TEST(mrdocs_log(env, MRDOCS_LOG_ERROR, "an error from a unit test") ==
        MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_OK;
}

mrdocs_status
nothingInit(mrdocs_env*)
{
    return MRDOCS_STATUS_OK;
}

mrdocs_status
failingInit(mrdocs_env*)
{
    return MRDOCS_STATUS_PLUGIN_ERROR;
}

mrdocs_status
errorInit(mrdocs_env* env)
{
    mrdocs_set_error(env, "first message");
    mrdocs_set_error(env, "second message");
    return MRDOCS_STATUS_OK;
}

mrdocs_status
badCallInit(mrdocs_env* env)
{
    mrdocs_value value = nullptr;
    // The failed call leaves its reason behind, which the diagnostic of the
    // plugin's own failure then carries.
    mrdocs_get_corpus(env, &value);
    return MRDOCS_STATUS_PLUGIN_ERROR;
}

// A call that fails by design, followed by calls that succeed, followed by
// a failure of the plugin's own that it does not explain.
mrdocs_status
staleFailureInit(mrdocs_env* env)
{
    mrdocs_value value = nullptr;
    mrdocs_get_corpus(env, &value);
    BOOST_TEST(mrdocs_create_int64(env, 1, &value) == MRDOCS_STATUS_OK);
    int64_t number = 0;
    BOOST_TEST(mrdocs_get_int64(env, value, &number) == MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_PLUGIN_ERROR;
}

// A visit callback that probes a property with a call that fails by design.
bool
visitProbing(mrdocs_env* env, char const*, mrdocs_value value, void*)
{
    int64_t number = 0;
    mrdocs_value child = nullptr;
    mrdocs_object_get(env, value, "missing", &child);
    mrdocs_get_int64(env, value, &number);
    return true;
}

// A visit whose callback made calls that failed, followed by a failure of
// the plugin's own that it does not explain.
mrdocs_status
staleVisitFailureInit(mrdocs_env* env)
{
    mrdocs_value object = nullptr;
    mrdocs_value text = nullptr;
    BOOST_TEST(mrdocs_create_object(env, &object) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_create_string(env, "x", 1, &text) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_set(env, object, "a", text) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_visit(env, object, visitProbing, nullptr) ==
        MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_PLUGIN_ERROR;
}

// The failing transform stays in the test pipeline once registered, so it
// fails only while a test arms it: with a message, with none, or with none
// after a call to MrDocs that failed.
enum class FailMode
{
    off,
    message,
    silent,
    silentAfterFailedCall
};
FailMode failTransformMode = FailMode::off;

mrdocs_status
failTransform(mrdocs_env* env, void*)
{
    switch (failTransformMode)
    {
    case FailMode::off:
        return MRDOCS_STATUS_OK;
    case FailMode::message:
        mrdocs_set_error(env, "transform boom");
        return MRDOCS_STATUS_PLUGIN_ERROR;
    case FailMode::silentAfterFailedCall:
    {
        mrdocs_value config = nullptr;
        int64_t number = 0;
        mrdocs_get_config(env, &config);
        mrdocs_get_int64(env, config, &number);
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    case FailMode::silent:
        break;
    }
    return MRDOCS_STATUS_PLUGIN_ERROR;
}

mrdocs_status
registerFailingTransform(mrdocs_env* env)
{
    mrdocs_transform_desc transform = {};
    transform.struct_size = sizeof(transform);
    transform.id = "capi-test-transform-error";
    transform.apply = failTransform;
    BOOST_TEST(mrdocs_register_transform(env, &transform) ==
        MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_OK;
}

void
release(void*)
{
    ++observed.releases;
}

// A plugin that keeps a reference in its `data` deletes it when MrDocs gives
// the data back, which happens with no environment.
void
releaseReference(void* data)
{
    mrdocs_ref_delete(nullptr, static_cast<mrdocs_ref>(data));
    ++observed.referencesDeleted;
}

mrdocs_status
generateNothing(mrdocs_env*, void*)
{
    return MRDOCS_STATUS_OK;
}

mrdocs_status
applyNothing(mrdocs_env*, void*)
{
    return MRDOCS_STATUS_OK;
}

// Registers a generator whose id a generator already has. The registry
// refuses it after MrDocs accepted the descriptor, so the registration
// reports a host error, and `release` has run once by the time it returns
// because the plugin's data was owned from the moment the descriptor was
// accepted.
mrdocs_status
duplicateIdInit(mrdocs_env* env)
{
    int const before = observed.releases;

    mrdocs_generator_desc generator = {};
    generator.struct_size = sizeof(generator);
    generator.id = "xml";
    generator.build = generateNothing;
    generator.release = release;
    BOOST_TEST(mrdocs_register_generator(env, &generator) ==
        MRDOCS_STATUS_HOST_ERROR);
    BOOST_TEST(observed.releases == before + 1);
    return MRDOCS_STATUS_OK;
}

// Register a transform from a descriptor that is longer than the host's, as
// a plugin built for a later ABI passes one, with garbage after the fields
// this MrDocs knows.
mrdocs_status
largerDescriptorInit(mrdocs_env* env)
{
    struct
    {
        mrdocs_transform_desc desc;
        char later[32];
    } buffer;
    std::memset(&buffer, 0xA5, sizeof buffer);
    buffer.desc.struct_size = sizeof buffer;
    buffer.desc.id = "capi-test-larger-descriptor";
    buffer.desc.apply = applyNothing;
    buffer.desc.data = nullptr;
    buffer.desc.release = nullptr;
    BOOST_TEST(mrdocs_register_transform(env, &buffer.desc) ==
        MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_OK;
}

mrdocs_status
referenceInit(mrdocs_env* env)
{
    mrdocs_value value = nullptr;
    mrdocs_ref ref = nullptr;
    BOOST_TEST(mrdocs_create_object(env, &value) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_ref_create(env, value, &ref) == MRDOCS_STATUS_OK);
    mrdocs_generator_desc desc = {};
    desc.struct_size = sizeof(desc);
    desc.id = "capi-test-generator-reference";
    desc.build = generateNothing;
    desc.data = ref;
    desc.release = releaseReference;
    BOOST_TEST(mrdocs_register_generator(env, &desc) == MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_OK;
}

mrdocs_status
generate(mrdocs_env* env, void* data)
{
    char const* const mode = static_cast<char const*>(data);
    if (std::string_view(mode) == "error")
    {
        mrdocs_set_error(env, "generator boom");
        return MRDOCS_STATUS_OK;
    }
    if (std::string_view(mode) == "silent")
    {
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }

    mrdocs_value corpus = nullptr;
    mrdocs_value symbols = nullptr;
    size_t size = 0;
    BOOST_TEST(mrdocs_get_corpus(env, &corpus) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_get(env, corpus, "symbols", &symbols) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_array_length(env, symbols, &size) == MRDOCS_STATUS_OK);
    mrdocs_value widget = nullptr;
    for (size_t i = 0; i < size; ++i)
    {
        mrdocs_scope scope = 0;
        mrdocs_scope_open(env, &scope);
        mrdocs_value symbol = nullptr;
        mrdocs_value name = nullptr;
        mrdocs_array_get(env, symbols, i, &symbol);
        mrdocs_object_get(env, symbol, "name", &name);
        observed.names.push_back(text(env, name));
        if (observed.names.back() == "Widget")
        {
            mrdocs_value id = nullptr;
            mrdocs_object_get(env, symbol, "id", &id);
            observed.foundWidget =
                mrdocs_corpus_find(env, text(env, id).c_str(), &widget) ==
                MRDOCS_STATUS_OK;
        }
        mrdocs_scope_close(env, scope);
    }

    // The corpus a generator sees is read-only, and an id that names
    // nothing is not found.
    mrdocs_value other = nullptr;
    mrdocs_create_int64(env, 1, &other);
    observed.readOnlyRefused =
        mrdocs_object_set(env, corpus, "symbols", other) ==
        MRDOCS_STATUS_READ_ONLY;
    // A function taken from a view is a view too, and is not stored.
    mrdocs_value getter = nullptr;
    mrdocs_value holder = nullptr;
    mrdocs_ref held = nullptr;
    BOOST_TEST(mrdocs_object_get(env, corpus, "get", &getter) ==
        MRDOCS_STATUS_OK);
    BOOST_TEST(kindOf(env, getter) == MRDOCS_VALUE_FUNCTION);
    BOOST_TEST(mrdocs_create_object(env, &holder) == MRDOCS_STATUS_OK);
    BOOST_TEST(mrdocs_object_set(env, holder, "f", getter) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_ref_create(env, getter, &held) ==
        MRDOCS_STATUS_INVALID_ARG);

    mrdocs_value missing = nullptr;
    observed.missingRefused =
        mrdocs_corpus_find(env, "not an id", &missing) ==
        MRDOCS_STATUS_KEY_NOT_FOUND;

    // A name is found with the access the generator has to the corpus.
    mrdocs_value named = nullptr;
    observed.lookedUpWidget =
        mrdocs_corpus_lookup(env, "Widget", nullptr, &named) ==
            MRDOCS_STATUS_OK &&
        mrdocs_object_set(env, named, "name", named) ==
            MRDOCS_STATUS_READ_ONLY;

    mrdocs_value params = nullptr;
    mrdocs_value answer = nullptr;
    mrdocs_get_params(env, &params);
    mrdocs_object_get(env, params, "answer", &answer);
    mrdocs_get_int64(env, answer, &observed.answer);

    // A string option is read by its camelCase key, and the string copied
    // out of the view can be stored in an object of the plugin.
    mrdocs_value config = nullptr;
    mrdocs_value sourceRoot = nullptr;
    mrdocs_value keeper = nullptr;
    mrdocs_value kept = nullptr;
    mrdocs_get_config(env, &config);
    mrdocs_object_get(env, config, "sourceRoot", &sourceRoot);
    mrdocs_create_object(env, &keeper);
    observed.sourceRootStored =
        mrdocs_object_set(env, keeper, "sourceRoot", sourceRoot) ==
            MRDOCS_STATUS_OK &&
        mrdocs_object_get(env, keeper, "sourceRoot", &kept) ==
            MRDOCS_STATUS_OK;
    observed.sourceRoot = text(env, kept);

    char dir[1024] = {};
    size_t length = 0;
    mrdocs_get_output_dir(env, dir, sizeof dir, &length);
    observed.outputDir = std::string(dir, length);
    return MRDOCS_STATUS_OK;
}

// Find by name what the transform that ran before this one renamed.
mrdocs_status
lookupRenamed(mrdocs_env* env, void*)
{
    mrdocs_value found = nullptr;
    observed.lookupFound =
        mrdocs_corpus_lookup(env, "Gadget", nullptr, &found) ==
        MRDOCS_STATUS_OK;
    if (!observed.lookupFound)
    {
        return MRDOCS_STATUS_OK;
    }
    mrdocs_value name = nullptr;
    mrdocs_value id = nullptr;
    mrdocs_object_get(env, found, "name", &name);
    mrdocs_object_get(env, found, "id", &id);
    observed.lookupName = text(env, name);

    mrdocs_value stale = nullptr;
    observed.lookupStale =
        mrdocs_corpus_lookup(env, "Widget", nullptr, &stale) ==
        MRDOCS_STATUS_KEY_NOT_FOUND;

    // Relative to a scope, an unqualified name finds a symbol outside it.
    mrdocs_value viaContext = nullptr;
    std::string const idText = text(env, id);
    observed.lookupContext =
        mrdocs_corpus_lookup(env, "Gadget", idText.c_str(), &viaContext) ==
        MRDOCS_STATUS_OK;

    // The context must be the id of a symbol, and the arguments are checked.
    // A well-formed id that names no symbol is refused like text that is not
    // an id: here the last digit of a real id is changed.
    std::string staleId = idText;
    staleId.back() = staleId.back() == '2' ? '3' : '2';
    mrdocs_value refused = nullptr;
    observed.lookupArguments =
        mrdocs_corpus_lookup(env, "Gadget", "not an id", &refused) ==
            MRDOCS_STATUS_INVALID_ARG &&
        mrdocs_corpus_lookup(env, "Gadget", staleId.c_str(), &refused) ==
            MRDOCS_STATUS_INVALID_ARG &&
        mrdocs_corpus_lookup(env, nullptr, nullptr, &refused) ==
            MRDOCS_STATUS_INVALID_ARG &&
        mrdocs_corpus_lookup(env, "Gadget", nullptr, nullptr) ==
            MRDOCS_STATUS_INVALID_ARG &&
        refused == nullptr;
    return MRDOCS_STATUS_OK;
}

mrdocs_status
rename(mrdocs_env* env, void*)
{
    mrdocs_value corpus = nullptr;
    mrdocs_value symbols = nullptr;
    size_t size = 0;
    mrdocs_get_corpus(env, &corpus);
    mrdocs_object_get(env, corpus, "symbols", &symbols);
    mrdocs_array_length(env, symbols, &size);
    for (size_t i = 0; i < size; ++i)
    {
        mrdocs_scope scope = 0;
        mrdocs_scope_open(env, &scope);
        mrdocs_value symbol = nullptr;
        mrdocs_value name = nullptr;
        mrdocs_array_get(env, symbols, i, &symbol);
        mrdocs_object_get(env, symbol, "name", &name);
        if (text(env, name) == "Widget")
        {
            mrdocs_value renamed = nullptr;
            mrdocs_create_string(env, "Gadget", MRDOCS_AUTO_LENGTH, &renamed);
            if (mrdocs_object_set(env, symbol, "name", renamed) !=
                MRDOCS_STATUS_OK)
            {
                return MRDOCS_STATUS_HOST_ERROR;
            }
            // A field takes only a value of its own type.
            mrdocs_value wrong = nullptr;
            mrdocs_ref viewRef = nullptr;
            mrdocs_create_bool(env, true, &wrong);
            BOOST_TEST(mrdocs_object_set(env, symbol, "name", wrong) ==
                MRDOCS_STATUS_HOST_ERROR);

            // A write through a view during a visit of it is seen by the
            // callbacks that follow: they read what the property holds now.
            VisitWrite visitWrite;
            visitWrite.symbol = symbol;
            BOOST_TEST(mrdocs_object_visit(
                env, symbol, visitWriting, &visitWrite) == MRDOCS_STATUS_OK);
            BOOST_TEST(visitWrite.wrote);
            BOOST_TEST(visitWrite.name == "Visited");
            BOOST_TEST(mrdocs_object_set(env, symbol, "name", renamed) ==
                MRDOCS_STATUS_OK);

            // The values of an object the plugin made are the ones it had
            // when the visit started, whatever the callback writes, to the
            // object or to a symbol.
            for (bool const touchSymbol : {false, true})
            {
                VisitOwn visitOwned;
                mrdocs_value one = nullptr;
                mrdocs_value two = nullptr;
                mrdocs_create_object(env, &visitOwned.own);
                mrdocs_create_int64(env, 1, &one);
                mrdocs_create_int64(env, 2, &two);
                mrdocs_object_set(env, visitOwned.own, "a", one);
                mrdocs_object_set(env, visitOwned.own, "b", two);
                visitOwned.symbol = symbol;
                visitOwned.touchSymbol = touchSymbol;
                BOOST_TEST(mrdocs_object_visit(
                    env, visitOwned.own, visitOwn, &visitOwned) ==
                    MRDOCS_STATUS_OK);
                BOOST_TEST(visitOwned.seenB == 2);
            }
            BOOST_TEST(mrdocs_object_set(env, symbol, "name", renamed) ==
                MRDOCS_STATUS_OK);

            // A container that holds itself cannot be copied into a symbol:
            // the write fails with a status and the process lives. A value
            // that only nests deeply is copied.
            mrdocs_value cyclicDoc = nullptr;
            mrdocs_value loop = nullptr;
            mrdocs_create_object(env, &cyclicDoc);
            mrdocs_object_set(env, cyclicDoc, "brief",
                makeBrief(env, 0, true, &loop));
            BOOST_TEST(mrdocs_object_set(env, symbol, "doc", cyclicDoc) ==
                MRDOCS_STATUS_HOST_ERROR);
            breakLoop(env, loop);
            mrdocs_value deepDoc = nullptr;
            mrdocs_create_object(env, &deepDoc);
            mrdocs_object_set(env, deepDoc, "brief", makeBrief(env, 20, false));
            BOOST_TEST(mrdocs_object_set(env, symbol, "doc", deepDoc) ==
                MRDOCS_STATUS_OK);
            mrdocs_value tooDeepDoc = nullptr;
            mrdocs_create_object(env, &tooDeepDoc);
            mrdocs_object_set(env, tooDeepDoc, "brief",
                makeBrief(env, 500, false));
            BOOST_TEST(mrdocs_object_set(env, symbol, "doc", tooDeepDoc) ==
                MRDOCS_STATUS_HOST_ERROR);

            // A view is not stored in a container the plugin made, and a
            // symbol is not written into another symbol.
            mrdocs_value wrapper = nullptr;
            mrdocs_create_object(env, &wrapper);
            BOOST_TEST(mrdocs_object_set(env, wrapper, "symbol", symbol) ==
                MRDOCS_STATUS_INVALID_ARG);
            BOOST_TEST(mrdocs_ref_create(env, symbol, &viewRef) ==
                MRDOCS_STATUS_INVALID_ARG);
            BOOST_TEST(viewRef == nullptr);

            // A container the plugin made lands in the symbol as a copy:
            // changing the container afterwards does not change the symbol.
            mrdocs_value doc = nullptr;
            mrdocs_value sees = nullptr;
            mrdocs_value stored = nullptr;
            mrdocs_value entry = nullptr;
            mrdocs_value back = nullptr;
            size_t count = 7;
            mrdocs_create_object(env, &doc);
            mrdocs_create_array(env, &sees);
            mrdocs_value literal = nullptr;
            mrdocs_create_object(env, &entry);
            mrdocs_create_string(env, "x", MRDOCS_AUTO_LENGTH, &literal);
            mrdocs_object_set(env, entry, "literal", literal);
            mrdocs_array_push(env, sees, entry);
            mrdocs_object_set(env, doc, "relates", sees);
            BOOST_TEST(mrdocs_object_set(env, symbol, "doc", doc) ==
                MRDOCS_STATUS_OK);
            mrdocs_array_push(env, sees, entry);
            BOOST_TEST(mrdocs_object_get(env, symbol, "doc", &back) ==
                MRDOCS_STATUS_OK);
            BOOST_TEST(mrdocs_object_get(env, back, "relates", &stored) ==
                MRDOCS_STATUS_OK);
            BOOST_TEST(mrdocs_array_length(env, stored, &count) ==
                MRDOCS_STATUS_OK);
            BOOST_TEST(count == 1u);

            // A push can reallocate the elements of the vector, so the
            // handle to an element made before it is read again from its
            // array and still names the same element.
            mrdocs_value first = nullptr;
            mrdocs_value firstLiteral = nullptr;
            BOOST_TEST(mrdocs_array_get(env, stored, 0, &first) ==
                MRDOCS_STATUS_OK);
            for (int pushes = 0; pushes < 32; ++pushes)
            {
                BOOST_TEST(mrdocs_array_push(env, stored, entry) ==
                    MRDOCS_STATUS_OK);
            }
            BOOST_TEST(mrdocs_array_length(env, stored, &count) ==
                MRDOCS_STATUS_OK);
            BOOST_TEST(count == 33u);
            BOOST_TEST(mrdocs_object_get(env, first, "literal", &firstLiteral) ==
                MRDOCS_STATUS_OK);
            BOOST_TEST(text(env, firstLiteral) == "x");

            // A handle whose place no longer holds a value is refused:
            // emptying the `relates` array leaves the handle to its first
            // element with nothing to name.
            mrdocs_value emptyDoc = nullptr;
            mrdocs_value emptyRelates = nullptr;
            mrdocs_value gone = nullptr;
            mrdocs_create_object(env, &emptyDoc);
            mrdocs_create_array(env, &emptyRelates);
            mrdocs_object_set(env, emptyDoc, "relates", emptyRelates);
            BOOST_TEST(mrdocs_object_set(env, symbol, "doc", emptyDoc) ==
                MRDOCS_STATUS_OK);
            BOOST_TEST(mrdocs_object_get(env, first, "literal", &gone) ==
                MRDOCS_STATUS_INVALID_ARG);

            // A write that fails partway has already replaced part of the
            // field, so it moves what views point at like one that works:
            // `relates` is replaced with an empty array before `bogus` is
            // refused, and the handle to its first element, which names
            // nothing now, is refused instead of reading freed storage.
            mrdocs_value partialDoc = nullptr;
            mrdocs_value partialRelates = nullptr;
            mrdocs_value partialBogus = nullptr;
            mrdocs_value viewedDoc = nullptr;
            mrdocs_value viewedRelates = nullptr;
            mrdocs_value viewedFirst = nullptr;
            mrdocs_value viewedLiteral = nullptr;
            mrdocs_value seeded = nullptr;
            mrdocs_value seededRelates = nullptr;
            mrdocs_create_object(env, &seeded);
            mrdocs_create_array(env, &seededRelates);
            mrdocs_array_push(env, seededRelates, entry);
            mrdocs_object_set(env, seeded, "relates", seededRelates);
            BOOST_TEST(mrdocs_object_set(env, symbol, "doc", seeded) ==
                MRDOCS_STATUS_OK);
            mrdocs_object_get(env, symbol, "doc", &viewedDoc);
            mrdocs_object_get(env, viewedDoc, "relates", &viewedRelates);
            BOOST_TEST(mrdocs_array_get(env, viewedRelates, 0, &viewedFirst) ==
                MRDOCS_STATUS_OK);
            BOOST_TEST(mrdocs_object_get(
                env, viewedFirst, "literal", &viewedLiteral) ==
                MRDOCS_STATUS_OK);
            BOOST_TEST(text(env, viewedLiteral) == "x");
            mrdocs_create_object(env, &partialDoc);
            mrdocs_create_array(env, &partialRelates);
            mrdocs_object_set(env, partialDoc, "relates", partialRelates);
            mrdocs_create_int64(env, 1, &partialBogus);
            mrdocs_object_set(env, partialDoc, "bogus", partialBogus);
            BOOST_TEST(mrdocs_object_set(env, symbol, "doc", partialDoc) ==
                MRDOCS_STATUS_HOST_ERROR);
            viewedLiteral = nullptr;
            BOOST_TEST(mrdocs_object_get(
                env, viewedFirst, "literal", &viewedLiteral) ==
                MRDOCS_STATUS_INVALID_ARG);

            // `$meta` is shared by every symbol of the type, so it is not
            // writable, and neither is anything inside it.
            mrdocs_value meta = nullptr;
            mrdocs_value bases = nullptr;
            mrdocs_value other = nullptr;
            mrdocs_create_string(env, "Other", MRDOCS_AUTO_LENGTH, &other);
            BOOST_TEST(mrdocs_object_get(env, symbol, "$meta", &meta) ==
                MRDOCS_STATUS_OK);
            BOOST_TEST(mrdocs_object_set(env, meta, "type", other) ==
                MRDOCS_STATUS_READ_ONLY);
            BOOST_TEST(mrdocs_object_get(env, meta, "bases", &bases) ==
                MRDOCS_STATUS_OK);
            BOOST_TEST(mrdocs_array_push(env, bases, other) ==
                MRDOCS_STATUS_READ_ONLY);

            // The kind, the id, the parent and the inherited-from link of
            // a symbol are refused whatever the value, and the symbol is
            // the same afterwards.
            mrdocs_value kindBefore = nullptr;
            mrdocs_value idBefore = nullptr;
            mrdocs_value kindAfter = nullptr;
            mrdocs_value idAfter = nullptr;
            mrdocs_object_get(env, symbol, "kind", &kindBefore);
            mrdocs_object_get(env, symbol, "id", &idBefore);
            std::string const kindText = text(env, kindBefore);
            std::string const idText = text(env, idBefore);
            mrdocs_value function = nullptr;
            mrdocs_create_string(env, "function", MRDOCS_AUTO_LENGTH, &function);
            for (char const* field : {"kind", "id", "parent", "inheritedFrom"})
            {
                BOOST_TEST(mrdocs_object_set(env, symbol, field, function) ==
                    MRDOCS_STATUS_READ_ONLY);
            }
            mrdocs_object_get(env, symbol, "kind", &kindAfter);
            mrdocs_object_get(env, symbol, "id", &idAfter);
            BOOST_TEST(text(env, kindAfter) == kindText);
            BOOST_TEST(text(env, idAfter) == idText);
        }
        mrdocs_scope_close(env, scope);
    }
    mrdocs_value params = nullptr;
    mrdocs_value answer = nullptr;
    mrdocs_get_params(env, &params);
    mrdocs_object_get(env, params, "answer", &answer);
    mrdocs_get_int64(env, answer, &observed.answer);
    return MRDOCS_STATUS_OK;
}

// What the transforms of the pipeline test saw: each one logs its own mode,
// the name its callback found for the symbol that starts with `Widget`, and
// the `answer` of its parameters.
std::vector<std::string> pipelineLog;

// How many times a plugin that counts its initializations ran its entry point.
int initCount = 0;

mrdocs_status
logWidget(mrdocs_env* env, void* data)
{
    mrdocs_value corpus = nullptr;
    mrdocs_value symbols = nullptr;
    size_t size = 0;
    mrdocs_get_corpus(env, &corpus);
    mrdocs_object_get(env, corpus, "symbols", &symbols);
    mrdocs_array_length(env, symbols, &size);
    std::string seen = "<none>";
    for (size_t i = 0; i < size; ++i)
    {
        mrdocs_scope scope = 0;
        mrdocs_scope_open(env, &scope);
        mrdocs_value symbol = nullptr;
        mrdocs_value name = nullptr;
        mrdocs_array_get(env, symbols, i, &symbol);
        mrdocs_object_get(env, symbol, "name", &name);
        if (std::string const candidate = text(env, name);
            candidate.starts_with("Widget"))
        {
            seen = candidate;
        }
        mrdocs_scope_close(env, scope);
    }
    mrdocs_value params = nullptr;
    mrdocs_value answer = nullptr;
    std::int64_t number = -1;
    mrdocs_get_params(env, &params);
    if (mrdocs_object_get(env, params, "answer", &answer) == MRDOCS_STATUS_OK)
    {
        mrdocs_get_int64(env, answer, &number);
    }
    pipelineLog.push_back(
        std::string(static_cast<char const*>(data)) + ":" + seen + ":" +
        std::to_string(number));
    return MRDOCS_STATUS_OK;
}

mrdocs_status
registerLogging(mrdocs_env* env, char const* id, char const* mode)
{
    mrdocs_transform_desc transform = {};
    transform.struct_size = sizeof(transform);
    transform.id = id;
    transform.apply = logWidget;
    transform.data = const_cast<char*>(mode);
    BOOST_TEST(mrdocs_register_transform(env, &transform) ==
        MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_OK;
}

mrdocs_status
registerPipelineFirst(mrdocs_env* env)
{
    return registerLogging(env, "capi-order-first", "first");
}

mrdocs_status
registerPipelineLast(mrdocs_env* env)
{
    return registerLogging(env, "capi-order-last", "last");
}

// A transform that reads the `page` parameter, which the test sets to a safe
// string, and logs its kind and the text the string reader gives for it.
mrdocs_status
logSafeString(mrdocs_env* env, void*)
{
    mrdocs_value params = nullptr;
    mrdocs_value page = nullptr;
    mrdocs_get_params(env, &params);
    mrdocs_object_get(env, params, "page", &page);
    pipelineLog.push_back(
        std::to_string(static_cast<int>(kindOf(env, page))) + ":" +
        text(env, page));
    return MRDOCS_STATUS_OK;
}

mrdocs_status
registerSafeStringReader(mrdocs_env* env)
{
    mrdocs_transform_desc transform = {};
    transform.struct_size = sizeof(transform);
    transform.id = "capi-safe-string";
    transform.apply = logSafeString;
    BOOST_TEST(mrdocs_register_transform(env, &transform) ==
        MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_OK;
}

mrdocs_status
registerSharedLogging(mrdocs_env* env)
{
    ++initCount;
    mrdocs_transform_desc transform = {};
    transform.struct_size = sizeof(transform);
    transform.id = "capi-order-first";
    transform.apply = logWidget;
    transform.data = const_cast<char*>("shared");
    transform.release = release;
    BOOST_TEST(mrdocs_register_transform(env, &transform) ==
        MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_OK;
}

// A string the descriptors use as `data`, so that a callback can tell which
// behavior it was registered for.
char const modeNormal[] = "normal";
char const modeError[] = "error";
char const modeSilent[] = "silent";

mrdocs_status
registerGenerators(mrdocs_env* env)
{
    mrdocs_generator_desc desc = {};
    desc.struct_size = sizeof(desc);
    desc.id = "capi-test-generator";
    desc.display_name = "C API test";
    desc.file_extension = "txt";
    desc.build = generate;
    desc.data = const_cast<char*>(modeNormal);
    desc.release = release;
    BOOST_TEST(mrdocs_register_generator(env, &desc) == MRDOCS_STATUS_OK);

    desc.id = "capi-test-generator-error";
    desc.display_name = nullptr;
    desc.file_extension = nullptr;
    desc.data = const_cast<char*>(modeError);
    BOOST_TEST(mrdocs_register_generator(env, &desc) == MRDOCS_STATUS_OK);

    desc.id = "capi-test-generator-silent";
    desc.data = const_cast<char*>(modeSilent);
    BOOST_TEST(mrdocs_register_generator(env, &desc) == MRDOCS_STATUS_OK);

    mrdocs_transform_desc transform = {};
    transform.struct_size = sizeof(transform);
    transform.id = "capi-test-transform";
    transform.apply = rename;
    transform.release = release;
    BOOST_TEST(mrdocs_register_transform(env, &transform) ==
        MRDOCS_STATUS_OK);

    transform.id = "capi-test-lookup";
    transform.apply = lookupRenamed;
    transform.release = nullptr;
    BOOST_TEST(mrdocs_register_transform(env, &transform) ==
        MRDOCS_STATUS_OK);
    return MRDOCS_STATUS_OK;
}

// Register against ids that are taken, and with descriptors that are not
// valid.
mrdocs_status
registerRefused(mrdocs_env* env)
{
    int const before = observed.releases;

    mrdocs_generator_desc desc = {};
    desc.struct_size = sizeof(desc);
    desc.id = "capi-test-generator";
    desc.build = generate;
    desc.data = const_cast<char*>(modeNormal);
    desc.release = release;
    // A duplicate id is an error, and MrDocs has already taken `data` by the
    // time it finds out.
    BOOST_TEST(mrdocs_register_generator(env, &desc) ==
        MRDOCS_STATUS_HOST_ERROR);
    BOOST_TEST(observed.releases == before + 1);

    // A built-in id is taken too.
    desc.id = "xml";
    BOOST_TEST(mrdocs_register_generator(env, &desc) ==
        MRDOCS_STATUS_HOST_ERROR);
    BOOST_TEST(observed.releases == before + 2);

    // An invalid descriptor leaves `data` with the caller.
    desc.id = "";
    BOOST_TEST(mrdocs_register_generator(env, &desc) ==
        MRDOCS_STATUS_INVALID_ARG);
    desc.id = "capi-test-never";
    desc.build = nullptr;
    BOOST_TEST(mrdocs_register_generator(env, &desc) ==
        MRDOCS_STATUS_INVALID_ARG);
    desc.build = generate;
    desc.struct_size = sizeof(size_t);
    BOOST_TEST(mrdocs_register_generator(env, &desc) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(mrdocs_register_generator(env, nullptr) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(findGenerator("capi-test-never") == nullptr);

    mrdocs_transform_desc transform = {};
    transform.struct_size = sizeof(transform);
    transform.id = "capi-test-never";
    BOOST_TEST(mrdocs_register_transform(env, &transform) ==
        MRDOCS_STATUS_INVALID_ARG);
    BOOST_TEST(observed.releases == before + 2);
    return MRDOCS_STATUS_OK;
}

// Write `content` verbatim to `path`.
void
writeFile(std::string_view path, std::string_view content)
{
    std::ofstream os(std::string{path}, std::ios::binary | std::ios::trunc);
    os.write(content.data(), static_cast<std::streamsize>(content.size()));
}

} // (anon)

struct CApiTest
{
    // The pipeline the plugins of the tests register their transforms in. It
    // lives as long as the test, so that the registrations made by one
    // method are alive for the ones that follow.
    ExtensionRegistry registry;

    // A configuration with no input, enough for the callbacks that do not
    // build a corpus.
    static Config
    plainConfig()
    {
        Config config;
        BOOST_TEST(Config::load(config, "").has_value());
        return config;
    }

    void
    testValues()
    {
        Config const config = plainConfig();
        Expected<void> const result =
            initializePlugin("values", valuesInit, config, registry);
        BOOST_TEST(result.has_value());
    }

    void
    testLogFollowsWarnAsError()
    {
        // A warning is a warning by default, and an error under
        // `warn-as-error`, like the warnings of a script.
        Config config = plainConfig();
        config.warnAsError = false;
        auto before = report::results;
        BOOST_TEST(initializePlugin("logging", loggingInit, config, registry)
            .has_value());
        BOOST_TEST(report::results.warnCount == before.warnCount + 1);
        BOOST_TEST(report::results.errorCount == before.errorCount);

        config.warnAsError = true;
        before = report::results;
        BOOST_TEST(initializePlugin("logging-strict", loggingInit, config, registry)
            .has_value());
        BOOST_TEST(report::results.warnCount == before.warnCount);
        BOOST_TEST(report::results.errorCount == before.errorCount + 1);

        // A message at the error level is an error whatever `warn-as-error`
        // says, which is what makes the run exit with a failure status. The
        // call itself succeeds.
        config.warnAsError = false;
        before = report::results;
        BOOST_TEST(initializePlugin(
            "logging-error", errorLoggingInit, config, registry).has_value());
        BOOST_TEST(report::results.warnCount == before.warnCount);
        BOOST_TEST(report::results.errorCount == before.errorCount + 1);
    }

    void
    testContextOfInitializer()
    {
        Config const config = plainConfig();
        Expected<void> const result =
            initializePlugin("context", contextInit, config, registry);
        BOOST_TEST(result.has_value());
    }

    void
    testInitializerOutcome()
    {
        Config const config = plainConfig();
        BOOST_TEST(initializePlugin("nothing", nothingInit, config, registry)
            .has_value());

        // A status other than ok fails the run, and the diagnostic names the
        // plugin and the status.
        Expected<void> const failed =
            initializePlugin("failing", failingInit, config, registry);
        BOOST_TEST(!failed.has_value());
        if (!failed)
        {
            std::string const reason = failed.error().reason();
            BOOST_TEST(reason.find("\"failing\"") != std::string::npos);
            BOOST_TEST(reason.find("plugin_error") != std::string::npos);
        }

        // A reported error fails the run whatever the status was, and the
        // first message is the one kept.
        Expected<void> const reported =
            initializePlugin("reporting", errorInit, config, registry);
        BOOST_TEST(!reported.has_value());
        if (!reported)
        {
            std::string const reason = reported.error().reason();
            BOOST_TEST(reason.find("first message") != std::string::npos);
            BOOST_TEST(reason.find("second message") == std::string::npos);
        }

        // The reason the last call to MrDocs failed is part of the
        // diagnostic when the plugin gave none.
        Expected<void> const bad =
            initializePlugin("bad-call", badCallInit, config, registry);
        BOOST_TEST(!bad.has_value());
        if (!bad)
        {
            BOOST_TEST(bad.error().reason().find("no corpus") !=
                std::string::npos);
        }

        // A call that failed earlier and was followed by calls that
        // succeeded is not blamed for a failure the plugin does not explain.
        Expected<void> const stale =
            initializePlugin(
                "stale-failure", staleFailureInit, config, registry);
        BOOST_TEST(!stale.has_value());
        if (!stale)
        {
            BOOST_TEST(stale.error().reason().find("plugin_error") !=
                std::string::npos);
            BOOST_TEST(stale.error().reason().find("last call") ==
                std::string::npos);
        }

        // Neither is a visit whose callback made calls that failed.
        Expected<void> const staleVisit = initializePlugin(
            "stale-visit-failure", staleVisitFailureInit, config, registry);
        BOOST_TEST(!staleVisit.has_value());
        if (!staleVisit)
        {
            BOOST_TEST(staleVisit.error().reason().find("plugin_error") !=
                std::string::npos);
            BOOST_TEST(staleVisit.error().reason().find("last call") ==
                std::string::npos);
        }
    }

    void
    testRegistration()
    {
        Config const config = plainConfig();
        observed = Observed();
        BOOST_TEST(initializePlugin(
            "register", registerGenerators, config, registry).has_value());

        Generator const* const generator = findGenerator("capi-test-generator");
        BOOST_TEST(generator != nullptr);
        if (generator)
        {
            BOOST_TEST(generator->id() == "capi-test-generator");
            BOOST_TEST(generator->displayName() == "C API test");
            BOOST_TEST(generator->fileExtension() == "txt");
        }
        Generator const* const bare =
            findGenerator("capi-test-generator-error");
        BOOST_TEST(bare != nullptr);
        if (bare)
        {
            // A descriptor may leave the name and the extension out.
            BOOST_TEST(bare->displayName() == "capi-test-generator-error");
            BOOST_TEST(bare->fileExtension().empty());
        }
        BOOST_TEST(observed.releases == 0);

        BOOST_TEST(initializePlugin(
            "refused", registerRefused, config, registry).has_value());
    }

    // A generator registration that fails after MrDocs accepted the
    // descriptor still gives the data back to the plugin, once, and reports
    // a host error. No transform registration fails after validation short
    // of running out of memory, so only generators have a test for it.
    void
    testRegistrationFailureReleases()
    {
        Config const config = plainConfig();
        observed = Observed();
        BOOST_TEST(initializePlugin(
            "duplicate-id", duplicateIdInit, config, registry)
            .has_value());
        BOOST_TEST(observed.releases == 1);
    }

    // MrDocs reads a descriptor up to its own size and no further: a field
    // that a later ABI version appends is unset for a plugin that predates
    // it, and a descriptor longer than the host's is not read past what the
    // host knows.
    void
    testDescriptorCopy()
    {
        struct Later
        {
            std::size_t struct_size;
            int first;
            int second;
        };
        Later const older = {offsetof(Later, second), 7, 99};
        Later const copied = copyDescriptor(older);
        BOOST_TEST(copied.first == 7);
        BOOST_TEST(copied.second == 0);
        Later const whole = {sizeof(Later), 7, 99};
        BOOST_TEST(copyDescriptor(whole).second == 99);
        struct Newer
        {
            Later known;
            int unknown;
        };
        Newer const newer = {{sizeof(Newer), 7, 99}, 5};
        Later const clamped =
            copyDescriptor(reinterpret_cast<Later const&>(newer));
        BOOST_TEST(clamped.struct_size == sizeof(Newer));
        BOOST_TEST(clamped.second == 99);

        Config const config = plainConfig();
        ExtensionRegistry other;
        BOOST_TEST(initializePlugin(
            "larger-descriptor", largerDescriptorInit, config, other)
            .has_value());
    }

    // Giving the data back runs `release` once for each registration still
    // alive, whether it comes first from `releasePlugins` or later from the
    // destruction of the registry, and the data of a plugin can delete a
    // reference with no environment. The generators and transforms registered
    // by the other tests must not run afterwards, so this one runs last.
    void
    testRelease()
    {
        Config const config = plainConfig();
        observed = Observed();
        BOOST_TEST(initializePlugin(
            "reference", referenceInit, config, registry).has_value());
        BOOST_TEST(observed.referencesDeleted == 0);
        releasePlugins();
        BOOST_TEST(observed.releases == 5);
        BOOST_TEST(observed.referencesDeleted == 1);
        releasePlugins();
        BOOST_TEST(observed.releases == 5);
        BOOST_TEST(observed.referencesDeleted == 1);
    }

    void
    testGeneratorAndTransformOverCorpus()
    {
        ScopedTempDirectory src("mrdocs-capi-src");
        BOOST_TEST(src);
        std::string const srcDir(src.path());
        writeFile(files::appendPath(srcDir, "input.cpp"),
            "struct Widget {};\n");
        std::string const configPath = files::appendPath(srcDir, "mrdocs.yml");
        writeFile(configPath,
            "source-root: " + srcDir + "\n"
            "addons: " + srcDir + "\n"
            "output: " + files::appendPath(srcDir, "out") + "\n"
            "input:\n  - " + srcDir + "\n"
            "generator-options:\n"
            "  capi-test-generator:\n"
            "    answer: 42\n"
            "transform-options:\n"
            "  capi-test-transform:\n"
            "    answer: 43\n");
        ReferenceDirectories dirs;
        dirs.cwd = srcDir;
        dirs.mrdocsRoot = srcDir;
        Config config;
        BOOST_TEST(Config::load_file(config, configPath, dirs).has_value());
        Expected<Corpus> corpus = Corpus::build(config);
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }

        // The generator reads the corpus and its own parameters, and finds
        // out where it writes.
        observed = Observed();
        Generator const* const generator = findGenerator("capi-test-generator");
        BOOST_TEST(generator != nullptr);
        if (!generator)
        {
            return;
        }
        BOOST_TEST(generator->build(*corpus, config).has_value());
        BOOST_TEST(observed.answer == 42);
        BOOST_TEST(observed.sourceRootStored);
        BOOST_TEST(std::filesystem::path(observed.sourceRoot).filename() ==
            std::filesystem::path(srcDir).filename());
        BOOST_TEST(observed.foundWidget);
        BOOST_TEST(observed.lookedUpWidget);
        BOOST_TEST(observed.readOnlyRefused);
        BOOST_TEST(observed.missingRefused);
        BOOST_TEST(observed.outputDir ==
            files::normalizePath(files::appendPath(srcDir, "out")));
        BOOST_TEST(std::filesystem::is_directory(
            std::filesystem::path(observed.outputDir)));
        bool sawWidget = false;
        for (std::string const& name : observed.names)
        {
            sawWidget = sawWidget || name == "Widget";
        }
        BOOST_TEST(sawWidget);

        // An `output` that names a file is for the single-page generators
        // of MrDocs. A plugin generator is refused by name, and no
        // directory is made where the file would go. A path names a file
        // when it exists and is not a directory, or when the run is
        // single-page and the path, which does not exist yet, ends in an
        // extension. A directory whose name has a dot, `v1.2`, is still a
        // directory in a multipage run, the default, as it is for the
        // multipage generators MrDocs ships.
        auto const buildWith = [&](
            std::string const& name,
            std::string const& output,
            bool multipage) -> Expected<void>
        {
            std::string const path = files::appendPath(srcDir, name);
            writeFile(path,
                "source-root: " + srcDir + "\n"
                "addons: " + srcDir + "\n"
                "output: " + output + "\n"
                "multipage: " + (multipage ? "true" : "false") + "\n"
                "input:\n  - " + srcDir + "\n");
            Config runConfig;
            BOOST_TEST(Config::load_file(runConfig, path, dirs).has_value());
            return generator->build(*corpus, runConfig);
        };
        auto const expectRefused = [&](Expected<void> const& result)
        {
            BOOST_TEST(!result.has_value());
            if (!result)
            {
                std::string const reason = result.error().reason();
                BOOST_TEST(reason.starts_with(
                    "the generator \"capi-test-generator\" failed: "));
                BOOST_TEST(reason.find("names a file") != std::string::npos);
            }
        };
        std::string const singleOutput =
            files::appendPath(srcDir, "single/page.xml");
        expectRefused(buildWith("single.yml", singleOutput, false));
        BOOST_TEST(!std::filesystem::exists(
            std::filesystem::path(singleOutput)));
        std::string const versionedSingle =
            files::appendPath(srcDir, "single/v1.2");
        expectRefused(buildWith("single-dotted.yml", versionedSingle, false));
        BOOST_TEST(!std::filesystem::exists(
            std::filesystem::path(versionedSingle)));

        std::string const existingFile =
            files::appendPath(srcDir, "existing.xml");
        writeFile(existingFile, "<doc/>");
        expectRefused(buildWith("existing.yml", existingFile, true));
        BOOST_TEST(std::filesystem::is_regular_file(
            std::filesystem::path(existingFile)));

        std::string const versioned = files::appendPath(srcDir, "site/v1.2");
        BOOST_TEST(buildWith("versioned.yml", versioned, true).has_value());
        BOOST_TEST(std::filesystem::is_directory(
            std::filesystem::path(versioned)));
        BOOST_TEST(observed.outputDir == files::normalizePath(versioned));
        BOOST_TEST(buildWith("versioned-again.yml", versioned, false)
            .has_value());

        // A generator that reports an error stops the run with its message;
        // one that fails without a message is reported by status.
        Generator const* const failing =
            findGenerator("capi-test-generator-error");
        BOOST_TEST(failing != nullptr);
        if (failing)
        {
            Expected<void> const result = failing->build(*corpus, config);
            BOOST_TEST(!result.has_value());
            if (!result)
            {
                BOOST_TEST(result.error().reason().find("generator boom") !=
                    std::string::npos);
            }
        }
        Generator const* const silent =
            findGenerator("capi-test-generator-silent");
        BOOST_TEST(silent != nullptr);
        if (silent)
        {
            Expected<void> const result = silent->build(*corpus, config);
            BOOST_TEST(!result.has_value());
            if (!result)
            {
                BOOST_TEST(result.error().reason().find("plugin_error") !=
                    std::string::npos);
            }
        }

        // The transform changes the live symbol, and gets its own
        // parameters.
        observed = Observed();
        BOOST_TEST(registry.applyTransforms(*corpus, config).has_value());
        BOOST_TEST(observed.answer == 43);
        BOOST_TEST(observed.lookupFound);
        BOOST_TEST(observed.lookupName == "Gadget");
        BOOST_TEST(observed.lookupStale);
        BOOST_TEST(observed.lookupContext);
        BOOST_TEST(observed.lookupArguments);
        bool renamed = false;
        bool stale = false;
        for (Symbol const& symbol : *corpus)
        {
            renamed = renamed || symbol.Name == "Gadget";
            stale = stale || symbol.Name == "Widget";
        }
        BOOST_TEST(renamed);
        BOOST_TEST(!stale);

        // A transform that reports an error stops the run, and the message
        // names the transform once.
        BOOST_TEST(initializePlugin(
            "failing-transform", registerFailingTransform, config, registry)
            .has_value());
        failTransformMode = FailMode::message;
        Expected<void> const failed = registry.applyTransforms(*corpus, config);
        BOOST_TEST(!failed.has_value());
        if (!failed)
        {
            std::string const reason = failed.error().reason();
            BOOST_TEST(reason ==
                "the transform \"capi-test-transform-error\" failed: "
                "transform boom");
        }

        // One that fails without a message is reported by its status, with
        // the reason of the last failed call when there was one.
        failTransformMode = FailMode::silent;
        Expected<void> const noMessage =
            registry.applyTransforms(*corpus, config);
        BOOST_TEST(!noMessage.has_value());
        if (!noMessage)
        {
            BOOST_TEST(noMessage.error().reason() ==
                "the transform \"capi-test-transform-error\" failed: "
                "status plugin_error");
        }
        failTransformMode = FailMode::silentAfterFailedCall;
        Expected<void> const afterCall =
            registry.applyTransforms(*corpus, config);
        failTransformMode = FailMode::off;
        BOOST_TEST(!afterCall.has_value());
        if (!afterCall)
        {
            std::string const reason = afterCall.error().reason();
            BOOST_TEST(reason.starts_with(
                "the transform \"capi-test-transform-error\" failed: "
                "status plugin_error; the last call to MrDocs failed: "));
            BOOST_TEST(reason.size() > std::string(
                "the transform \"capi-test-transform-error\" failed: "
                "status plugin_error; the last call to MrDocs failed: ")
                    .size());
        }
    }

    // Plugin transforms and script transforms share one pipeline that runs
    // in registration order, each with its own `transform-options`. The
    // script renames `Widget`, so what the plugin transforms around it see
    // tells where it ran. A lookup that missed before the pipeline started
    // does not hide the new name from the script that runs after the
    // rename, which appends to it for the transforms that follow.
    void
    testPipelineOrder()
    {
        ScopedTempDirectory src("mrdocs-capi-pipeline");
        BOOST_TEST(src);
        std::string const srcDir(src.path());
        writeFile(files::appendPath(srcDir, "input.cpp"),
            "struct Widget {};\n");
        std::string const extensions = files::appendPath(srcDir, "extensions");
        BOOST_TEST(files::createDirectory(extensions).has_value());
        writeFile(files::appendPath(extensions, "rename.lua"),
            "mrdocs.register_transform(\"capi-order-script\", function(ctx)\n"
            "    for _, symbol in ipairs(ctx.corpus.symbols) do\n"
            "        if symbol.name == \"Widget\" then\n"
            "            symbol.name = \"Widget\" .. ctx.params.suffix\n"
            "        end\n"
            "    end\n"
            "end)\n");
        writeFile(files::appendPath(extensions, "zprobe.lua"),
            "mrdocs.register_transform(\"capi-order-probe\", function(ctx)\n"
            "    local found = ctx.corpus.lookup(\"WidgetScripted\")\n"
            "    if found then\n"
            "        found.name = found.name .. \"Probed\"\n"
            "    end\n"
            "end)\n");
        std::string const configPath = files::appendPath(srcDir, "mrdocs.yml");
        writeFile(configPath,
            "source-root: " + srcDir + "\n"
            "addons: " + srcDir + "\n"
            "output: " + files::appendPath(srcDir, "out") + "\n"
            "input:\n  - " + srcDir + "\n"
            "transform-options:\n"
            "  capi-order-first:\n"
            "    answer: 1\n"
            "  capi-order-script:\n"
            "    suffix: Scripted\n"
            "  capi-order-last:\n"
            "    answer: 3\n");
        ReferenceDirectories dirs;
        dirs.cwd = srcDir;
        dirs.mrdocsRoot = srcDir;
        Config config;
        BOOST_TEST(Config::load_file(config, configPath, dirs).has_value());
        Expected<Corpus> corpus = Corpus::build(config);
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }

        // A plugin that loaded before the scripts, one that loaded after.
        ExtensionRegistry pipeline;
        BOOST_TEST(initializePlugin(
            "first", registerPipelineFirst, config, pipeline).has_value());
        BOOST_TEST(pipeline.loadScripts(config).has_value());
        BOOST_TEST(initializePlugin(
            "last", registerPipelineLast, config, pipeline).has_value());

        BOOST_TEST(!corpus->lookup(SymbolID::global, "WidgetScripted")
            .has_value());
        pipelineLog.clear();
        BOOST_TEST(pipeline.applyTransforms(*corpus, config).has_value());
        BOOST_TEST(pipelineLog.size() == 2u);
        if (pipelineLog.size() == 2u)
        {
            BOOST_TEST(pipelineLog[0] == "first:Widget:1");
            BOOST_TEST(pipelineLog[1] == "last:WidgetScriptedProbed:3");
        }
        BOOST_TEST(!corpus->lookup(SymbolID::global, "WidgetScripted")
            .has_value());
        BOOST_TEST(corpus->lookup(SymbolID::global, "WidgetScriptedProbed")
            .has_value());

        // A transform without options gets an empty object.
        Config const plain = plainConfig();
        ExtensionRegistry bare;
        BOOST_TEST(initializePlugin(
            "bare", registerPipelineFirst, plain, bare).has_value());
        pipelineLog.clear();
        BOOST_TEST(bare.applyTransforms(*corpus, plain).has_value());
        BOOST_TEST(pipelineLog.size() == 1u);
        if (!pipelineLog.empty())
        {
            BOOST_TEST(pipelineLog[0] == "first:WidgetScriptedProbed:-1");
        }
    }

    // A safe string is a string that a template helper marked as already
    // escaped. It has a kind of its own, `MRDOCS_VALUE_SAFE_STRING`, and
    // `mrdocs_get_string_utf8` reads it like a string. The corpus holds none;
    // a parameter can, since the parameters are a DOM object, which the test
    // builds by hand here.
    void
    testSafeString()
    {
        ScopedTempDirectory src("mrdocs-capi-safe");
        BOOST_TEST(src);
        std::string const srcDir(src.path());
        writeFile(files::appendPath(srcDir, "input.cpp"),
            "struct Widget {};\n");
        std::string const configPath = files::appendPath(srcDir, "mrdocs.yml");
        writeFile(configPath,
            "source-root: " + srcDir + "\n"
            "addons: " + srcDir + "\n"
            "output: " + files::appendPath(srcDir, "out") + "\n"
            "input:\n  - " + srcDir + "\n");
        ReferenceDirectories dirs;
        dirs.cwd = srcDir;
        dirs.mrdocsRoot = srcDir;
        Config config;
        BOOST_TEST(Config::load_file(config, configPath, dirs).has_value());
        dom::Object options;
        options.set("page", safeString("<b>escaped</b>"));
        config.transformOptions["capi-safe-string"] = options;
        Expected<Corpus> corpus = Corpus::build(config);
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        ExtensionRegistry pipeline;
        BOOST_TEST(initializePlugin(
            "safe-string", registerSafeStringReader, config, pipeline)
            .has_value());
        pipelineLog.clear();
        BOOST_TEST(pipeline.applyTransforms(*corpus, config).has_value());
        BOOST_TEST(pipelineLog.size() == 1u);
        if (!pipelineLog.empty())
        {
            BOOST_TEST(pipelineLog[0] ==
                std::to_string(static_cast<int>(MRDOCS_VALUE_SAFE_STRING)) +
                ":<b>escaped</b>");
        }
    }

    // The kind, id, parent and inheritedFrom fields of a symbol are read-only
    // for extension scripts too, since the symbol proxy refuses them whoever
    // writes: each assignment raises an error a script can catch, and the
    // symbol is the same afterwards. Each script writes how many of the four
    // assignments it saw refused into the name of the symbol.
    void
    testScriptReadOnlyFields()
    {
        ScopedTempDirectory src("mrdocs-capi-readonly");
        BOOST_TEST(src);
        std::string const srcDir(src.path());
        writeFile(files::appendPath(srcDir, "input.cpp"),
            "struct Widget {};\nstruct Gadget {};\n");
        std::string const extensions = files::appendPath(srcDir, "extensions");
        BOOST_TEST(files::createDirectory(extensions).has_value());
        writeFile(files::appendPath(extensions, "readonly.lua"),
            "mrdocs.register_transform(\"capi-readonly-lua\", function(ctx)\n"
            "    for _, symbol in ipairs(ctx.corpus.symbols) do\n"
            "        if symbol.name == \"Widget\" then\n"
            "            local refused = 0\n"
            "            local kind = symbol.kind\n"
            "            for _, field in ipairs({\"kind\", \"id\", \"parent\", "
            "\"inheritedFrom\"}) do\n"
            "                local ok, err = pcall(function()\n"
            "                    symbol[field] = \"function\"\n"
            "                end)\n"
            "                if not ok and string.find(tostring(err), "
            "\"read%-only\") then\n"
            "                    refused = refused + 1\n"
            "                end\n"
            "            end\n"
            "            if symbol.kind == kind then\n"
            "                symbol.name = \"LuaRefused\" .. refused\n"
            "            end\n"
            "        end\n"
            "    end\n"
            "end)\n");
        writeFile(files::appendPath(extensions, "readonly.js"),
            "mrdocs.register_transform(\"capi-readonly-js\", function(ctx) {\n"
            "    for (const symbol of ctx.corpus.symbols) {\n"
            "        if (symbol.name === \"Gadget\") {\n"
            "            let refused = 0;\n"
            "            const kind = symbol.kind;\n"
            "            for (const field of [\"kind\", \"id\", \"parent\", "
            "\"inheritedFrom\"]) {\n"
            "                try {\n"
            "                    symbol[field] = \"function\";\n"
            "                } catch (err) {\n"
            "                    if (String(err).includes(\"read-only\")) {\n"
            "                        refused += 1;\n"
            "                    }\n"
            "                }\n"
            "            }\n"
            "            if (symbol.kind === kind) {\n"
            "                symbol.name = \"JsRefused\" + refused;\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "});\n");
        std::string const configPath = files::appendPath(srcDir, "mrdocs.yml");
        writeFile(configPath,
            "source-root: " + srcDir + "\n"
            "addons: " + srcDir + "\n"
            "output: " + files::appendPath(srcDir, "out") + "\n"
            "input:\n  - " + srcDir + "\n");
        ReferenceDirectories dirs;
        dirs.cwd = srcDir;
        dirs.mrdocsRoot = srcDir;
        Config config;
        BOOST_TEST(Config::load_file(config, configPath, dirs).has_value());
        Expected<Corpus> corpus = Corpus::build(config);
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        ExtensionRegistry pipeline;
        BOOST_TEST(pipeline.loadScripts(config).has_value());
        BOOST_TEST(pipeline.applyTransforms(*corpus, config).has_value());
        BOOST_TEST(corpus->lookup(SymbolID::global, "LuaRefused4")
            .has_value());
        BOOST_TEST(corpus->lookup(SymbolID::global, "JsRefused4")
            .has_value());
    }

    // The lists of symbol ids of a symbol, such as the members of a
    // namespace, can be reordered or shortened by a transform but not
    // extended, since a generator follows them and a namespace listed as its
    // own member would never end. The script tries to list the namespace
    // under itself, a record under the namespaces and a namespace twice, and
    // counts the refusals in the new name of the namespace; then it empties
    // the list, which is accepted.
    void
    testMemberListsOnlyShrink()
    {
        ScopedTempDirectory src("mrdocs-capi-members");
        BOOST_TEST(src);
        std::string const srcDir(src.path());
        writeFile(files::appendPath(srcDir, "input.cpp"),
            "namespace outer {\n"
            "namespace inner { void f(); }\n"
            "struct S {};\n"
            "}\n");
        std::string const extensions = files::appendPath(srcDir, "extensions");
        BOOST_TEST(files::createDirectory(extensions).has_value());
        writeFile(files::appendPath(extensions, "members.lua"),
            "mrdocs.register_transform(\"capi-members\", function(ctx)\n"
            "    local outer, record\n"
            "    for _, symbol in ipairs(ctx.corpus.symbols) do\n"
            "        if symbol.name == \"outer\" then outer = symbol end\n"
            "        if symbol.name == \"S\" then record = symbol end\n"
            "    end\n"
            "    local members = outer.members\n"
            "    local inner = members.namespaces[1]\n"
            "    local refused = 0\n"
            "    for _, ids in ipairs({\n"
            "        { outer.id },\n"
            "        { record.id },\n"
            "        { inner, inner }\n"
            "    }) do\n"
            "        local ok, err = pcall(function()\n"
            "            members.namespaces = ids\n"
            "        end)\n"
            "        if not ok and string.find(tostring(err), "
            "\"extended\") then\n"
            "            refused = refused + 1\n"
            "        end\n"
            "    end\n"
            "    local kept = #members.namespaces\n"
            "    local emptied = pcall(function()\n"
            "        members.namespaces = {}\n"
            "    end)\n"
            "    outer.name = \"Outer\" .. refused .. \"Kept\" .. kept .. "
            "(emptied and \"Emptied\" or \"Stuck\")\n"
            "end)\n");
        std::string const configPath = files::appendPath(srcDir, "mrdocs.yml");
        writeFile(configPath,
            "source-root: " + srcDir + "\n"
            "addons: " + srcDir + "\n"
            "output: " + files::appendPath(srcDir, "out") + "\n"
            "input:\n  - " + srcDir + "\n");
        ReferenceDirectories dirs;
        dirs.cwd = srcDir;
        dirs.mrdocsRoot = srcDir;
        Config config;
        BOOST_TEST(Config::load_file(config, configPath, dirs).has_value());
        Expected<Corpus> corpus = Corpus::build(config);
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        ExtensionRegistry pipeline;
        BOOST_TEST(pipeline.loadScripts(config).has_value());
        BOOST_TEST(pipeline.applyTransforms(*corpus, config).has_value());
        BOOST_TEST(corpus->lookup(SymbolID::global, "Outer3Kept1Emptied")
            .has_value());
    }

    // Every script transform gets a corpus object of its own. The first
    // script replaces `symbols` and `lookup` of the object it was given, as
    // a script narrowing its own view would, and the scripts after it, of
    // either engine, still see every symbol and the original `lookup`. The
    // second script renames a symbol after the number of symbols it saw, the
    // third one after whether `lookup` still finds a symbol.
    void
    testScriptsDoNotShareCorpus()
    {
        ScopedTempDirectory src("mrdocs-capi-ownctx");
        BOOST_TEST(src);
        std::string const srcDir(src.path());
        writeFile(files::appendPath(srcDir, "input.cpp"),
            "struct Widget {};\nstruct Gadget {};\nstruct Gizmo {};\n");
        std::string const extensions = files::appendPath(srcDir, "extensions");
        BOOST_TEST(files::createDirectory(extensions).has_value());
        writeFile(files::appendPath(extensions, "a_narrow.lua"),
            "mrdocs.register_transform(\"capi-ownctx-narrow\", "
            "function(ctx)\n"
            "    ctx.corpus.symbols = {}\n"
            "    ctx.corpus.lookup = function() return nil end\n"
            "end)\n");
        writeFile(files::appendPath(extensions, "b_count.lua"),
            "mrdocs.register_transform(\"capi-ownctx-count\", "
            "function(ctx)\n"
            "    local count = 0\n"
            "    for _, symbol in ipairs(ctx.corpus.symbols) do\n"
            "        if symbol.name == \"Widget\" then\n"
            "            count = #ctx.corpus.symbols\n"
            "            symbol.name = \"Counted\" .. (count > 0 and "
            "\"Some\" or \"None\")\n"
            "        end\n"
            "    end\n"
            "end)\n");
        writeFile(files::appendPath(extensions, "c_lookup.js"),
            "mrdocs.register_transform(\"capi-ownctx-lookup\", "
            "function(ctx) {\n"
            "    for (const symbol of ctx.corpus.symbols) {\n"
            "        if (symbol.name === \"Gadget\") {\n"
            "            const found = ctx.corpus.lookup(\"Gizmo\");\n"
            "            symbol.name = found ? \"LookedUp\" : \"Lost\";\n"
            "        }\n"
            "    }\n"
            "});\n");
        std::string const configPath = files::appendPath(srcDir, "mrdocs.yml");
        writeFile(configPath,
            "source-root: " + srcDir + "\n"
            "addons: " + srcDir + "\n"
            "output: " + files::appendPath(srcDir, "out") + "\n"
            "input:\n  - " + srcDir + "\n");
        ReferenceDirectories dirs;
        dirs.cwd = srcDir;
        dirs.mrdocsRoot = srcDir;
        Config config;
        BOOST_TEST(Config::load_file(config, configPath, dirs).has_value());
        Expected<Corpus> corpus = Corpus::build(config);
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }
        ExtensionRegistry pipeline;
        BOOST_TEST(pipeline.loadScripts(config).has_value());
        BOOST_TEST(pipeline.applyTransforms(*corpus, config).has_value());
        BOOST_TEST(corpus->lookup(SymbolID::global, "CountedSome")
            .has_value());
        BOOST_TEST(corpus->lookup(SymbolID::global, "LookedUp").has_value());
    }

    // A plugin initializes once per process, keyed by its canonical path.
    // A later call for the same plugin, as the next run of an embedder
    // makes, does not run the entry point again and attaches the transform
    // the plugin registered to the pipeline of its registry, sharing the
    // state of the plugin with the first. The plugin's `release` runs once,
    // when the plugins are released, not when a registry is destroyed.
    void
    testInitOnce()
    {
        Config const config = plainConfig();
        ScopedTempDirectory src("mrdocs-capi-shared");
        BOOST_TEST(src);
        std::string const srcDir(src.path());
        writeFile(files::appendPath(srcDir, "input.cpp"),
            "struct Widget {};\n");
        std::string const configPath = files::appendPath(srcDir, "mrdocs.yml");
        writeFile(configPath,
            "source-root: " + srcDir + "\n"
            "addons: " + srcDir + "\n"
            "output: " + files::appendPath(srcDir, "out") + "\n"
            "input:\n  - " + srcDir + "\n");
        ReferenceDirectories dirs;
        dirs.cwd = srcDir;
        dirs.mrdocsRoot = srcDir;
        Config runConfig;
        BOOST_TEST(Config::load_file(runConfig, configPath, dirs).has_value());
        Expected<Corpus> corpus = Corpus::build(runConfig);
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }

        // The library exists, and a link to its directory and a link to
        // the file reach it under other names. A file system that cannot
        // make links leaves those two out.
        std::string const path = files::appendPath(srcDir, "once.so");
        writeFile(path, "");
        std::string const sameFile =
            files::appendPath(files::appendPath(srcDir, "."), "once.so");
        std::string const linkedDir = files::appendPath(srcDir, "linked");
        std::string const linkedFile = files::appendPath(srcDir, "once-link.so");
        std::error_code dirLinkEc;
        std::error_code fileLinkEc;
        std::filesystem::create_directory_symlink(
            std::filesystem::path(srcDir), std::filesystem::path(linkedDir),
            dirLinkEc);
        std::filesystem::create_symlink(
            std::filesystem::path(path), std::filesystem::path(linkedFile),
            fileLinkEc);
        int const before = observed.releases;
        initCount = 0;
        pipelineLog.clear();
        {
            ExtensionRegistry first;
            ExtensionRegistry second;
            BOOST_TEST(initializePlugin(
                path, registerSharedLogging, config, first).has_value());
            BOOST_TEST(initializePlugin(
                sameFile, registerSharedLogging, config, second).has_value());
            BOOST_TEST(initCount == 1);
            BOOST_TEST(first.applyTransforms(*corpus, runConfig).has_value());
            BOOST_TEST(second.applyTransforms(*corpus, runConfig).has_value());
            BOOST_TEST(pipelineLog.size() == 2u);

            // Loading the plugin again into a registry that already holds
            // its transform adds nothing: the transform runs once.
            BOOST_TEST(initializePlugin(
                sameFile, registerSharedLogging, config, first).has_value());
            BOOST_TEST(initCount == 1);
            pipelineLog.clear();
            BOOST_TEST(first.applyTransforms(*corpus, runConfig).has_value());
            BOOST_TEST(pipelineLog.size() == 1u);

            // The same library reached through a link is the same plugin.
            if (!dirLinkEc)
            {
                ExtensionRegistry viaDirectory;
                BOOST_TEST(initializePlugin(
                    files::appendPath(linkedDir, "once.so"),
                    registerSharedLogging, config, viaDirectory).has_value());
                BOOST_TEST(initCount == 1);
                pipelineLog.clear();
                BOOST_TEST(viaDirectory.applyTransforms(*corpus, runConfig)
                    .has_value());
                BOOST_TEST(pipelineLog.size() == 1u);
            }
            if (!fileLinkEc)
            {
                ExtensionRegistry viaFile;
                BOOST_TEST(initializePlugin(
                    linkedFile, registerSharedLogging, config, viaFile)
                    .has_value());
                BOOST_TEST(initCount == 1);
            }
        }
        BOOST_TEST(observed.releases == before);
    }

    // Assigning a registry over one that loaded a JavaScript extension
    // releases that registry's transforms before its script engine, and the
    // assigned registry runs the transforms it was given.
    void
    testMoveAssignOverScripts()
    {
        ScopedTempDirectory src("mrdocs-capi-assign");
        BOOST_TEST(src);
        std::string const srcDir(src.path());
        writeFile(files::appendPath(srcDir, "input.cpp"),
            "struct Widget {};\n");
        std::string const extensions = files::appendPath(srcDir, "extensions");
        BOOST_TEST(files::createDirectory(extensions).has_value());
        writeFile(files::appendPath(extensions, "noop.js"),
            "mrdocs.register_transform(\"capi-assign-script\", "
            "function(ctx) { return; });\n");
        std::string const configPath = files::appendPath(srcDir, "mrdocs.yml");
        writeFile(configPath,
            "source-root: " + srcDir + "\n"
            "addons: " + srcDir + "\n"
            "output: " + files::appendPath(srcDir, "out") + "\n"
            "input:\n  - " + srcDir + "\n");
        ReferenceDirectories dirs;
        dirs.cwd = srcDir;
        dirs.mrdocsRoot = srcDir;
        Config config;
        BOOST_TEST(Config::load_file(config, configPath, dirs).has_value());
        Expected<Corpus> corpus = Corpus::build(config);
        BOOST_TEST(corpus.has_value());
        if (!corpus)
        {
            return;
        }

        ExtensionRegistry target;
        BOOST_TEST(target.loadScripts(config).has_value());
        BOOST_TEST(target.applyTransforms(*corpus, config).has_value());
        ExtensionRegistry source;
        BOOST_TEST(initializePlugin(
            "assigned", registerPipelineFirst, config, source).has_value());
        target = std::move(source);
        pipelineLog.clear();
        BOOST_TEST(target.applyTransforms(*corpus, config).has_value());
        BOOST_TEST(pipelineLog.size() == 1u);
        BOOST_TEST(source.applyTransforms(*corpus, config).has_value());
        BOOST_TEST(pipelineLog.size() == 1u);

        // A registry assigned to itself keeps what it has.
        ExtensionRegistry& alias = target;
        target = std::move(alias);
        pipelineLog.clear();
        BOOST_TEST(target.applyTransforms(*corpus, config).has_value());
        BOOST_TEST(pipelineLog.size() == 1u);
    }

    void
    run()
    {
        testValues();
        testLogFollowsWarnAsError();
        testContextOfInitializer();
        testInitializerOutcome();
        testRegistration();
        testRegistrationFailureReleases();
        testDescriptorCopy();
        testGeneratorAndTransformOverCorpus();
        testPipelineOrder();
        testSafeString();
        testScriptReadOnlyFields();
        testMemberListsOnlyShrink();
        testScriptsDoNotShareCorpus();
        testInitOnce();
        testMoveAssignOverScripts();
        testRelease();
    }
};

TEST_SUITE(
    CApiTest,
    "clang.mrdocs.Plugin.CApi");

} // mrdocs
