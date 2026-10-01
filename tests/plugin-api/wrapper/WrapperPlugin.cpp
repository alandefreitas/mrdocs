//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A plugin written with the C++ wrapper of mrdocs/plugin.hpp, which exercises
// the wrapper from the inside: values, iteration, references, and the way an
// exception that leaves a callback becomes the error of the run.
//
// The `wrapper-values` generator reads and writes values and records what
// it sees in `values.txt`, which the ctest entry compares with the file kept
// next to this one. The other generators each fail in one way, and the ctest
// entries check the diagnostic.
//
// The library is compiled as C++17 with strict warnings, since the header is
// compiled into every plugin and has to stay clean under what a plugin may
// enable.

#include <mrdocs/plugin.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace mrdocs::plugin;

// What the transform leaves for the generator, which runs in a later
// callback: a reference keeps a value the plugin made alive in between.
struct Shared
{
    Ref ref;
};

// Characters are not numbers to a plugin, and neither are booleans.
static_assert(!detail::isIntegerType<char>);
static_assert(!detail::isIntegerType<wchar_t>);
static_assert(!detail::isIntegerType<char16_t>);
static_assert(!detail::isIntegerType<char32_t>);
static_assert(!detail::isIntegerType<bool>);
static_assert(detail::isIntegerType<unsigned>);
static_assert(detail::isIntegerType<std::int64_t>);

// Whether `set` and `push` take an argument of type `A`. Only the types that
// name a value do: a character, an enumerator or a pointer would otherwise
// convert to a boolean and be stored as `true` without a diagnostic.
template <class A, class = void>
struct CanSet : std::false_type {};
template <class A>
struct CanSet<A, std::void_t<decltype(
    std::declval<Value const&>().set(std::string_view(), std::declval<A>()))>>
    : std::true_type {};

template <class A, class = void>
struct CanPush : std::false_type {};
template <class A>
struct CanPush<A, std::void_t<decltype(
    std::declval<Value const&>().push(std::declval<A>()))>>
    : std::true_type {};

enum Plain { PlainEnumerator };
enum class Scoped { Enumerator };

static_assert(CanSet<bool>::value && CanPush<bool>::value);
static_assert(CanSet<int>::value && CanPush<int>::value);
static_assert(CanSet<std::uint64_t>::value && CanPush<std::uint64_t>::value);
static_assert(CanSet<char const*>::value && CanPush<char const*>::value);
static_assert(CanSet<std::string>::value && CanPush<std::string>::value);
static_assert(CanSet<Value>::value && CanPush<Value>::value);
static_assert(!CanSet<char>::value && !CanPush<char>::value);
static_assert(!CanSet<wchar_t>::value && !CanPush<wchar_t>::value);
static_assert(!CanSet<char16_t>::value && !CanPush<char16_t>::value);
static_assert(!CanSet<char32_t>::value && !CanPush<char32_t>::value);
static_assert(!CanSet<Plain>::value && !CanPush<Plain>::value);
static_assert(!CanSet<Scoped>::value && !CanPush<Scoped>::value);
static_assert(!CanSet<int*>::value && !CanPush<int*>::value);
static_assert(!CanSet<double>::value && !CanPush<double>::value);
static_assert(!CanSet<std::nullptr_t>::value && !CanPush<std::nullptr_t>::value);

// What the entry point saw when it registered a generator with an id that is
// already taken, and how often the object it handed over was destroyed.
struct Duplicate
{
    mrdocs_status status = MRDOCS_STATUS_OK;
    std::string message;
    int destroyed = 0;
};

Duplicate duplicate;

char const*
kindName(Kind const kind)
{
    switch (kind)
    {
    case Kind::Undefined: return "undefined";
    case Kind::Null: return "null";
    case Kind::Boolean: return "boolean";
    case Kind::Integer: return "integer";
    case Kind::String: return "string";
    case Kind::SafeString: return "safe_string";
    case Kind::Array: return "array";
    case Kind::Object: return "object";
    case Kind::Function: return "function";
    }
    return "unknown";
}

// Fail the callback unless a condition holds. The exception is the check:
// it reaches the diagnostic of the run, so a failed one fails the ctest entry.
void
expect(bool const condition, char const* const what)
{
    if (!condition)
    {
        throw std::logic_error(std::string("check failed: ") + what);
    }
}

// Run `f` and return the status of the Error it throws, or OK if it does not.
template <class F>
mrdocs_status
statusOf(F&& f)
{
    try
    {
        f();
    }
    catch (Error const& e)
    {
        return e.status();
    }
    return MRDOCS_STATUS_OK;
}

class Transformer : public Transform
{
public:
    explicit Transformer(std::shared_ptr<Shared> shared)
        : Transform("wrapper-transform")
        , shared_(std::move(shared))
    {
    }

    void
    apply(Env env) override
    {
        Value params = env.params();
        if (params.has("fail"))
        {
            throw std::runtime_error(params["fail"].str());
        }
        // The value is made here and read by the generator.
        Value kept = env.object();
        kept.set("tag", params["tag"].str());
        kept.set("symbols", env.corpus()["symbols"].size());
        shared_->ref = Ref(kept);
    }

private:
    std::shared_ptr<Shared> shared_;
};

class ValuesGenerator : public Generator
{
public:
    explicit ValuesGenerator(std::shared_ptr<Shared> shared)
        : Generator("wrapper-values", "Wrapper values", "txt")
        , shared_(std::move(shared))
    {
    }

    void
    build(Env env) override
    {
        std::ostringstream os;
        reportParams(env, os);
        reportMadeValues(env, os);
        reportIteration(env, os);
        reportReference(env, os);
        reportCorpus(env, os);
        reportErrors(env, os);
        reportRegistration(os);
        reportHost(env, os);

        std::filesystem::path const dir = env.outputDir();
        std::filesystem::create_directories(dir);
        // Binary mode keeps the line endings the same on every platform.
        std::ofstream file(dir / "values.txt", std::ios::binary);
        file << os.str();
        expect(static_cast<bool>(file), "values.txt was written");
    }

private:
    static void
    reportParams(Env env, std::ostream& os)
    {
        Value params = env.params();
        os << "params " << kindName(params.kind()) << '\n';
        std::vector<std::string> keys = Object(params).keys();
        std::sort(keys.begin(), keys.end());
        for (std::string const& key : keys)
        {
            os << "  " << key << ' ' << kindName(params[key].kind()) << '\n';
        }
        os << "  greeting = " << params["greeting"].str() << '\n';
        os << "  count = " << params["count"].integer() << '\n';
        os << "  nested.flag = "
           << (params["nested"]["flag"].boolean() ? "true" : "false") << '\n';
        os << "  has(count) = " << params.has("count") << '\n';
        os << "  has(missing) = " << params.has("missing") << '\n';
        os << "  missing " << kindName(params["missing"].kind()) << '\n';
    }

    static void
    reportMadeValues(Env env, std::ostream& os)
    {
        os << "made\n";
        os << "  null " << kindName(env.null().kind()) << '\n';
        os << "  boolean " << env.boolean(true).boolean() << '\n';
        os << "  integer " << env.integer(-42).integer() << '\n';
        os << "  string " << env.string("text").str() << '\n';
        // A length, not a terminator, ends the text.
        os << "  string with a NUL " << env.string(std::string_view("a\0b", 3)).str().size()
           << '\n';
        // An empty view may have no data pointer, and is still a string.
        os << "  empty string " << env.string(std::string_view()).str().size()
           << '\n';
        Value object = env.object();
        object.set("empty", std::string_view());
        object.set("text", "a literal");
        object.set("string", std::string("a std::string"));
        object.set("flag", true);
        object.set("small", 7);
        object.set("big", std::int64_t(1) << 40);
        object.set("unsigned", 8u);
        object.set("largest", std::uint64_t(INT64_MAX));
        object.set("value", env.string("a value"));
        Value array = env.array();
        array.push("first");
        array.push(std::string_view("second"));
        array.push(false);
        array.push(3);
        array.push(env.null());
        array.push(std::string_view());
        object.set("array", array);
        os << "  object.empty " << object["empty"].str().size() << '\n';
        os << "  object.text " << object["text"].str() << '\n';
        os << "  object.string " << object["string"].str() << '\n';
        os << "  object.flag " << object["flag"].boolean() << '\n';
        os << "  object.small " << object["small"].integer() << '\n';
        os << "  object.big " << object["big"].integer() << '\n';
        os << "  object.unsigned " << object["unsigned"].integer() << '\n';
        os << "  object.largest " << object["largest"].integer() << '\n';
        os << "  object.value " << object["value"].str() << '\n';
        // The array shares its elements with the object it was put into.
        object["array"].push("third");
        os << "  array.size " << object["array"].size() << '\n';
        os << "  array[0] " << object["array"][0].str() << '\n';
        os << "  array[2] " << object["array"][2].boolean() << '\n';
        os << "  array[4] " << kindName(object["array"][4].kind()) << '\n';
        os << "  array[5] " << object["array"][std::size_t{5}].str().size()
           << '\n';
        os << "  array[6] " << object["array"][std::size_t{6}].str() << '\n';
        os << "  isObject " << object.isObject() << ' ' << object.isArray()
           << '\n';
    }

    static void
    reportIteration(Env env, std::ostream& os)
    {
        Value made = env.object();
        made.set("one", 1);
        made.set("two", 2);
        made.set("three", 3);
        Value list = env.array();
        for (int i = 10; i < 14; ++i)
        {
            list.push(i);
        }

        os << "iteration\n";
        std::vector<std::string> pairs;
        for (auto const& [key, value] : Object(made))
        {
            pairs.push_back(key + "=" + std::to_string(value.integer()));
        }
        std::sort(pairs.begin(), pairs.end());
        for (std::string const& pair : pairs)
        {
            os << "  object " << pair << '\n';
        }

        std::int64_t sum = 0;
        for (Value element : Array(list))
        {
            sum += element.integer();
        }
        os << "  array sum " << sum << '\n';

        sum = 0;
        Array(list).each([&](Value element) { sum += element.integer(); });
        os << "  array each " << sum << '\n';

        // Returning false stops a visit.
        int visited = 0;
        Object(made).visit([&](std::string_view, Value)
        {
            ++visited;
            return false;
        });
        os << "  visit stopped after " << visited << '\n';

        // An exception thrown by the function leaves the visit.
        bool rethrown = false;
        try
        {
            Object(made).visit([](std::string_view, Value)
            {
                throw std::runtime_error("from the visit");
            });
        }
        catch (std::runtime_error const& e)
        {
            rethrown = std::string(e.what()) == "from the visit";
        }
        os << "  visit rethrew " << rethrown << '\n';

        // The handles made inside a scope are released with it.
        Value kept = env.string("outer");
        std::optional<Value> escaped;
        {
            Scope scope(env);
            Value inner = env.string("inner");
            expect(inner.str() == "inner", "a handle works inside its scope");
            escaped = inner;
        }
        expect(kept.str() == "outer", "a handle outlives an inner scope");
        os << "  scope ok\n";
        os << "  scope released "
           << detail::statusName(statusOf([&] { escaped->str(); })) << '\n';

        // `each` releases the handles of an element when `f` returns.
        std::vector<Value> elements;
        Array(list).each([&](Value element) { elements.push_back(element); });
        expect(elements.size() == 4, "each visits every element");
        mrdocs_status released = MRDOCS_STATUS_OK;
        for (Value const& element : elements)
        {
            released = statusOf([&] { element.integer(); });
            if (released == MRDOCS_STATUS_OK)
            {
                break;
            }
        }
        os << "  each released " << detail::statusName(released) << '\n';
    }

    void
    reportReference(Env env, std::ostream& os) const
    {
        os << "reference\n";
        Value kept = shared_->ref.get(env);
        os << "  tag " << kept["tag"].str() << '\n';
        os << "  symbols " << kept["symbols"].integer() << '\n';
    }

    static void
    reportCorpus(Env env, std::ostream& os)
    {
        os << "corpus\n";
        Value corpus = env.corpus();
        Array symbols(corpus["symbols"]);
        std::vector<std::string> names;
        symbols.each([&](Value symbol)
        {
            if (symbol["kind"].str() == "function")
            {
                names.push_back(symbol["name"].str());
            }
        });
        std::sort(names.begin(), names.end());
        for (std::string const& name : names)
        {
            os << "  function " << name << '\n';
        }

        // A symbol found by its id is the symbol the array holds.
        std::string const id = symbols[0]["id"].str();
        std::optional<Value> found = env.find(id);
        expect(found.has_value(), "a symbol is found by its id");
        expect(
            (*found)["id"].str() == id,
            "the symbol found is the one asked for");
        expect(
            !env.find("not an id").has_value(), "an unknown id finds nothing");
        os << "  find ok\n";
    }

    static void
    reportErrors(Env env, std::ostream& os)
    {
        os << "errors\n";
        Value params = env.params();
        Value symbols = env.corpus()["symbols"];
        Value text = env.string("text");
        Value number = env.integer(1);

        auto line = [&](char const* what, mrdocs_status const status)
        {
            os << "  " << what << ' ' << detail::statusName(status) << '\n';
        };
        line("str of an integer", statusOf([&] { number.str(); }));
        line("integer of a string", statusOf([&] { text.integer(); }));
        line("boolean of a string", statusOf([&] { text.boolean(); }));
        line("property of a string", statusOf([&] { text["key"]; }));
        line("element of an object", statusOf([&] { params[0]; }));
        line("element out of range", statusOf([&] { symbols[1000000]; }));
        line("negative element", statusOf([&] { symbols[-1]; }));
        line("push to an object", statusOf([&] { params.push(1); }));
        line("size of an object", statusOf([&] { params.size(); }));
        line("Array of an object", statusOf([&] { Array a(params); }));
        line("Object of an array", statusOf([&] { Object o(symbols); }));
        line("set on read-only params", statusOf([&] { params.set("a", 1); }));
        line("set on read-only config",
            statusOf([&] { env.config().set("a", 1); }));
        line("register outside init", statusOf([&]
        {
            env.addTransform(
                std::make_unique<Transformer>(std::make_shared<Shared>()));
        }));
        // A view of the corpus cannot be stored in a container of the plugin.
        Value mine = env.object();
        line("store a view", statusOf([&] { mine.set("view", symbols); }));
        // An unsigned value that does not fit is refused, not wrapped.
        line("set a huge unsigned",
            statusOf([&] { mine.set("big", std::uint64_t(1) << 63); }));
        line("push a huge unsigned",
            statusOf([&] { mine.push(std::uint64_t(1) << 63); }));
        // A null pointer that is not the literal nullptr is refused too.
        char const* absent = nullptr;
        Value list = env.array();
        line("set a null text", statusOf([&] { mine.set("t", absent); }));
        line("push a null text", statusOf([&] { list.push(absent); }));
        // A key with a NUL cannot be passed to the C interface, and cutting
        // it there would name another property.
        std::string_view const nulKey("name\0x", 6);
        line("set with a NUL key", statusOf([&] { mine.set(nulKey, 1); }));
        line("has with a NUL key", statusOf([&] { mine.has(nulKey); }));
        line("get with a NUL key", statusOf([&] { mine[nulKey]; }));
        line("no error", statusOf([&] { mine.set("fine", 1); }));

        // The Error carries the reason MrDocs recorded for the failure.
        std::string message;
        try
        {
            params.set("a", 1);
        }
        catch (Error const& e)
        {
            message = e.what();
        }
        expect(
            message.find("cannot set \"a\": this is a read-only view") !=
                std::string::npos,
            "the Error carries the reason of the host");
        os << "  reason carried "
           << (message.find("read_only") != std::string::npos) << '\n';

        // A refusal made by the wrapper has the shape of the other errors.
        std::string refusal;
        try
        {
            mine.set(nulKey, 1);
        }
        catch (Error const& e)
        {
            refusal = e.what();
        }
        os << "  refusal shape "
           << (refusal ==
               "Value::set failed: invalid_arg: "
               "the key contains a NUL character")
           << '\n';

        env.log(LogLevel::Debug, "logged by wrapper-values");
    }

    // MrDocs deletes the object of a descriptor it accepted, even when it then
    // refuses to install the generator, and the wrapper must not delete it
    // again.
    static void
    reportRegistration(std::ostream& os)
    {
        os << "registration\n";
        os << "  duplicate id " << detail::statusName(duplicate.status)
           << '\n';
        os << "  reason carried "
           << (duplicate.message.find("already exists") != std::string::npos)
           << '\n';
        os << "  destroyed " << duplicate.destroyed << '\n';
    }

    static void
    reportHost(Env env, std::ostream& os)
    {
        HostInfo const info = env.hostInfo();
        os << "host\n";
        os << "  abi " << (info.abiVersion >= 1) << '\n';
        os << "  release " << !info.release.empty() << '\n';
        os << "  output dir " << !env.outputDir().empty() << '\n';
    }

    std::shared_ptr<Shared> shared_;
};

// A generator whose id is taken. It counts its destructions.
class Clone : public Generator
{
public:
    Clone()
        : Generator("wrapper-values", "A clone of wrapper-values", "txt")
    {
    }

    ~Clone() override
    {
        ++duplicate.destroyed;
    }

    void
    build(Env) override
    {
    }
};

// A generator that fails in one way, chosen by the function it runs.
template <class F>
class Failing : public Generator
{
public:
    Failing(std::string id, F f)
        : Generator(std::move(id), "A generator that fails", "txt")
        , f_(std::move(f))
    {
    }

    void
    build(Env env) override
    {
        f_(env);
    }

private:
    F f_;
};

template <class F>
std::unique_ptr<Generator>
failing(std::string id, F f)
{
    return std::make_unique<Failing<F>>(std::move(id), std::move(f));
}

} // (anon)

MRDOCS_PLUGIN_INIT_CPP(env)
{
    auto shared = std::make_shared<Shared>();
    env.addTransform(std::make_unique<Transformer>(shared));
    env.addGenerator(std::make_unique<ValuesGenerator>(shared));

    try
    {
        env.addGenerator(std::make_unique<Clone>());
    }
    catch (Error const& e)
    {
        duplicate.status = e.status();
        duplicate.message = e.what();
    }

    env.addGenerator(failing("wrapper-throws-std", [](Env)
    {
        throw std::runtime_error("the generator gave up");
    }));
    env.addGenerator(failing("wrapper-throws-other", [](Env)
    {
        throw 42;
    }));
    env.addGenerator(failing("wrapper-throws-empty", [](Env)
    {
        throw std::runtime_error("");
    }));
    env.addGenerator(failing("wrapper-bad-call", [](Env callEnv)
    {
        // The Error of the failed call is the error of the run.
        callEnv.corpus().str();
    }));
    env.addGenerator(failing("wrapper-host-reason", [](Env callEnv)
    {
        // The diagnostic of the run says why MrDocs refused the call.
        callEnv.config().set("a", 1);
    }));
    env.addGenerator(failing("wrapper-refusal", [](Env callEnv)
    {
        // The wrapper's own refusals name the function and the status too.
        callEnv.object().set("t", static_cast<char const*>(nullptr));
    }));
    env.addGenerator(failing("wrapper-first-error", [](Env callEnv)
    {
        callEnv.setError("the first message");
        throw std::runtime_error("the second message");
    }));
    env.addGenerator(failing("wrapper-logged-error", [](Env callEnv)
    {
        // A message at the error level fails the run, though the callback
        // returns normally.
        callEnv.log(LogLevel::Error, "an error logged by the generator");
    }));
}
