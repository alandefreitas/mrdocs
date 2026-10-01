//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A C++ wrapper over the C interface of mrdocs/plugin.h, for writing a plugin
// in C++17 or later.
//
// The wrapper is compiled entirely inside the plugin. It uses the C functions
// of mrdocs/plugin.h and nothing else from MrDocs: no MrDocs type, no MrDocs
// header, and no symbol of the MrDocs binaries, so any C++ compiler and
// standard library can build a plugin with it. This is the role
// node-addon-api plays for Node-API.
//
// What the wrapper adds to the C interface:
//
//  - Failed calls throw `Error`, so plugin code reads straight through
//    instead of checking a status after every call. The two that find a
//    symbol, `Env::find` and `Env::lookup`, return an empty optional when the
//    corpus has no such symbol, and throw for every other failure. No
//    exception leaves a callback: the functions the wrapper registers with
//    MrDocs catch what the plugin throws and turn it into the error that
//    stops the run.
//  - `Value` and its subclasses `Array` and `Object` read like the values of a
//    script, and `Object` and `Array` can be iterated.
//  - `Generator` and `Transform` are base classes for what a plugin registers.
//    Their virtual tables are compiled into the plugin, and nothing crosses
//    to MrDocs but function pointers and a pointer to the object.
//  - `MRDOCS_PLUGIN_INIT_CPP` defines the entry points of the plugin.
//
// The wrapper honors `MRDOCS_PLUGIN_ABI_TARGET`: a plugin that defines it to
// a lower ABI gets a wrapper that compiles against that ABI, and the wrapper's
// core paths use nothing but ABI 1 functions. A member that uses a function
// added after ABI 1 is declared under the same `MRDOCS_PLUGIN_ABI_TARGET`
// check as that function.
//
// The wrapper does not change the rules of the interface. A `Value` is a
// handle that is valid until the callback that obtained it returns, and a
// callback uses its handles on the thread that runs it. It does not serialize
// the callbacks either: the object a `Generator` or `Transform` derived class
// is can be asked to run on several threads at once, so a member that changes
// during a callback needs a lock of its own.

#ifndef MRDOCS_PLUGIN_HPP
#define MRDOCS_PLUGIN_HPP

#include <mrdocs/plugin.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace mrdocs::plugin {

class Env;
class Value;
class Array;
class Object;
class Generator;
class Transform;

/** The kind of a value.

    The enumerators have the values of `mrdocs_value_kind`.
*/
enum class Kind
{
    Undefined = MRDOCS_VALUE_UNDEFINED,
    Null = MRDOCS_VALUE_NULL,
    Boolean = MRDOCS_VALUE_BOOLEAN,
    Integer = MRDOCS_VALUE_INTEGER,
    String = MRDOCS_VALUE_STRING,
    SafeString = MRDOCS_VALUE_SAFE_STRING,
    Array = MRDOCS_VALUE_ARRAY,
    Object = MRDOCS_VALUE_OBJECT,
    Function = MRDOCS_VALUE_FUNCTION
};

/** The severity of a message sent to the MrDocs log.

    The enumerators have the values of `mrdocs_log_level`.
*/
enum class LogLevel
{
    Trace = MRDOCS_LOG_TRACE,
    Debug = MRDOCS_LOG_DEBUG,
    Info = MRDOCS_LOG_INFO,
    Warn = MRDOCS_LOG_WARN,
    Error = MRDOCS_LOG_ERROR
};

/** The exception thrown when a call to MrDocs does not succeed.

    The message names the function and the status it returned, followed by the
    reason when there is one. A call the wrapper refuses before it reaches
    MrDocs has the same shape, and names the wrapper function. It is the
    message of the error that stops the run when the exception leaves a
    callback.
*/
class Error : public std::runtime_error
{
public:
    Error(mrdocs_status status, std::string const& message)
        : std::runtime_error(message)
        , status_(status)
    {
    }

    /** The status the function returned. */
    mrdocs_status
    status() const noexcept
    {
        return status_;
    }

private:
    mrdocs_status status_;
};

namespace detail {

inline char const*
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

// The reason MrDocs recorded for the last call that failed, or an empty
// string. It has to be read before another call is made, which clears it.
inline std::string
lastFailure(mrdocs_env* const env)
{
    std::size_t length = 0;
    if (mrdocs_last_failure(env, nullptr, 0, &length) != MRDOCS_STATUS_OK ||
        length == 0)
    {
        return {};
    }
    std::string reason(length + 1, '\0');
    if (mrdocs_last_failure(env, reason.data(), reason.size(), &length) !=
        MRDOCS_STATUS_OK)
    {
        return {};
    }
    reason.resize(length);
    return reason;
}

// Throw if a call did not succeed. The message carries the reason MrDocs
// gives for the failure, since that is what tells the author what to change.
inline void
check(
    mrdocs_env* const env,
    mrdocs_status const status,
    char const* const function)
{
    if (status != MRDOCS_STATUS_OK)
    {
        std::string message =
            std::string(function) + " failed: " + statusName(status);
        std::string const reason = lastFailure(env);
        if (!reason.empty())
        {
            message += ": " + reason;
        }
        throw Error(status, message);
    }
}

// The error for a call the wrapper refuses before it reaches MrDocs. It has
// the shape of the error `check` throws: the function, the status and the
// reason.
inline Error
refusal(
    char const* const function,
    mrdocs_status const status,
    char const* const reason)
{
    return Error(status,
        std::string(function) + " failed: " + statusName(status) + ": " +
        reason);
}

// The key as the NUL-terminated string the C interface takes. A key with a
// NUL in it cannot be expressed there, and cutting it at the NUL would name
// another property, so it is refused.
inline std::string
cKey(std::string_view const key, char const* const function)
{
    if (key.find('\0') != std::string_view::npos)
    {
        throw refusal(function, MRDOCS_STATUS_INVALID_ARG,
            "the key contains a NUL character");
    }
    return std::string(key);
}

// Run the body of a callback the plugin registered. The exception the body
// throws becomes the error of the call, since none may reach MrDocs.
template <class F>
mrdocs_status
guard(mrdocs_env* const env, F&& body) noexcept
{
    try
    {
        body();
        return MRDOCS_STATUS_OK;
    }
    catch (std::exception const& e)
    {
        char const* const what = e.what();
        mrdocs_set_error(env,
            what && *what ? what : "an exception with no message");
    }
    catch (...)
    {
        mrdocs_set_error(env, "unexpected exception");
    }
    return MRDOCS_STATUS_PLUGIN_ERROR;
}

// Integers, but not bool, which is a boolean, nor the character types, which
// are not numbers to a plugin.
template <class T>
inline constexpr bool isIntegerType =
    std::is_integral_v<T> &&
    !std::is_same_v<T, bool> &&
    !std::is_same_v<T, char> &&
    !std::is_same_v<T, wchar_t> &&
    !std::is_same_v<T, char16_t> &&
#ifdef __cpp_char8_t
    !std::is_same_v<T, char8_t> &&
#endif
    !std::is_same_v<T, char32_t>;

template <class T>
using IsInteger = std::enable_if_t<isIntegerType<T>, int>;

// Exactly `bool`. A template is what keeps a character, an enumerator or a
// pointer from converting to a boolean and being stored as `true`.
template <class T>
using IsBool = std::enable_if_t<std::is_same_v<T, bool>, int>;

// The value as the integer MrDocs stores. An unsigned value above the range
// of the integer is refused, since storing it would change it silently.
template <class I>
std::int64_t
toInt64(I const n, char const* const function)
{
    if constexpr (std::is_signed_v<I>)
    {
        return n;
    }
    else
    {
        if constexpr (sizeof(I) >= sizeof(std::int64_t))
        {
            if (n > static_cast<std::uintmax_t>(INT64_MAX))
            {
                throw refusal(
                    function,
                    MRDOCS_STATUS_INVALID_ARG,
                    "the integer does not fit in 64 signed bits");
            }
        }
        return static_cast<std::int64_t>(n);
    }
}

template <class T>
using IsFloating = std::enable_if_t<std::is_floating_point_v<T>, int>;

// Copy a string out of MrDocs. The first call asks for the length, the
// second one copies the text.
template <class Get>
std::string
readString(mrdocs_env* const env, Get&& get, char const* const function)
{
    std::size_t length = 0;
    check(env, get(nullptr, std::size_t{}, &length), function);
    std::string text(length + 1, '\0');
    check(env, get(text.data(), text.size(), &length), function);
    text.resize(length);
    return text;
}

template <class T>
void MRDOCS_PLUGIN_CALL
release(void* const data) noexcept
{
    try
    {
        delete static_cast<T*>(data);
    }
    catch (...)
    {
    }
}

} // detail

/** A handle to a value that MrDocs owns, or that the plugin made.

    A `Value` is a small copyable handle, not the value itself: copying it
    copies the handle, and the functions that change the value below are
    `const` because they change what the handle names. It is valid until the
    callback that obtained it returns, or until the `Scope` it was made in is
    closed.

    The value behaves like a value in a script. Reading a property an object
    does not have gives `undefined` instead of failing, and a call that does
    not fit the value, such as `str()` on an integer, throws `Error`.
*/
class Value
{
public:
    /** Wrap a handle of the environment. */
    Value(Env env, mrdocs_value handle) noexcept;

    /** The environment of the callback the value belongs to. */
    inline Env
    env() const noexcept;

    /** The handle of the C interface. */
    mrdocs_value
    handle() const noexcept
    {
        return handle_;
    }

    /** The kind of the value. */
    Kind
    kind() const;

    bool
    isUndefined() const
    {
        return kind() == Kind::Undefined;
    }

    bool
    isNull() const
    {
        return kind() == Kind::Null;
    }

    bool
    isBoolean() const
    {
        return kind() == Kind::Boolean;
    }

    bool
    isInteger() const
    {
        return kind() == Kind::Integer;
    }

    /** Whether the value is a string, safe or not. */
    bool
    isString() const
    {
        Kind const k = kind();
        return k == Kind::String || k == Kind::SafeString;
    }

    bool
    isArray() const
    {
        return kind() == Kind::Array;
    }

    bool
    isObject() const
    {
        return kind() == Kind::Object;
    }

    bool
    isFunction() const
    {
        return kind() == Kind::Function;
    }

    /** The boolean, or throw if the value is not one. */
    bool
    boolean() const;

    /** The integer, or throw if the value is not one. */
    std::int64_t
    integer() const;

    /** The text of the string, or throw if the value is not a string. */
    std::string
    str() const;

    /** Whether the object has a property.

        Throws with `MRDOCS_STATUS_INVALID_ARG` if `key` contains a NUL
        character, which the C interface cannot express.
    */
    bool
    has(std::string_view key) const;

    /** The property of an object.

        A property the object does not have is `undefined`; use `has` to tell
        it from one set to `undefined`. Throws with
        `MRDOCS_STATUS_INVALID_ARG` if `key` contains a NUL character.
    */
    Value
    operator[](std::string_view key) const;

    /** An element of an array.

        Throws if `index` is negative or not less than `size()`.
    */
    template <class I, detail::IsInteger<I> = 0>
    Value
    operator[](I const index) const
    {
        if constexpr (std::is_signed_v<I>)
        {
            if (index < 0)
            {
                throw detail::refusal(
                    "Value::operator[]",
                    MRDOCS_STATUS_INVALID_ARG,
                    "the array index is negative");
            }
        }
        return element(static_cast<std::size_t>(index));
    }

    /** Set a property of an object.

        Throws with `MRDOCS_STATUS_READ_ONLY` if the object is a read-only
        view, and with `MRDOCS_STATUS_HOST_ERROR` if it does not accept the
        property or the value, and with `MRDOCS_STATUS_INVALID_ARG` if `key`
        contains a NUL character. The overloads make the value from their
        argument.
    */
    void
    set(std::string_view key, Value value) const;

    void
    set(std::string_view key, std::string_view text) const;

    void
    set(std::string_view key, char const* text) const;

    void
    set(std::string_view, std::nullptr_t) const = delete;

    template <class B, detail::IsBool<B> = 0>
    void
    set(std::string_view key, B b) const;

    template <class I, detail::IsInteger<I> = 0>
    void
    set(std::string_view key, I const n) const;

    template <class F, detail::IsFloating<F> = 0>
    void
    set(std::string_view, F) const = delete;

    /** The number of elements of an array. */
    std::size_t
    size() const;

    /** Append an element to an array.

        The overloads make the value from their argument.
    */
    void
    push(Value value) const;

    void
    push(std::string_view text) const;

    void
    push(char const* text) const;

    void
    push(std::nullptr_t) const = delete;

    template <class B, detail::IsBool<B> = 0>
    void
    push(B b) const;

    template <class I, detail::IsInteger<I> = 0>
    void
    push(I const n) const;

    template <class F, detail::IsFloating<F> = 0>
    void
    push(F) const = delete;

private:
    Value
    element(std::size_t index) const;

    mrdocs_env* env_;
    mrdocs_value handle_;
};

/** The array a `Value` holds, with iteration.

    Constructing one from a value of another kind throws.
*/
class Array : public Value
{
public:
    class Iterator
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = Value;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = Value;

        // An iterator that names no array. It makes the iterator
        // default-constructible, which the C++20 range concepts require.
        Iterator() = default;

        Iterator(Value const& array, std::size_t const index)
            : array_(array)
            , index_(index)
        {
        }

        Value
        operator*() const
        {
            return (*array_)[index_];
        }

        Iterator&
        operator++()
        {
            ++index_;
            return *this;
        }

        Iterator
        operator++(int)
        {
            Iterator const previous = *this;
            ++index_;
            return previous;
        }

        friend bool
        operator==(Iterator const& a, Iterator const& b) noexcept
        {
            return a.index_ == b.index_;
        }

        friend bool
        operator!=(Iterator const& a, Iterator const& b) noexcept
        {
            return a.index_ != b.index_;
        }

    private:
        std::optional<Value> array_;
        std::size_t index_ = 0;
    };

    explicit Array(Value value);

    Iterator
    begin() const
    {
        return Iterator(*this, 0);
    }

    Iterator
    end() const
    {
        return Iterator(*this, size());
    }

    /** Call `f` with each element.

        The handles `f` makes are released after each element, so the loop
        does not pile up handles however long the array is. This is the
        counterpart of opening a `Scope` per iteration, and it carries the
        same rule: a value made inside `f` does not outlive the call, so `f`
        copies out what it wants to keep.
    */
    template <class F>
    void
    each(F&& f) const;
};

/** The object a `Value` holds, with iteration.

    Constructing one from a value of another kind throws.
*/
class Object : public Value
{
public:
    class Iterator
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = std::pair<std::string, Value>;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = value_type;

        // An iterator that names no object, which is also the end of every
        // object.
        Iterator() = default;

        Iterator(
            Value const& object,
            std::shared_ptr<std::vector<std::string> const> keys,
            std::size_t const index)
            : object_(object)
            , keys_(std::move(keys))
            , index_(index)
        {
        }

        value_type
        operator*() const
        {
            std::string const& key = (*keys_)[index_];
            return value_type(key, (*object_)[key]);
        }

        Iterator&
        operator++()
        {
            ++index_;
            return *this;
        }

        Iterator
        operator++(int)
        {
            Iterator const previous = *this;
            ++index_;
            return previous;
        }

        friend bool
        operator==(Iterator const& a, Iterator const& b) noexcept
        {
            if (a.done() || b.done())
            {
                return a.done() && b.done();
            }
            return a.keys_ == b.keys_ && a.index_ == b.index_;
        }

        friend bool
        operator!=(Iterator const& a, Iterator const& b) noexcept
        {
            return !(a == b);
        }

    private:
        bool
        done() const noexcept
        {
            return !keys_ || index_ >= keys_->size();
        }

        std::optional<Value> object_;
        std::shared_ptr<std::vector<std::string> const> keys_;
        std::size_t index_ = 0;
    };

    explicit Object(Value value);

    /** The keys of the object, in the order the object keeps them. */
    std::vector<std::string>
    keys() const;

    /** Iterate over the properties as key and value pairs.

        The keys are those the object had when `begin` was called.
    */
    Iterator
    begin() const
    {
        return Iterator(
            *this,
            std::make_shared<std::vector<std::string> const>(keys()),
            0);
    }

    Iterator
    end() const
    {
        return Iterator(*this, nullptr, 0);
    }

    /** Call `f` with each key and value.

        `f` takes a `std::string_view` and a `Value`. It may return a `bool`,
        where false stops the visit. The value is released when `f` returns,
        and an exception `f` throws stops the visit and propagates.
    */
    template <class F>
    void
    visit(F&& f) const;
};

/** A scope of handles.

    The handles made while a `Scope` is open are released when it closes. A
    loop over a large corpus opens one per iteration. Scopes close in the
    reverse order they opened, which the destructors of local variables
    follow. A scope that was not closed explicitly closes when the object is
    destroyed.
*/
class Scope
{
public:
    explicit Scope(Env env);

    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;

    Scope(Scope&& other) noexcept
        : env_(other.env_)
        , scope_(other.scope_)
        , open_(other.open_)
    {
        other.open_ = false;
    }

    ~Scope()
    {
        if (open_)
        {
            mrdocs_scope_close(env_, scope_);
        }
    }

    /** Close the scope now, and throw if that fails. */
    void
    close()
    {
        if (open_)
        {
            open_ = false;
            detail::check(
                env_, mrdocs_scope_close(env_, scope_), "mrdocs_scope_close");
        }
    }

private:
    mrdocs_env* env_;
    mrdocs_scope scope_ = 0;
    bool open_ = false;
};

/** A reference that keeps a value alive across callbacks.

    A reference holds a value the plugin made, not a view of the corpus, the
    configuration or the parameters. Use `get` in a later callback to get a
    handle that is valid there. The reference is deleted when the object is
    destroyed, which is fine to do in the destructor of a `Generator` or
    `Transform`, where no environment exists.
*/
class Ref
{
public:
    Ref() noexcept = default;

    /** Make a reference to a value. */
    explicit Ref(Value value);

    Ref(Ref const&) = delete;
    Ref& operator=(Ref const&) = delete;

    Ref(Ref&& other) noexcept
        : ref_(std::exchange(other.ref_, nullptr))
    {
    }

    Ref&
    operator=(Ref&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            ref_ = std::exchange(other.ref_, nullptr);
        }
        return *this;
    }

    ~Ref()
    {
        reset();
    }

    /** Delete the reference, if there is one. */
    void
    reset() noexcept
    {
        if (ref_)
        {
            mrdocs_ref_delete(nullptr, ref_);
            ref_ = nullptr;
        }
    }

    explicit
    operator bool() const noexcept
    {
        return ref_ != nullptr;
    }

    /** The value the reference holds, as a handle of the current callback. */
    Value
    get(Env env) const;

private:
    mrdocs_ref ref_ = nullptr;
};

/** Facts about the MrDocs that loaded the plugin. */
struct HostInfo
{
    /** The newest ABI version the host provides. */
    std::uint32_t abiVersion = 0;
    /** The release of MrDocs. */
    std::string release;
};

/** The base class of a generator a plugin registers.

    The object lives as long as the registration, which is until MrDocs
    drops the generator after the run.
*/
class Generator
{
public:
    /** Describe the generator.

        @param id The id the generator is selected by.
        @param displayName A name for people.
        @param fileExtension The extension of the files it writes, in lower
        case and without the period.
    */
    Generator(
        std::string id, std::string displayName, std::string fileExtension)
        : id_(std::move(id))
        , displayName_(std::move(displayName))
        , fileExtension_(std::move(fileExtension))
    {
    }

    Generator(Generator const&) = delete;
    Generator& operator=(Generator const&) = delete;

    virtual ~Generator() = default;

    std::string const&
    id() const noexcept
    {
        return id_;
    }

    std::string const&
    displayName() const noexcept
    {
        return displayName_;
    }

    std::string const&
    fileExtension() const noexcept
    {
        return fileExtension_;
    }

    /** Write the documentation.

        The corpus of the environment is read-only. An exception stops the
        run with its message as the error.
    */
    virtual void
    build(Env env) = 0;

private:
    std::string id_;
    std::string displayName_;
    std::string fileExtension_;
};

/** The base class of a corpus transform a plugin registers.

    The object lives as long as the registration, which is until MrDocs
    drops the transform after the run.
*/
class Transform
{
public:
    /** Describe the transform.

        @param id The id the parameters of the transform are keyed by in
        `transform-options`.
    */
    explicit Transform(std::string id)
        : id_(std::move(id))
    {
    }

    Transform(Transform const&) = delete;
    Transform& operator=(Transform const&) = delete;

    virtual ~Transform() = default;

    std::string const&
    id() const noexcept
    {
        return id_;
    }

    /** Change the corpus.

        The corpus of the environment is writable. An exception stops the run
        with its message as the error.
    */
    virtual void
    apply(Env env) = 0;

private:
    std::string id_;
};

/** The environment of one callback.

    It is a copyable handle. A plugin never keeps one past the callback it
    received it in.
*/
class Env
{
public:
    explicit Env(mrdocs_env* const raw) noexcept
        : raw_(raw)
    {
    }

    /** The environment of the C interface. */
    mrdocs_env*
    raw() const noexcept
    {
        return raw_;
    }

    /** The corpus: writable in a transform, read-only in a generator. */
    Value
    corpus() const
    {
        mrdocs_value result = nullptr;
        detail::check(
            raw_, mrdocs_get_corpus(raw_, &result), "mrdocs_get_corpus");
        return Value(*this, result);
    }

    /** The configuration, which is read-only. */
    Value
    config() const
    {
        mrdocs_value result = nullptr;
        detail::check(
            raw_, mrdocs_get_config(raw_, &result), "mrdocs_get_config");
        return Value(*this, result);
    }

    /** The parameters of the callback, which are read-only. */
    Value
    params() const
    {
        mrdocs_value result = nullptr;
        detail::check(
            raw_, mrdocs_get_params(raw_, &result), "mrdocs_get_params");
        return Value(*this, result);
    }

    /** The absolute path of the directory a generator writes into, in UTF-8.

        MrDocs has created the directory before it calls the generator. Use
        @ref outputPath to open files in it: a `std::filesystem::path` made
        from this string reads the bytes in the ANSI code page on Windows.
    */
    std::string
    outputDir() const
    {
        return detail::readString(
            raw_,
            [this](char* buffer, std::size_t capacity, std::size_t* length)
            {
                return mrdocs_get_output_dir(raw_, buffer, capacity, length);
            },
            "mrdocs_get_output_dir");
    }

    /** The directory a generator writes into, as a path.

        The same directory as @ref outputDir, with the UTF-8 text converted
        the way the platform needs, so that a directory such as
        `C:\Users\José\docs` opens on Windows.
    */
    std::filesystem::path
    outputPath() const
    {
        std::string const text = outputDir();
#if defined(__cpp_lib_char8_t)
        return std::filesystem::path(std::u8string(
            reinterpret_cast<char8_t const*>(text.data()), text.size()));
#else
        return std::filesystem::u8path(text);
#endif
    }

    /** Find a symbol by the id in its `id` property, if the corpus has it.

        Returns an empty optional when no symbol has that id, and throws
        `Error` for every other failure.
    */
    std::optional<Value>
    find(std::string const& id) const
    {
        mrdocs_value result = nullptr;
        mrdocs_status const status =
            mrdocs_corpus_find(raw_, id.c_str(), &result);
        if (status == MRDOCS_STATUS_KEY_NOT_FOUND)
        {
            return std::nullopt;
        }
        detail::check(raw_, status, "mrdocs_corpus_find");
        return Value(*this, result);
    }

    /** Find a symbol by its qualified name, if the corpus has one.

        Returns an empty optional when the name names no symbol, and throws
        `Error` for every other failure, including a `contextId` that is not
        the id of a symbol.

        With an empty `contextId` the name is looked up from the global
        namespace. Otherwise `contextId` is the `id` of a symbol, and the
        name is resolved the way that scope sees it.
    */
    std::optional<Value>
    lookup(std::string const& name, std::string const& contextId = {}) const
    {
        mrdocs_value result = nullptr;
        mrdocs_status const status = mrdocs_corpus_lookup(
            raw_,
            name.c_str(),
            contextId.empty() ? nullptr : contextId.c_str(),
            &result);
        if (status == MRDOCS_STATUS_KEY_NOT_FOUND)
        {
            return std::nullopt;
        }
        detail::check(raw_, status, "mrdocs_corpus_lookup");
        return Value(*this, result);
    }

    /** Make a `null`. */
    Value
    null() const
    {
        mrdocs_value result = nullptr;
        detail::check(
            raw_, mrdocs_create_null(raw_, &result), "mrdocs_create_null");
        return Value(*this, result);
    }

    /** Make a boolean. */
    Value
    boolean(bool const b) const
    {
        mrdocs_value result = nullptr;
        detail::check(
            raw_, mrdocs_create_bool(raw_, b, &result), "mrdocs_create_bool");
        return Value(*this, result);
    }

    /** Make an integer. */
    Value
    integer(std::int64_t const n) const
    {
        mrdocs_value result = nullptr;
        detail::check(
            raw_, mrdocs_create_int64(raw_, n, &result), "mrdocs_create_int64");
        return Value(*this, result);
    }

    /** Make a string from UTF-8 text. */
    Value
    string(std::string_view const text) const
    {
        mrdocs_value result = nullptr;
        detail::check(
            raw_,
            mrdocs_create_string(
                raw_, text.data() ? text.data() : "", text.size(), &result),
            "mrdocs_create_string");
        return Value(*this, result);
    }

    /** Make an empty object. */
    Value
    object() const
    {
        mrdocs_value result = nullptr;
        detail::check(
            raw_, mrdocs_create_object(raw_, &result), "mrdocs_create_object");
        return Value(*this, result);
    }

    /** Make an empty array. */
    Value
    array() const
    {
        mrdocs_value result = nullptr;
        detail::check(
            raw_, mrdocs_create_array(raw_, &result), "mrdocs_create_array");
        return Value(*this, result);
    }

    /** Report an error.

        The message becomes the error that stops the run, whatever the
        callback goes on to do. Only the first message of a callback is kept.
        Throwing an exception has the same effect and is usually simpler.
    */
    void
    setError(std::string const& message) const
    {
        detail::check(
            raw_, mrdocs_set_error(raw_, message.c_str()), "mrdocs_set_error");
    }

    /** Write a message to the MrDocs log. */
    void
    log(LogLevel const level, std::string const& message) const
    {
        detail::check(
            raw_,
            mrdocs_log(
                raw_, static_cast<mrdocs_log_level>(level), message.c_str()),
            "mrdocs_log");
    }

    /** Facts about the MrDocs that loaded the plugin. */
    HostInfo
    hostInfo() const
    {
        HostInfo info;
        detail::check(
            raw_,
            mrdocs_host_info(raw_, &info.abiVersion, nullptr, 0, nullptr),
            "mrdocs_host_info");
        info.release = detail::readString(
            raw_,
            [this](char* buffer, std::size_t capacity, std::size_t* length)
            {
                return mrdocs_host_info(
                    raw_, nullptr, buffer, capacity, length);
            },
            "mrdocs_host_info");
        return info;
    }

    /** Register a generator. Callable while the plugin initializes.

        MrDocs owns the object from the call on, and deletes it when it drops
        the generator, also when the registration fails after the descriptor
        was accepted.
    */
    void
    addGenerator(std::unique_ptr<Generator> generator) const;

    /** Register a transform. Callable while the plugin initializes.

        See @ref addGenerator for who owns the object.
    */
    void
    addTransform(std::unique_ptr<Transform> transform) const;

private:
    mrdocs_env* raw_;
};

inline
Value::Value(Env const env, mrdocs_value const handle) noexcept
    : env_(env.raw())
    , handle_(handle)
{
}

inline Env
Value::env() const noexcept
{
    return Env(env_);
}

inline Kind
Value::kind() const
{
    mrdocs_value_kind result = MRDOCS_VALUE_UNDEFINED;
    detail::check(
        env_, mrdocs_get_kind(env_, handle_, &result), "mrdocs_get_kind");
    return static_cast<Kind>(result);
}

inline bool
Value::boolean() const
{
    bool result = false;
    detail::check(
        env_, mrdocs_get_bool(env_, handle_, &result), "mrdocs_get_bool");
    return result;
}

inline std::int64_t
Value::integer() const
{
    std::int64_t result = 0;
    detail::check(
        env_, mrdocs_get_int64(env_, handle_, &result), "mrdocs_get_int64");
    return result;
}

inline std::string
Value::str() const
{
    return detail::readString(
        env_,
        [this](char* buffer, std::size_t capacity, std::size_t* length)
        {
            return mrdocs_get_string_utf8(
                env_, handle_, buffer, capacity, length);
        },
        "mrdocs_get_string_utf8");
}

inline bool
Value::has(std::string_view const key) const
{
    bool result = false;
    detail::check(
        env_,
        mrdocs_object_has(
            env_,
            handle_,
            detail::cKey(key, "Value::has").c_str(),
            &result),
        "mrdocs_object_has");
    return result;
}

inline Value
Value::operator[](std::string_view const key) const
{
    mrdocs_value result = nullptr;
    detail::check(
        env_,
        mrdocs_object_get(
            env_,
            handle_,
            detail::cKey(key, "Value::operator[]").c_str(),
            &result),
        "mrdocs_object_get");
    return Value(Env(env_), result);
}

inline Value
Value::element(std::size_t const index) const
{
    mrdocs_value result = nullptr;
    detail::check(
        env_,
        mrdocs_array_get(env_, handle_, index, &result),
        "mrdocs_array_get");
    return Value(Env(env_), result);
}

inline void
Value::set(std::string_view const key, Value const value) const
{
    detail::check(
        env_,
        mrdocs_object_set(
            env_,
            handle_,
            detail::cKey(key, "Value::set").c_str(),
            value.handle_),
        "mrdocs_object_set");
}

inline void
Value::set(std::string_view const key, std::string_view const text) const
{
    set(key, Env(env_).string(text));
}

inline void
Value::set(std::string_view const key, char const* const text) const
{
    if (!text)
    {
        throw detail::refusal(
            "Value::set", MRDOCS_STATUS_INVALID_ARG, "the text is null");
    }
    set(key, Env(env_).string(text));
}

template <class B, detail::IsBool<B>>
void
Value::set(std::string_view const key, B const b) const
{
    set(key, Env(env_).boolean(b));
}

template <class I, detail::IsInteger<I>>
void
Value::set(std::string_view const key, I const n) const
{
    set(key,
        Env(env_).integer(detail::toInt64(n, "Value::set")));
}

inline std::size_t
Value::size() const
{
    std::size_t result = 0;
    detail::check(
        env_,
        mrdocs_array_length(env_, handle_, &result),
        "mrdocs_array_length");
    return result;
}

inline void
Value::push(Value const value) const
{
    detail::check(
        env_,
        mrdocs_array_push(env_, handle_, value.handle_),
        "mrdocs_array_push");
}

inline void
Value::push(std::string_view const text) const
{
    push(Env(env_).string(text));
}

inline void
Value::push(char const* const text) const
{
    if (!text)
    {
        throw detail::refusal(
            "Value::push", MRDOCS_STATUS_INVALID_ARG, "the text is null");
    }
    push(Env(env_).string(text));
}

template <class B, detail::IsBool<B>>
void
Value::push(B const b) const
{
    push(Env(env_).boolean(b));
}

template <class I, detail::IsInteger<I>>
void
Value::push(I const n) const
{
    push(
        Env(env_).integer(detail::toInt64(n, "Value::push")));
}

inline
Array::Array(Value value)
    : Value(value)
{
    if (kind() != Kind::Array)
    {
        throw detail::refusal(
            "Array::Array",
            MRDOCS_STATUS_TYPE_MISMATCH,
            "the value is not an array");
    }
}

template <class F>
void
Array::each(F&& f) const
{
    std::size_t const count = size();
    for (std::size_t i = 0; i < count; ++i)
    {
        Scope scope(env());
        f((*this)[i]);
        scope.close();
    }
}

inline
Object::Object(Value value)
    : Value(value)
{
    if (kind() != Kind::Object)
    {
        throw detail::refusal(
            "Object::Object",
            MRDOCS_STATUS_TYPE_MISMATCH,
            "the value is not an object");
    }
}

namespace detail {

// What the callback of a visit needs to run the function of the plugin and
// to carry its exception out of the C frames.
template <class F>
struct VisitState
{
    F& f;
    Env env;
    std::exception_ptr error;
};

template <class F>
bool MRDOCS_PLUGIN_CALL
visitThunk(
    mrdocs_env*, char const* const key, mrdocs_value const value, void* data)
{
    auto& state = *static_cast<VisitState<F>*>(data);
    try
    {
        using Result = std::invoke_result_t<F&, std::string_view, Value>;
        if constexpr (std::is_void_v<Result>)
        {
            state.f(std::string_view(key), Value(state.env, value));
            return true;
        }
        else
        {
            return static_cast<bool>(
                state.f(std::string_view(key), Value(state.env, value)));
        }
    }
    catch (...)
    {
        state.error = std::current_exception();
        return false;
    }
}

} // detail

template <class F>
void
Object::visit(F&& f) const
{
    detail::VisitState<std::remove_reference_t<F>> state{f, env(), nullptr};
    mrdocs_status const status = mrdocs_object_visit(
        env().raw(),
        handle(),
        &detail::visitThunk<std::remove_reference_t<F>>,
        &state);
    if (state.error)
    {
        std::rethrow_exception(state.error);
    }
    detail::check(env().raw(), status, "mrdocs_object_visit");
}

inline std::vector<std::string>
Object::keys() const
{
    std::vector<std::string> result;
    visit([&](std::string_view key, Value)
    {
        result.emplace_back(key);
    });
    return result;
}

inline
Scope::Scope(Env const env)
    : env_(env.raw())
{
    detail::check(
        env_, mrdocs_scope_open(env_, &scope_), "mrdocs_scope_open");
    open_ = true;
}

inline
Ref::Ref(Value const value)
{
    detail::check(
        value.env().raw(),
        mrdocs_ref_create(value.env().raw(), value.handle(), &ref_),
        "mrdocs_ref_create");
}

inline Value
Ref::get(Env const env) const
{
    mrdocs_value result = nullptr;
    detail::check(
        env.raw(), mrdocs_ref_get(env.raw(), ref_, &result), "mrdocs_ref_get");
    return Value(env, result);
}

namespace detail {

inline mrdocs_status MRDOCS_PLUGIN_CALL
buildThunk(mrdocs_env* const env, void* const data)
{
    return guard(env, [&]
    {
        static_cast<Generator*>(data)->build(Env(env));
    });
}

inline mrdocs_status MRDOCS_PLUGIN_CALL
applyThunk(mrdocs_env* const env, void* const data)
{
    return guard(env, [&]
    {
        static_cast<Transform*>(data)->apply(Env(env));
    });
}

template <class Init>
mrdocs_status
init(mrdocs_env* const env, Init&& body) noexcept
{
    return guard(env, [&]
    {
        body(Env(env));
    });
}

} // detail

inline void
Env::addGenerator(std::unique_ptr<Generator> generator) const
{
    if (!generator)
    {
        throw detail::refusal(
            "Env::addGenerator",
            MRDOCS_STATUS_INVALID_ARG,
            "the generator is null");
    }
    mrdocs_generator_desc desc = {};
    desc.struct_size = sizeof(desc);
    desc.id = generator->id().c_str();
    desc.display_name = generator->displayName().c_str();
    desc.file_extension = generator->fileExtension().c_str();
    desc.build = &detail::buildThunk;
    desc.data = generator.get();
    desc.release = &detail::release<Generator>;
    mrdocs_status const status = mrdocs_register_generator(raw_, &desc);
    if (status != MRDOCS_STATUS_INVALID_ARG)
    {
        // MrDocs owns the object now and deletes it through `release`.
        static_cast<void>(generator.release());
    }
    detail::check(raw_, status, "mrdocs_register_generator");
}

inline void
Env::addTransform(std::unique_ptr<Transform> transform) const
{
    if (!transform)
    {
        throw detail::refusal(
            "Env::addTransform",
            MRDOCS_STATUS_INVALID_ARG,
            "the transform is null");
    }
    mrdocs_transform_desc desc = {};
    desc.struct_size = sizeof(desc);
    desc.id = transform->id().c_str();
    desc.apply = &detail::applyThunk;
    desc.data = transform.get();
    desc.release = &detail::release<Transform>;
    mrdocs_status const status = mrdocs_register_transform(raw_, &desc);
    if (status != MRDOCS_STATUS_INVALID_ARG)
    {
        static_cast<void>(transform.release());
    }
    detail::check(raw_, status, "mrdocs_register_transform");
}

} // mrdocs::plugin

/** Define the two functions a plugin exports, with a C++ body.

    This is the C++ counterpart of `MRDOCS_PLUGIN_INIT`. The braces that
    follow the macro are the body of the entry point, which receives the
    environment of the call as a `mrdocs::plugin::Env` named by the argument
    and registers what the plugin provides:

        MRDOCS_PLUGIN_INIT_CPP(env)
        {
            env.addGenerator(std::make_unique<MyGenerator>());
        }

    The body returns nothing. An exception it throws becomes the error that
    stops the run. See `MRDOCS_PLUGIN_INIT` for when MrDocs calls the body.
*/
#define MRDOCS_PLUGIN_INIT_CPP(env_)                                        \
    static void mrdocs_plugin_init_cpp(::mrdocs::plugin::Env env_);         \
                                                                            \
    MRDOCS_PLUGIN_INIT(mrdocs_plugin_raw_env)                               \
    {                                                                       \
        return ::mrdocs::plugin::detail::init(                              \
            mrdocs_plugin_raw_env, &mrdocs_plugin_init_cpp);                \
    }                                                                       \
                                                                            \
    static void mrdocs_plugin_init_cpp(::mrdocs::plugin::Env env_)

#endif // MRDOCS_PLUGIN_HPP
