//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// The host side of the plugin C API. Each `mrdocs_*` function is a thin
// wrapper over `dom::Value`, the same type extension scripts receive as
// `ctx.corpus`, `ctx.config` and `ctx.params`, so a plugin and a script read
// and write symbols through one implementation.

#define MRDOCS_PLUGIN_HOST_BUILD

#include "CApi.hpp"
#include <mrdocs/Config.hpp>
#include <mrdocs/Corpus.hpp>
#include <mrdocs/Generator.hpp>
#include <mrdocs/Metadata/DomCorpus.hpp>
#include <mrdocs/Support/DescribedToDom.hpp>
#include <mrdocs/Support/Error/Error.hpp>
#include <mrdocs/Support/Filesystem/Path.hpp>
#include <mrdocs/Support/Generator.hpp>
#include <mrdocs/Support/Report.hpp>
#include <mrdocs/Transform.hpp>
#include <mrdocs/Version.hpp>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The environment of one callback. It lives in the global namespace because
// that is where the header declares the type.
struct mrdocs_env
{
    // What the callback is doing, which decides what the plugin may ask for.
    enum class Phase
    {
        init,
        generate,
        transform
    };

    // A handle's value, and what the plugin may do with it.
    struct Entry
    {
        mrdocs::dom::Value value;
        // The value is a view: a container or function reached through the
        // corpus, the configuration or the parameters. It is valid for this
        // callback only and cannot be stored anywhere.
        bool borrowed = false;
        // Writes through the value are refused.
        bool readOnly = false;
        // The handle of the entry. Serials grow across every environment and
        // are never reused, so a released or foreign handle names no entry.
        std::uintptr_t serial = 0;
        // Where a view comes from: the entry it was read out of, and the
        // key or index it was read at. A write can move the storage a view
        // points into (a push reallocates the vector, a replaced field
        // frees the old object), so after a write the view is read again
        // from its parent, all the way up to a root that does not move. A
        // root has no parent.
        std::uintptr_t parent = 0;
        std::string key;
        std::size_t index = 0;
        bool byIndex = false;
        // The number of writes through views when the value was read.
        std::uint64_t epoch = 0;
        // The place the view names no longer holds a value of its kind.
        bool dead = false;
    };

    Phase phase;
    mrdocs::Config const& config;
    mrdocs::Corpus* mutableCorpus = nullptr;
    mrdocs::Corpus const* corpus = nullptr;
    mrdocs::dom::Object params;
    std::string outputDir;
    // The message set by `mrdocs_set_error`, if any.
    std::optional<std::string> error;
    // The reason of the last failure inside a function of the API.
    std::string lastFailure;
    // The handles of the callback, in increasing order of serial. A handle is
    // the serial of its entry, so a released or foreign handle is not found.
    std::vector<Entry> table;
    // How many writes through views the callback has made.
    std::uint64_t writes = 0;
    // The scopes the plugin has open, innermost last. A scope names the
    // length of the table when it was opened, and carries a token that is
    // never reused, so closing a scope twice, or out of order, is refused
    // instead of releasing handles that were made outside it.
    struct Scope
    {
        std::uintptr_t token = 0;
        std::size_t mark = 0;
    };
    std::vector<Scope> scopes;
    std::optional<mrdocs::dom::Value> corpusDom;
    std::optional<mrdocs::dom::Value> configDom;

    mrdocs_env(Phase phase_, mrdocs::Config const& config_)
        : phase(phase_)
        , config(config_)
    {
    }

    // Record why a function failed, and return its status.
    mrdocs_status
    fail(mrdocs_status status, std::string_view reason)
    {
        lastFailure = reason;
        return status;
    }
};

namespace mrdocs {

namespace {

static_assert(static_cast<int>(dom::Kind::Undefined) == MRDOCS_VALUE_UNDEFINED);
static_assert(static_cast<int>(dom::Kind::Null) == MRDOCS_VALUE_NULL);
static_assert(static_cast<int>(dom::Kind::Boolean) == MRDOCS_VALUE_BOOLEAN);
static_assert(static_cast<int>(dom::Kind::Integer) == MRDOCS_VALUE_INTEGER);
static_assert(static_cast<int>(dom::Kind::String) == MRDOCS_VALUE_STRING);
static_assert(static_cast<int>(dom::Kind::SafeString) == MRDOCS_VALUE_SAFE_STRING);
static_assert(static_cast<int>(dom::Kind::Array) == MRDOCS_VALUE_ARRAY);
static_assert(static_cast<int>(dom::Kind::Object) == MRDOCS_VALUE_OBJECT);
static_assert(static_cast<int>(dom::Kind::Function) == MRDOCS_VALUE_FUNCTION);

using Env = ::mrdocs_env;

std::string_view
statusName(mrdocs_status const status) noexcept
{
    switch (status)
    {
    case MRDOCS_STATUS_OK: return "ok";
    case MRDOCS_STATUS_INVALID_ARG: return "invalid_arg";
    case MRDOCS_STATUS_TYPE_MISMATCH: return "type_mismatch";
    case MRDOCS_STATUS_KEY_NOT_FOUND: return "key_not_found";
    case MRDOCS_STATUS_READ_ONLY: return "read_only";
    case MRDOCS_STATUS_PLUGIN_ERROR: return "plugin_error";
    case MRDOCS_STATUS_HOST_ERROR: return "host_error";
    }
    return "unknown";
}

// Run the body of an API function: refuse a null environment and turn an
// exception into a status, since nothing C++ may cross the boundary.
template <class F>
mrdocs_status
guard(Env* env, F&& body) noexcept
{
    if (!env)
    {
        return MRDOCS_STATUS_INVALID_ARG;
    }
    // The reason of a failure describes the last call only.
    env->lastFailure.clear();
    try
    {
        mrdocs_status const status = body();
        if (status == MRDOCS_STATUS_OK)
        {
            // A call that succeeds leaves no reason behind, even if a
            // callback it ran made calls that failed.
            env->lastFailure.clear();
        }
        return status;
    }
    catch (std::exception const& ex)
    {
        return env->fail(MRDOCS_STATUS_HOST_ERROR, ex.what());
    }
    catch (...)
    {
        return env->fail(MRDOCS_STATUS_HOST_ERROR, "unknown exception");
    }
}

// Return the entry with this serial, or null if there is none.
Env::Entry*
find(Env& env, std::uintptr_t const serial) noexcept
{
    auto const it = std::lower_bound(
        env.table.begin(), env.table.end(), serial,
        [](Env::Entry const& entry, std::uintptr_t const s)
        {
            return entry.serial < s;
        });
    if (serial == 0 || it == env.table.end() || it->serial != serial)
    {
        return nullptr;
    }
    return &*it;
}

// The entry no longer names anything.
void
kill(Env::Entry& entry) noexcept
{
    entry.dead = true;
    entry.value = dom::Value();
}

// Read a view again from its parent if a write may have moved its storage.
void
refresh(Env& env, Env::Entry& entry)
{
    if (entry.dead || !entry.borrowed || entry.parent == 0 ||
        entry.epoch == env.writes)
    {
        return;
    }
    // A parent is always older than its views, and handles are released from
    // the newest, so a view outlives its parent only if it is released with it.
    Env::Entry* const parent = find(env, entry.parent);
    if (!parent)
    {
        kill(entry);
        return;
    }
    refresh(env, *parent);
    dom::Value fresh;
    if (parent->dead)
    {
        kill(entry);
        return;
    }
    if (entry.byIndex)
    {
        if (!parent->value.isArray() ||
            entry.index >= parent->value.getArray().size())
        {
            kill(entry);
            return;
        }
        fresh = parent->value.getArray().get(entry.index);
    }
    else
    {
        if (!parent->value.isObject())
        {
            kill(entry);
            return;
        }
        fresh = parent->value.getObject().get(entry.key);
    }
    if (fresh.kind() != entry.value.kind())
    {
        kill(entry);
        return;
    }
    entry.value = std::move(fresh);
    entry.epoch = env.writes;
}

// Return the entry a handle names, or null if it names none.
Env::Entry*
lookup(Env& env, mrdocs_value const handle) noexcept
{
    Env::Entry* const entry =
        find(env, reinterpret_cast<std::uintptr_t>(handle));
    if (!entry || entry->dead)
    {
        return nullptr;
    }
    try
    {
        refresh(env, *entry);
    }
    catch (...)
    {
        kill(*entry);
    }
    return entry->dead ? nullptr : entry;
}

// Whether a value is a view of something other than itself: scalars are
// copied into the handle, while containers and functions alias their source.
bool
isReference(dom::Value const& value) noexcept
{
    return value.isArray() || value.isObject() || value.isFunction();
}

// Why a view cannot be stored in a container.
constexpr char const* viewStoreMessage =
    "a view of the corpus, the configuration or the parameters is valid "
    "during the callback only, so it cannot be stored in a container; copy "
    "the data (strings, numbers, booleans) into values of your own first";

// Where a value was read from, for a handle that is a view.
struct Origin
{
    std::uintptr_t parent = 0;
    std::string_view key;
    std::size_t index = 0;
    bool byIndex = false;
};

// Make a handle for `value`.
mrdocs_value
makeHandle(
    Env& env,
    dom::Value value,
    bool const borrowed,
    bool const readOnly,
    Origin const& origin = {})
{
    static std::atomic<std::uintptr_t> nextSerial{1};
    Env::Entry entry;
    entry.borrowed = borrowed && isReference(value);
    entry.readOnly = readOnly;
    entry.value = std::move(value);
    entry.epoch = env.writes;
    if (entry.borrowed)
    {
        entry.parent = origin.parent;
        entry.key = std::string(origin.key);
        entry.index = origin.index;
        entry.byIndex = origin.byIndex;
    }
    entry.serial = nextSerial.fetch_add(1, std::memory_order_relaxed);
    auto const handle = reinterpret_cast<mrdocs_value>(entry.serial);
    env.table.push_back(std::move(entry));
    return handle;
}

// Copy `text` out the way the header describes for strings.
mrdocs_status
copyOut(
    Env& env,
    std::string_view const text,
    char* const buffer,
    std::size_t const capacity,
    std::size_t* const length)
{
    if (!buffer && capacity != 0)
    {
        return env.fail(MRDOCS_STATUS_INVALID_ARG,
            "a null buffer needs a capacity of zero");
    }
    if (length)
    {
        *length = text.size();
    }
    if (buffer && capacity != 0)
    {
        std::size_t const count = std::min(text.size(), capacity - 1);
        std::memcpy(buffer, text.data(), count);
        buffer[count] = '\0';
    }
    return MRDOCS_STATUS_OK;
}

// Build the corpus view the first time a callback asks for it.
dom::Value const&
corpusView(Env& env)
{
    if (!env.corpusDom)
    {
        env.corpusDom = env.mutableCorpus
            ? buildCorpusDom(*env.mutableCorpus)
            : buildCorpusDom(*env.corpus);
    }
    return *env.corpusDom;
}

// Build the configuration view the first time a callback asks for it.
dom::Value const&
configView(Env& env)
{
    if (!env.configDom)
    {
        env.configDom = describedToDom(env.config);
    }
    return *env.configDom;
}

bool
hasCorpus(Env const& env) noexcept
{
    return env.mutableCorpus || env.corpus;
}

// Call a function of the corpus object that finds a symbol, and hand out
// the symbol with the access the corpus has in this callback.
mrdocs_status
findInCorpus(
    Env& env,
    std::string_view const function,
    dom::Array const& args,
    std::string_view const missing,
    mrdocs_value* const result)
{
    if (!hasCorpus(env))
    {
        return env.fail(MRDOCS_STATUS_INVALID_ARG,
            "there is no corpus while the plugin initializes");
    }
    dom::Value const callee =
        corpusView(env).getObject().get(function);
    auto const found = callee.getFunction().call(args);
    if (!found)
    {
        return env.fail(MRDOCS_STATUS_HOST_ERROR, found.error().message());
    }
    if (found->isNull() || found->isUndefined())
    {
        return env.fail(MRDOCS_STATUS_KEY_NOT_FOUND, missing);
    }
    *result = makeHandle(env, *found, true, env.mutableCorpus == nullptr);
    return MRDOCS_STATUS_OK;
}

// Return the parameters the user wrote for `id`, or an empty object.
dom::Object
optionsFor(std::map<std::string, dom::Object> const& options, std::string_view id)
{
    auto const it = options.find(std::string(id));
    return it != options.end() ? it->second : dom::Object();
}

// The smallest descriptor an ABI 1 plugin can pass: up to and including its
// last field. Later ABI versions append fields after it, so this value does
// not move.
constexpr std::size_t minGeneratorDescSize =
    offsetof(mrdocs_generator_desc, release) +
    sizeof(mrdocs_generator_desc::release);
constexpr std::size_t minTransformDescSize =
    offsetof(mrdocs_transform_desc, release) +
    sizeof(mrdocs_transform_desc::release);

// Turn what a callback left behind into the result of the call.
// `named` says whether the message starts with the name of the callee. A
// transform leaves it out because the caller already names the transform.
template <class Call>
Expected<void>
invoke(
    Env& env,
    std::string_view const what,
    std::string_view const id,
    bool const named,
    Call&& call)
{
    mrdocs_status status = MRDOCS_STATUS_PLUGIN_ERROR;
    try
    {
        status = call();
    }
    catch (std::exception const& ex)
    {
        if (!env.error)
        {
            env.error = std::string("threw an exception: ") + ex.what();
        }
    }
    catch (...)
    {
        if (!env.error)
        {
            env.error = "threw an exception";
        }
    }
    std::string const subject = named
        ? formatError("the {} \"{}\" failed", what, id).reason()
        : std::string();
    if (env.error)
    {
        return Unexpected(formatError(
            "{}{}{}", subject, named ? ": " : "", *env.error));
    }
    if (status != MRDOCS_STATUS_OK)
    {
        std::string const head = named
            ? subject + " with status "
            : std::string("status ");
        if (env.lastFailure.empty())
        {
            return Unexpected(formatError(
                "{}{}", head, statusName(status)));
        }
        return Unexpected(formatError(
            "{}{}; the last call to MrDocs failed: {}",
            head, statusName(status), env.lastFailure));
    }
    return {};
}

// The `data` of a registration, and the function that gives it back to the
// plugin. Every one that is alive is listed, so that `releasePlugins` can
// give the data back while the plugin's static objects are still alive,
// instead of leaving it to the static destruction of the registries.
class PluginData
{
    struct Live
    {
        std::mutex mutex;
        std::vector<PluginData*> list;
    };

    // Never destroyed: a registry that outlives it would otherwise touch a
    // destroyed list.
    static Live&
    live()
    {
        static Live* const instance = new Live;
        return *instance;
    }

    void* data_;
    void (*release_)(void*);
    bool released_ = false;
    bool listed_ = true;

    // Mark the data as given back, and append what the caller must call to
    // `out`. The caller holds the lock; the plugin's function runs outside it.
    void
    takeLocked(std::vector<std::pair<void (MRDOCS_PLUGIN_CALL *)(void*), void*>>& out) noexcept
    {
        if (!released_ && release_)
        {
            out.emplace_back(release_, data_);
        }
        released_ = true;
    }

    // Mark the data as given back, and return what the caller must call.
    // The plugin's function runs outside the lock.
    std::pair<void (MRDOCS_PLUGIN_CALL *)(void*), void*>
    take() noexcept
    {
        std::vector<std::pair<void (MRDOCS_PLUGIN_CALL *)(void*), void*>> out;
        {
            std::lock_guard<std::mutex> lock(live().mutex);
            takeLocked(out);
        }
        if (out.empty())
        {
            return {nullptr, nullptr};
        }
        return out.front();
    }

public:
    PluginData(void* data, void (*release)(void*))
        : data_(data)
        , release_(release)
    {
        // The data is the plugin's to give back once the host accepted it,
        // and no destructor runs for an object whose constructor throws.
        try
        {
            std::lock_guard<std::mutex> lock(live().mutex);
            live().list.push_back(this);
        }
        catch (...)
        {
            if (release_)
            {
                release_(data_);
            }
            throw;
        }
    }

    // The list entry moves with the data, so that the data stays listed
    // exactly once and `release` still runs once.
    PluginData(PluginData&& other) noexcept
        : data_(other.data_)
        , release_(other.release_)
        , released_(other.released_)
    {
        std::lock_guard<std::mutex> lock(live().mutex);
        std::replace(live().list.begin(), live().list.end(), &other, this);
        other.released_ = true;
        other.listed_ = false;
    }

    PluginData(PluginData const&) = delete;
    PluginData& operator=(PluginData const&) = delete;
    PluginData& operator=(PluginData&&) = delete;

    ~PluginData()
    {
        if (listed_)
        {
            std::lock_guard<std::mutex> lock(live().mutex);
            std::erase(live().list, this);
        }
        giveBack();
    }

    void*
    get() const noexcept
    {
        return data_;
    }

    // Call the plugin's `release` unless that was done already.
    void
    giveBack() noexcept
    {
        auto const [release, data] = take();
        if (release)
        {
            release(data);
        }
    }

    static void
    giveBackAll() noexcept
    {
        // Mark every entry as given back while the lock is held, because a
        // PluginData another thread destroys meanwhile is erased from the
        // list under the same lock. The plugin's functions run afterwards,
        // from the collected pairs, and touch no PluginData.
        std::vector<std::pair<void (MRDOCS_PLUGIN_CALL *)(void*), void*>> taken;
        {
            std::lock_guard<std::mutex> lock(live().mutex);
            taken.reserve(live().list.size());
            for (PluginData* const entry : live().list)
            {
                entry->takeLocked(taken);
            }
        }
        for (auto const& [release, data] : taken)
        {
            release(data);
        }
    }
};

// A generator a plugin registered. It owns the plugin's `data` and gives it
// back to the plugin when it is destroyed.
class CGenerator final
    : public Generator
{
    // First, so that it is destroyed last: once a descriptor is accepted,
    // the plugin's `release` runs for it whatever happens next.
    PluginData data_;
    std::string id_;
    std::string displayName_;
    std::string fileExtension_;
    mrdocs_status (MRDOCS_PLUGIN_CALL *build_)(mrdocs_env*, void*);

public:
    explicit
    CGenerator(mrdocs_generator_desc const& desc, PluginData&& data)
        : data_(std::move(data))
        , id_(desc.id)
        , displayName_(desc.display_name ? desc.display_name : desc.id)
        , fileExtension_(desc.file_extension ? desc.file_extension : "")
        , build_(desc.build)
    {
    }

    std::string_view
    id() const noexcept override
    {
        return id_;
    }

    std::string_view
    displayName() const noexcept override
    {
        return displayName_;
    }

    std::string_view
    fileExtension() const noexcept override
    {
        return fileExtension_;
    }

    Expected<void>
    build(Corpus const& corpus, Config const& config) const override
    {
        Env env(Env::Phase::generate, config);
        env.corpus = &corpus;
        env.params = optionsFor(config.generatorOptions, id_);
        env.outputDir = getGeneratorOutputPath(*this, config);
        // A plugin generator always writes into a directory, whatever the
        // name of the path: `docs/v1.2` is a directory, as it is for the
        // multipage generators MrDocs ships. What cannot be a directory is
        // a path that exists and is not one, and, in a single-page run, a
        // path that does not exist yet and looks like a file the way a
        // single-page generator reads it. Those are refused here, by name,
        // instead of becoming a directory that looks like the file.
        bool const namesFile = files::exists(env.outputDir)
            ? !files::isDirectory(env.outputDir)
            : !config.multipage && files::looksLikeFile(env.outputDir);
        if (namesFile)
        {
            return Unexpected(formatError(
                "the generator \"{}\" failed: the output \"{}\" names a "
                "file, but a plugin generator writes into a directory",
                id_, env.outputDir));
        }
        // Like the built-in generators, MrDocs creates the output directory
        // and any missing parents, so a plugin only writes into it.
        if (Expected<void> const created = files::createDirectory(env.outputDir);
            !created)
        {
            return Unexpected(formatError(
                "the generator \"{}\" failed: {}",
                id_, created.error().reason()));
        }
        return invoke(env, "generator", id_, true,
            [&] { return build_(&env, data_.get()); });
    }
};

// A transform a plugin registered.
class CTransform final
    : public Transform
{
    // First, for the reason given in CGenerator.
    PluginData data_;
    std::string id_;
    mrdocs_status (MRDOCS_PLUGIN_CALL *apply_)(mrdocs_env*, void*);

public:
    explicit
    CTransform(mrdocs_transform_desc const& desc, PluginData&& data)
        : data_(std::move(data))
        , id_(desc.id)
        , apply_(desc.apply)
    {
    }

    std::string_view
    id() const noexcept override
    {
        return id_;
    }

    Expected<void>
    apply(Corpus& corpus, Config const& config) const override
    {
        Env env(Env::Phase::transform, config);
        env.mutableCorpus = &corpus;
        env.params = optionsFor(config.transformOptions, id_);
        return invoke(env, "transform", id_, false,
            [&] { return apply_(&env, data_.get()); });
    }
};

} // (anon)

void
releasePlugins() noexcept
{
    PluginData::giveBackAll();
}

Expected<void>
initializePlugin(
    std::string_view const path,
    PluginInitFn const init,
    Config const& config)
{
    Env env(Env::Phase::init, config);
    return invoke(env, "plugin", path, true,
        [&] { return init(&env); });
}

} // mrdocs

using mrdocs::Env;
using mrdocs::guard;
using mrdocs::lookup;
using mrdocs::makeHandle;
using mrdocs::viewStoreMessage;
namespace dom = mrdocs::dom;

extern "C" {

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_register_generator(mrdocs_env* env, mrdocs_generator_desc const* desc)
{
    return guard(env, [&]
    {
        if (env->phase != Env::Phase::init)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "generators are registered while the plugin initializes");
        }
        if (!desc || desc->struct_size < mrdocs::minGeneratorDescSize)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "the generator descriptor needs an id and a build function");
        }
        mrdocs_generator_desc const accepted =
            mrdocs::copyDescriptor(*desc);
        if (!accepted.id || !*accepted.id || !accepted.build)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "the generator descriptor needs an id and a build function");
        }
        // The plugin's data is owned from here on, so `release` runs once
        // whichever step below fails.
        mrdocs::PluginData data(accepted.data, accepted.release);
        mrdocs::Expected<void> const installed = mrdocs::installGenerator(
            std::make_unique<mrdocs::CGenerator>(accepted, std::move(data)));
        if (!installed)
        {
            return env->fail(MRDOCS_STATUS_HOST_ERROR,
                installed.error().reason());
        }
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_register_transform(mrdocs_env* env, mrdocs_transform_desc const* desc)
{
    return guard(env, [&]
    {
        if (env->phase != Env::Phase::init)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "transforms are registered while the plugin initializes");
        }
        if (!desc || desc->struct_size < mrdocs::minTransformDescSize)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "the transform descriptor needs an id and an apply function");
        }
        mrdocs_transform_desc const accepted =
            mrdocs::copyDescriptor(*desc);
        if (!accepted.id || !*accepted.id || !accepted.apply)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "the transform descriptor needs an id and an apply function");
        }
        mrdocs::PluginData data(accepted.data, accepted.release);
        mrdocs::Expected<void> const installed = mrdocs::installTransform(
            std::make_unique<mrdocs::CTransform>(accepted, std::move(data)));
        if (!installed)
        {
            return env->fail(MRDOCS_STATUS_HOST_ERROR,
                installed.error().reason());
        }
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_corpus(mrdocs_env* env, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "result is null");
        }
        if (!mrdocs::hasCorpus(*env))
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "there is no corpus while the plugin initializes");
        }
        *result = makeHandle(*env, mrdocs::corpusView(*env), true,
            env->mutableCorpus == nullptr);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_config(mrdocs_env* env, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "result is null");
        }
        *result = makeHandle(*env, mrdocs::configView(*env), true, true);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_params(mrdocs_env* env, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "result is null");
        }
        *result = makeHandle(*env, dom::Value(env->params), true, true);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_output_dir(
    mrdocs_env* env, char* buffer, size_t capacity, size_t* length)
{
    return guard(env, [&]
    {
        if (env->phase != Env::Phase::generate)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "only a generator has an output directory");
        }
        return mrdocs::copyOut(*env, env->outputDir, buffer, capacity, length);
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_corpus_find(mrdocs_env* env, char const* id, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result || !id)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "id and result must not be null");
        }
        dom::Array args;
        args.push_back(dom::Value(std::string_view(id)));
        return mrdocs::findInCorpus(
            *env, "get", args, "no symbol has that id", result);
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_corpus_lookup(
    mrdocs_env* env,
    char const* name,
    char const* context_id,
    mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result || !name)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "name and result must not be null");
        }
        dom::Array args;
        args.push_back(dom::Value(std::string_view(name)));
        if (context_id)
        {
            // The function of the corpus resolves an id it cannot read to
            // the global namespace, which would hide the plugin's mistake.
            // An id the corpus does not hold, such as a stale one or one
            // from another corpus, is the same mistake.
            auto const contextId = mrdocs::fromBase58Str(context_id);
            if (!contextId ||
                (mrdocs::hasCorpus(*env) &&
                 !(env->mutableCorpus ? *env->mutableCorpus : *env->corpus)
                     .exists(*contextId)))
            {
                return env->fail(MRDOCS_STATUS_INVALID_ARG,
                    "context_id is not the id of a symbol");
            }
            args.push_back(dom::Value(std::string_view(context_id)));
        }
        return mrdocs::findInCorpus(
            *env, "lookup", args, "the name names no symbol", result);
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_kind(mrdocs_env* env, mrdocs_value value, mrdocs_value_kind* result)
{
    return guard(env, [&]
    {
        Env::Entry const* const entry = lookup(*env, value);
        if (!entry || !result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "value or result is not valid");
        }
        *result = static_cast<mrdocs_value_kind>(entry->value.kind());
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_bool(mrdocs_env* env, mrdocs_value value, bool* result)
{
    return guard(env, [&]
    {
        Env::Entry const* const entry = lookup(*env, value);
        if (!entry || !result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "value or result is not valid");
        }
        if (!entry->value.isBoolean())
        {
            return env->fail(MRDOCS_STATUS_TYPE_MISMATCH,
                "the value is not a boolean");
        }
        *result = entry->value.getBool();
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_int64(mrdocs_env* env, mrdocs_value value, int64_t* result)
{
    return guard(env, [&]
    {
        Env::Entry const* const entry = lookup(*env, value);
        if (!entry || !result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "value or result is not valid");
        }
        if (!entry->value.isInteger())
        {
            return env->fail(MRDOCS_STATUS_TYPE_MISMATCH,
                "the value is not an integer");
        }
        *result = entry->value.getInteger();
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_string_utf8(
    mrdocs_env* env,
    mrdocs_value value,
    char* buffer,
    size_t capacity,
    size_t* length)
{
    return guard(env, [&]
    {
        Env::Entry const* const entry = lookup(*env, value);
        if (!entry)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "value is not valid");
        }
        if (!entry->value.isString() && !entry->value.isSafeString())
        {
            return env->fail(MRDOCS_STATUS_TYPE_MISMATCH,
                "the value is not a string");
        }
        return mrdocs::copyOut(
            *env, entry->value.getString().get(), buffer, capacity, length);
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_object_get(
    mrdocs_env* env, mrdocs_value object, char const* key, mrdocs_value* result)
{
    return guard(env, [&]
    {
        Env::Entry const* const entry = lookup(*env, object);
        if (!entry || !key || !result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "object, key or result is not valid");
        }
        if (!entry->value.isObject())
        {
            return env->fail(MRDOCS_STATUS_TYPE_MISMATCH,
                "the value is not an object");
        }
        dom::Object const properties = entry->value.getObject();
        bool const borrowed = entry->borrowed;
        // The `$meta` of a view of the corpus is the type identity shared by
        // every symbol of a type, not a part of the symbol, so it is never
        // writable. A `$meta` in an object the plugin made is the plugin's.
        bool const readOnly = entry->readOnly ||
            (borrowed && std::string_view(key) == "$meta");
        mrdocs::Origin origin;
        origin.parent = entry->serial;
        origin.key = key;
        *result = makeHandle(
            *env, properties.get(key), borrowed, readOnly, origin);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_object_set(
    mrdocs_env* env, mrdocs_value object, char const* key, mrdocs_value value)
{
    return guard(env, [&]
    {
        Env::Entry* const target = lookup(*env, object);
        Env::Entry const* const source = lookup(*env, value);
        if (!target || !source || !key)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "object, key or value is not valid");
        }
        if (!target->value.isObject())
        {
            return env->fail(MRDOCS_STATUS_TYPE_MISMATCH,
                "the value is not an object");
        }
        if (target->readOnly)
        {
            return env->fail(MRDOCS_STATUS_READ_ONLY,
                std::string("cannot set \"") + key +
                "\": this is a read-only view");
        }
        if (source->borrowed)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, viewStoreMessage);
        }
        dom::Value const stored = source->value;
        // A write that fails can still have replaced part of the field (an
        // object is applied sub-field by sub-field), and what it replaced may
        // be what views of the symbol point into, so the write counts
        // whether or not it succeeds.
        if (target->borrowed)
        {
            ++env->writes;
        }
        try
        {
            target->value.getObject().set(
                dom::String(std::string_view(key)), stored);
        }
        catch (mrdocs::ReadOnlyFieldError const& ex)
        {
            return env->fail(MRDOCS_STATUS_READ_ONLY, ex.what());
        }
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_object_has(
    mrdocs_env* env, mrdocs_value object, char const* key, bool* result)
{
    return guard(env, [&]
    {
        Env::Entry const* const entry = lookup(*env, object);
        if (!entry || !key || !result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "object, key or result is not valid");
        }
        if (!entry->value.isObject())
        {
            return env->fail(MRDOCS_STATUS_TYPE_MISMATCH,
                "the value is not an object");
        }
        *result = entry->value.getObject().exists(key);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_object_visit(
    mrdocs_env* env, mrdocs_value object, mrdocs_visit_fn callback, void* data)
{
    return guard(env, [&]
    {
        Env::Entry const* const entry = lookup(*env, object);
        if (!entry || !callback)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "object or callback is not valid");
        }
        if (!entry->value.isObject())
        {
            return env->fail(MRDOCS_STATUS_TYPE_MISMATCH,
                "the value is not an object");
        }
        bool const borrowed = entry->borrowed;
        bool const readOnly = entry->readOnly;
        std::uintptr_t const serial = entry->serial;
        // Visit a copy of the properties, so that a callback which changes
        // the object cannot invalidate the traversal.
        std::vector<std::pair<std::string, dom::Value>> properties;
        entry->value.getObject().visit(
            [&](dom::String const& key, dom::Value const& value)
            {
                properties.emplace_back(std::string(key.get()), value);
                return true;
            });
        std::uint64_t seen = env->writes;
        // The handles made for one property are released before the next, so
        // that visiting a large object does not grow the table.
        for (auto& [name, value] : properties)
        {
            if (borrowed && seen != env->writes)
            {
                // A callback wrote through a view, which may have freed what
                // the copied values point into: read the property again. The
                // values of an object the plugin made point into no symbol,
                // so they stay what they were when the visit started.
                Env::Entry* const current =
                    lookup(*env, reinterpret_cast<mrdocs_value>(serial));
                if (!current)
                {
                    return env->fail(MRDOCS_STATUS_INVALID_ARG,
                        "the object was released by a write");
                }
                value = current->value.getObject().get(name);
                seen = env->writes;
            }
            std::size_t const mark = env->table.size();
            std::size_t const openScopes = env->scopes.size();
            mrdocs::Origin origin;
            origin.parent = serial;
            origin.key = name;
            mrdocs_value const handle = makeHandle(
                *env, value, borrowed,
                readOnly || (borrowed && name == "$meta"), origin);
            bool const keepGoing = callback(env, name.c_str(), handle, data);
            // A scope the callback opened and left open ends with it.
            if (env->scopes.size() > openScopes)
            {
                env->scopes.resize(openScopes);
            }
            if (env->table.size() > mark)
            {
                env->table.erase(
                    env->table.begin() + static_cast<std::ptrdiff_t>(mark),
                    env->table.end());
            }
            if (!keepGoing)
            {
                break;
            }
        }
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_array_length(mrdocs_env* env, mrdocs_value array, size_t* result)
{
    return guard(env, [&]
    {
        Env::Entry const* const entry = lookup(*env, array);
        if (!entry || !result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "array or result is not valid");
        }
        if (!entry->value.isArray())
        {
            return env->fail(MRDOCS_STATUS_TYPE_MISMATCH,
                "the value is not an array");
        }
        *result = entry->value.getArray().size();
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_array_get(
    mrdocs_env* env, mrdocs_value array, size_t index, mrdocs_value* result)
{
    return guard(env, [&]
    {
        Env::Entry const* const entry = lookup(*env, array);
        if (!entry || !result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "array or result is not valid");
        }
        if (!entry->value.isArray())
        {
            return env->fail(MRDOCS_STATUS_TYPE_MISMATCH,
                "the value is not an array");
        }
        dom::Array const elements = entry->value.getArray();
        if (index >= elements.size())
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "the index is past the end of the array");
        }
        bool const borrowed = entry->borrowed;
        bool const readOnly = entry->readOnly;
        mrdocs::Origin origin;
        origin.parent = entry->serial;
        origin.index = index;
        origin.byIndex = true;
        *result = makeHandle(
            *env, elements.get(index), borrowed, readOnly, origin);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_array_push(mrdocs_env* env, mrdocs_value array, mrdocs_value value)
{
    return guard(env, [&]
    {
        Env::Entry* const target = lookup(*env, array);
        Env::Entry const* const source = lookup(*env, value);
        if (!target || !source)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "array or value is not valid");
        }
        if (!target->value.isArray())
        {
            return env->fail(MRDOCS_STATUS_TYPE_MISMATCH,
                "the value is not an array");
        }
        if (target->readOnly)
        {
            return env->fail(MRDOCS_STATUS_READ_ONLY,
                "cannot push: this is a read-only view");
        }
        if (source->borrowed)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, viewStoreMessage);
        }
        dom::Value const stored = source->value;
        // The push may reallocate the elements views point at, and a push
        // that fails can still have changed the array, so it counts whether
        // or not it succeeds.
        if (target->borrowed)
        {
            ++env->writes;
        }
        target->value.getArray().push_back(stored);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_null(mrdocs_env* env, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "result is null");
        }
        *result = makeHandle(*env, dom::Value(nullptr), false, false);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_bool(mrdocs_env* env, bool value, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "result is null");
        }
        *result = makeHandle(*env, dom::Value(value), false, false);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_int64(mrdocs_env* env, int64_t value, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "result is null");
        }
        *result = makeHandle(*env, dom::Value(value), false, false);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_string(
    mrdocs_env* env, char const* text, size_t length, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result || !text)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "text and result must not be null");
        }
        std::string_view const view = length == MRDOCS_AUTO_LENGTH
            ? std::string_view(text)
            : std::string_view(text, length);
        *result = makeHandle(*env, dom::Value(dom::String(view)), false, false);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_object(mrdocs_env* env, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "result is null");
        }
        *result = makeHandle(*env, dom::Value(dom::Object()), false, false);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_array(mrdocs_env* env, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "result is null");
        }
        *result = makeHandle(*env, dom::Value(dom::Array()), false, false);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_scope_open(mrdocs_env* env, mrdocs_scope* result)
{
    return guard(env, [&]
    {
        if (!result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "result is null");
        }
        static std::atomic<std::uintptr_t> nextToken{1};
        Env::Scope scope;
        scope.token = nextToken.fetch_add(1, std::memory_order_relaxed);
        scope.mark = env->table.size();
        env->scopes.push_back(scope);
        *result = static_cast<mrdocs_scope>(scope.token);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_scope_close(mrdocs_env* env, mrdocs_scope scope)
{
    return guard(env, [&]
    {
        if (env->scopes.empty() ||
            env->scopes.back().token != static_cast<std::uintptr_t>(scope))
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "the scope is not the innermost open scope: it was closed "
                "already, or an inner scope is still open");
        }
        std::size_t const mark = env->scopes.back().mark;
        env->scopes.pop_back();
        if (env->table.size() > mark)
        {
            env->table.erase(
                env->table.begin() + static_cast<std::ptrdiff_t>(mark),
                env->table.end());
        }
        return MRDOCS_STATUS_OK;
    });
}

} // extern "C"

// A reference keeps the value of a handle past the callback.
struct mrdocs_ref_s
{
    mrdocs::dom::Value value;
};

extern "C" {

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_ref_create(mrdocs_env* env, mrdocs_value value, mrdocs_ref* result)
{
    return guard(env, [&]
    {
        Env::Entry const* const entry = lookup(*env, value);
        if (!entry || !result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "value or result is not valid");
        }
        if (entry->borrowed)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "a view of the corpus, the configuration or the parameters "
                "is valid during the callback only, so it cannot be kept; "
                "copy the data into values of your own first");
        }
        *result = new mrdocs_ref_s{entry->value};
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_ref_get(mrdocs_env* env, mrdocs_ref ref, mrdocs_value* result)
{
    return guard(env, [&]
    {
        if (!ref || !result)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "ref and result must not be null");
        }
        *result = makeHandle(*env, ref->value, false, false);
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_ref_delete(mrdocs_env*, mrdocs_ref ref)
{
    // Not guarded: a plugin deletes the references it keeps in `data` from
    // `release`, which runs with no environment.
    delete ref;
    return MRDOCS_STATUS_OK;
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_set_error(mrdocs_env* env, char const* message)
{
    return guard(env, [&]
    {
        if (!message)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "message is null");
        }
        if (!env->error)
        {
            env->error = message;
        }
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_log(mrdocs_env* env, mrdocs_log_level level, char const* message)
{
    return guard(env, [&]
    {
        if (!message)
        {
            return env->fail(MRDOCS_STATUS_INVALID_ARG, "message is null");
        }
        using Level = mrdocs::report::Level;
        Level mapped;
        switch (level)
        {
        case MRDOCS_LOG_TRACE: mapped = Level::trace; break;
        case MRDOCS_LOG_DEBUG: mapped = Level::debug; break;
        case MRDOCS_LOG_INFO: mapped = Level::info; break;
        case MRDOCS_LOG_WARN:
            mapped = env->config.warnAsError ? Level::error : Level::warn;
            break;
        case MRDOCS_LOG_ERROR: mapped = Level::error; break;
        default:
            return env->fail(MRDOCS_STATUS_INVALID_ARG,
                "the log level is not one of mrdocs_log_level");
        }
        mrdocs::report::log(mapped, "{}", std::string(message));
        return MRDOCS_STATUS_OK;
    });
}

mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_host_info(
    mrdocs_env* env,
    uint32_t* abi_version,
    char* release,
    size_t capacity,
    size_t* length)
{
    return guard(env, [&]
    {
        // Nothing is written unless the call succeeds, so the release string
        // is copied out first: it is the one part that can be refused.
        if (release || length)
        {
            std::string_view const text = mrdocs::project_release.empty()
                ? mrdocs::project_version
                : mrdocs::project_release;
            mrdocs_status const status =
                mrdocs::copyOut(*env, text, release, capacity, length);
            if (status != MRDOCS_STATUS_OK)
            {
                return status;
            }
        }
        if (abi_version)
        {
            *abi_version = MRDOCS_PLUGIN_ABI_VERSION;
        }
        return MRDOCS_STATUS_OK;
    });
}

} // extern "C"
