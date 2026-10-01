/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* The C interface a shared library implements to extend MrDocs from outside
 * the tool.
 *
 * The design follows Node-API: every MrDocs value a plugin touches is an
 * opaque handle, every function returns a status code, one environment is
 * handed to each callback, and the interface only grows. The entry point
 * that receives the environment is the C analogue of `NAPI_MODULE_INIT`. As
 * with Clang plugins, a plugin is a shared library loaded into the tool that
 * adds to its registries; MrDocs finds the libraries in the plugins
 * directory of its addon roots.
 *
 * Nothing C++ crosses this boundary. The values behind the handles are the
 * same ones extension scripts see as `ctx.corpus`, `ctx.config` and
 * `ctx.params`, so a plugin reads and writes symbols as a Lua or JavaScript
 * extension does, and finds them with the same two lookups, by id and by
 * qualified name. A new field in a symbol is a new key rather than a change
 * to this header. Any C or C++ compiler can build a plugin.
 *
 * Every function below carries the ABI version it first appeared in. The
 * version only grows by adding functions and descriptor fields: a plugin
 * built against ABI n runs on any MrDocs that provides ABI n or later.
 */

#ifndef MRDOCS_PLUGIN_H
#define MRDOCS_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The version of this header.

   It is the number of the newest ABI the header describes, and it grows
   whenever a function or a descriptor field is added. MrDocs reports the
   value it was built with through @ref mrdocs_host_info, and accepts a plugin
   that was built for this value or a lower one.

   @since ABI 1
*/
#define MRDOCS_PLUGIN_ABI_VERSION 1u

/* The ABI version a plugin is built for.

   A plugin reports this value through `mrdocs_plugin_abi_version`, and MrDocs
   refuses to load it when its own version is lower. It defaults to
   @ref MRDOCS_PLUGIN_ABI_VERSION, which is the right choice for a plugin that
   uses what the header offers. A plugin that wants to run on older MrDocs
   releases defines it to the lowest ABI it needs before including this
   header, the way a Node-API addon defines `NAPI_VERSION`. A function added
   after ABI 1 is declared only when the target is at least the version it
   appeared in, so using it by mistake is a compile error.

   @since ABI 1
*/
#ifndef MRDOCS_PLUGIN_ABI_TARGET
#    define MRDOCS_PLUGIN_ABI_TARGET MRDOCS_PLUGIN_ABI_VERSION
#endif

#if MRDOCS_PLUGIN_ABI_TARGET < 1u || \
    MRDOCS_PLUGIN_ABI_TARGET > MRDOCS_PLUGIN_ABI_VERSION
#    error "MRDOCS_PLUGIN_ABI_TARGET must be between 1 and MRDOCS_PLUGIN_ABI_VERSION"
#endif

/* Marks a function MrDocs provides to a plugin.

   MrDocs defines the macro to export the functions from the tool; a plugin
   sees it as an import where the platform needs one (Windows) and as a
   plain declaration elsewhere, where the loader resolves the names.

   The functions come from the program that loads the plugin. The `mrdocs`
   tool provides them. On Windows a plugin imports them from a module named
   `mrdocs.exe`, so only the tool under that name can load plugins there, and
   a program that embeds MrDocs, or a renamed tool, cannot. On the other
   platforms the loading program has to export the `mrdocs_*` functions from
   its executable.
*/
#if defined(_WIN32)
#    if defined(MRDOCS_PLUGIN_HOST_BUILD)
#        define MRDOCS_PLUGIN_API __declspec(dllexport)
#    else
#        define MRDOCS_PLUGIN_API __declspec(dllimport)
#    endif
#elif defined(__GNUC__) || defined(__clang__)
#    define MRDOCS_PLUGIN_API __attribute__((__visibility__("default")))
#else
#    define MRDOCS_PLUGIN_API
#endif

/* Marks a function a plugin provides to MrDocs. */
#if defined(_WIN32)
#    define MRDOCS_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#    define MRDOCS_PLUGIN_EXPORT __attribute__((__visibility__("default")))
#else
#    define MRDOCS_PLUGIN_EXPORT
#endif

/* The calling convention of every function and callback of the interface.

   It is `__cdecl` on Windows and nothing elsewhere, where the platform has
   one convention. On Windows the default convention is a compiler option
   (`/Gv` makes `__vectorcall` the default and `/Gz` makes `__stdcall` the
   default on x86), so the header names it, the way Node-API does with
   `NAPI_CDECL`: a plugin built with such an option still links, is found by
   its entry points, and is called the way MrDocs calls it. A plugin writes
   it on the callbacks it hands to MrDocs (`build`, `apply`, `release` and
   the function given to `mrdocs_object_visit`).

   The convention is part of the binary interface, as much as a signature:
   it is `__cdecl` on Windows in every ABI version, and never changes.

   @since ABI 1
*/
#if defined(_WIN32)
#    define MRDOCS_PLUGIN_CALL __cdecl
#else
#    define MRDOCS_PLUGIN_CALL
#endif

/* Gives a function C linkage when the plugin is compiled as C++. */
#ifdef __cplusplus
#    define MRDOCS_PLUGIN_EXTERN_C extern "C"
#else
#    define MRDOCS_PLUGIN_EXTERN_C
#endif

/* Pass as a length to say that the string is NUL-terminated.

   @since ABI 1
*/
#define MRDOCS_AUTO_LENGTH SIZE_MAX

/* The environment of one callback.

   MrDocs creates an environment before it calls into a plugin and destroys it
   when the call returns. It identifies the call to every function below,
   carries the corpus, the configuration and the parameters the call is about,
   holds the error the plugin reports, and owns the handles the call hands
   out. A plugin never keeps one past the callback it received it in.

   An environment, and every handle it hands out, belongs to the thread that
   runs the callback: a plugin uses them on that thread only, never from a
   thread it starts, and never from two threads at once. A plugin that wants
   to work in parallel first copies what it needs out of the handles (strings
   and numbers) into data of its own.

   MrDocs makes no promise that the callbacks of a plugin run one at a time.
   The `mrdocs` tool runs a single generation, but a program that embeds
   MrDocs may build several corpora at once, and then the `build` of a
   generator and the `apply` of a transform (the transforms of a plugin are
   shared by every registry that loads it) can run on different threads, each
   with its own environment and all with the same `data` and the same global
   state of the plugin. State a callback changes, such as a counter or an open
   file, is therefore the plugin's to protect. `release` runs only once no
   callback is running, which `releasePlugins` requires of its caller.

   @since ABI 1
*/
typedef struct mrdocs_env mrdocs_env;

/* A handle to a value MrDocs owns.

   A value is a boolean, an integer, a string, a safe string, an array, an
   object, or a function, and `undefined` or `null`. A safe string is a
   string that a template helper marked as already escaped. It reads like a
   string, with @ref mrdocs_get_string_utf8, and it has a kind of its own,
   `MRDOCS_VALUE_SAFE_STRING`. The corpus, a configuration read from a file
   and the parameters read from a file hold none, and a plugin cannot make
   one, so a plugin sees one only when a program that embeds MrDocs put it
   in the parameters. A value reached through the corpus,
   the configuration or the parameters, at any depth, is a view of data MrDocs
   owns: the handle to a symbol is a view of the live symbol, and writing to
   it changes the symbol. A view is valid for the callback that produced it,
   and three rules follow from that. A view cannot be stored in a container
   the plugin made (@ref mrdocs_object_set, @ref mrdocs_array_push): copy
   the strings, numbers and booleans it holds instead. A view cannot be kept
   with @ref mrdocs_ref_create. Writing into a view accepts scalars and
   containers the plugin made, which MrDocs copies into the symbol, and
   nothing else.

   A handle names a place, not a snapshot. A write through a view can move
   what the view points at (a push can reallocate the elements of an array,
   a replaced field frees the object it held), so after a write MrDocs reads
   each view again from where it came from: a handle to an element shows the
   element at the same index, and a handle to a field shows what the field
   holds now. A handle whose place no longer holds a value of its kind fails
   with `MRDOCS_STATUS_INVALID_ARG`. This holds for a write that fails as well
   as one that works: a value written into a field is applied piece by piece,
   so a failed write may have changed part of the field.

   A handle belongs to the thread of the callback that obtained it (see
   @ref mrdocs_env). It is valid until that callback returns. Inside a
   long loop, a scope (@ref mrdocs_scope_open) releases the handles the loop
   makes as it goes. To keep a value past the callback, make a reference with
   @ref mrdocs_ref_create.

   @since ABI 1
*/
typedef struct mrdocs_value_s* mrdocs_value;

/* A reference that keeps a value alive across callbacks.

   @since ABI 1
*/
typedef struct mrdocs_ref_s* mrdocs_ref;

/* A scope of handles. The value is a token that names the scope and is never
   reused, so closing a scope twice, or out of order, fails.

   @since ABI 1
*/
typedef size_t mrdocs_scope;

/* Gives an enumeration a fixed underlying type when the header is compiled
   as C++.

   A C++ enumeration without one has undefined behavior when it holds a
   value outside the range of its enumerators, such as a status a plugin
   returns that this header does not list, or a log level it passes. With
   `int` every value of an int is a value of the enumeration, as in C, and
   the enumeration is passed and returned as the C compiler does it.
*/
#ifdef __cplusplus
#    define MRDOCS_PLUGIN_ENUM_BASE : int
#else
#    define MRDOCS_PLUGIN_ENUM_BASE
#endif

/* The outcome of a function.

   The values are part of the ABI and never change. A function that does not
   return `MRDOCS_STATUS_OK` leaves its output parameters untouched.

   @since ABI 1
*/
typedef enum mrdocs_status MRDOCS_PLUGIN_ENUM_BASE
{
    /* The function did what was asked. */
    MRDOCS_STATUS_OK = 0,
    /* An argument is null, out of range, or not a handle of this call. */
    MRDOCS_STATUS_INVALID_ARG = 1,
    /* The value is not of the kind the function works on. */
    MRDOCS_STATUS_TYPE_MISMATCH = 2,
    /* The corpus has no symbol with that id or name. */
    MRDOCS_STATUS_KEY_NOT_FOUND = 3,
    /* The value is a read-only view, as every value a generator is handed. */
    MRDOCS_STATUS_READ_ONLY = 4,
    /* A callback of the plugin failed. Plugins return this from their own
       callbacks; MrDocs never returns it from a function below. */
    MRDOCS_STATUS_PLUGIN_ERROR = 5,
    /* MrDocs could not do what was asked, for a reason that is not the
       caller's argument: a duplicate generator id, a field that rejects the
       value, or a failure inside MrDocs. */
    MRDOCS_STATUS_HOST_ERROR = 6
} mrdocs_status;

/* The kind of a value. The values are part of the ABI and never change.

   A function is a value a plugin can see but not call: the `get` and `lookup`
   members of the corpus are functions to a script, and a plugin reaches them
   through @ref mrdocs_corpus_find and @ref mrdocs_corpus_lookup instead.

   @since ABI 1
*/
typedef enum mrdocs_value_kind MRDOCS_PLUGIN_ENUM_BASE
{
    MRDOCS_VALUE_UNDEFINED = 0,
    MRDOCS_VALUE_NULL = 1,
    MRDOCS_VALUE_BOOLEAN = 2,
    MRDOCS_VALUE_INTEGER = 3,
    MRDOCS_VALUE_STRING = 4,
    MRDOCS_VALUE_SAFE_STRING = 5,
    MRDOCS_VALUE_ARRAY = 6,
    MRDOCS_VALUE_OBJECT = 7,
    MRDOCS_VALUE_FUNCTION = 8
} mrdocs_value_kind;

/* The severity of a message. The values are part of the ABI and never change.

   @since ABI 1
*/
typedef enum mrdocs_log_level MRDOCS_PLUGIN_ENUM_BASE
{
    MRDOCS_LOG_TRACE = 0,
    MRDOCS_LOG_DEBUG = 1,
    MRDOCS_LOG_INFO = 2,
    MRDOCS_LOG_WARN = 3,
    MRDOCS_LOG_ERROR = 4
} mrdocs_log_level;

/* A generator, as a plugin describes it.

   `struct_size` is the size of the structure as the plugin compiled it, so
   that later ABI versions can append fields: MrDocs reads only the fields
   that fit in the size it is given, and treats the ones beyond it as unset.

   @since ABI 1
*/
typedef struct mrdocs_generator_desc
{
    /* Set to `sizeof(mrdocs_generator_desc)`. */
    size_t struct_size;
    /* The id the generator is selected by, in the `generator` option and on
       the command line. */
    const char* id;
    /* A name for people. May be null: MrDocs then uses the id. */
    const char* display_name;
    /* The extension of the files the generator writes, in lower case and
       without the period. May be null, which means no extension. */
    const char* file_extension;
    /* Writes the documentation. The corpus is read-only. Returns
       `MRDOCS_STATUS_OK`, or a failure the plugin can explain with
       @ref mrdocs_set_error. No exception may leave the function: a C++
       plugin catches what its code throws and returns a status. */
    mrdocs_status (MRDOCS_PLUGIN_CALL *build)(mrdocs_env* env, void* data);
    /* Handed back to `build` and `release`. */
    void* data;
    /* Called once when MrDocs drops the generator, with `data`. May be null.
       The `mrdocs` tool calls it after the run, before `main` returns, so the
       static objects of the plugin are alive. A program that embeds MrDocs
       and does not release plugins explicitly gets the call during static
       destruction, when static objects of the plugin may be gone. It runs
       with no environment: a plugin deletes the references it keeps in `data`
       with `mrdocs_ref_delete(NULL, ref)`. No exception may leave it. */
    void (MRDOCS_PLUGIN_CALL *release)(void* data);
} mrdocs_generator_desc;

/* A corpus transform, as a plugin describes it.

   See @ref mrdocs_generator_desc for `struct_size`.

   @since ABI 1
*/
typedef struct mrdocs_transform_desc
{
    /* Set to `sizeof(mrdocs_transform_desc)`. */
    size_t struct_size;
    /* The id the transform's parameters are keyed by in `transform-options`.
       It names the transform in diagnostics and need not be unique. */
    const char* id;
    /* Changes the corpus. Runs once, after the corpus is built and finalized
       and before any generator. Returns `MRDOCS_STATUS_OK`, or a failure the
       plugin can explain with @ref mrdocs_set_error. No exception may leave
       the function. */
    mrdocs_status (MRDOCS_PLUGIN_CALL *apply)(mrdocs_env* env, void* data);
    /* Handed back to `apply` and `release`. */
    void* data;
    /* Called once when MrDocs drops the transform, with `data`. May be null.
       It runs at the same time as the `release` of a generator, and under
       the same rules: see @ref mrdocs_generator_desc. The order among the
       releases of a plugin is not specified. A plugin that shares state
       between its generators and transforms keeps a count in it, and frees
       it in the release that brings the count to zero. */
    void (MRDOCS_PLUGIN_CALL *release)(void* data);
} mrdocs_transform_desc;

/* The type of the callback @ref mrdocs_object_visit calls.

   `key` and `value` are valid during the call only: MrDocs releases the
   handles the callback makes once it returns, and closes the scopes the
   callback opened and did not close. Return false to stop the visit. No
   exception may leave the callback.

   @since ABI 1
*/
typedef bool (MRDOCS_PLUGIN_CALL *mrdocs_visit_fn)(
    mrdocs_env* env, const char* key, mrdocs_value value, void* data);

/*------------------------------------------------------------------------
 *
 * Entry points
 *
 *-----------------------------------------------------------------------*/

/* Define the two functions a plugin exports.

   `mrdocs_plugin_abi_version` reports @ref MRDOCS_PLUGIN_ABI_TARGET.
   `mrdocs_plugin_init` is the function MrDocs calls once, while it starts up,
   with the environment of the call; the braces that follow the macro are its
   body, which registers what the plugin provides and returns a status. In a
   process that runs MrDocs more than once the call happens for the first run
   only, so `mrdocs_get_config` shows the configuration of that run and what
   the body registers must not depend on it; decisions that depend on the
   configuration belong in the callbacks, which receive the configuration of
   the run they execute in:

       MRDOCS_PLUGIN_INIT(env)
       {
           mrdocs_generator_desc desc = { sizeof desc, "mine", "Mine", "txt",
               build, NULL, NULL };
           return mrdocs_register_generator(env, &desc);
       }

   The macro is the C analogue of `NAPI_MODULE_INIT`. Like every callback,
   the body lets no exception leave it.

   @since ABI 1
*/
#define MRDOCS_PLUGIN_INIT(env_)                                        \
    MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT uint32_t                \
    MRDOCS_PLUGIN_CALL mrdocs_plugin_abi_version(void);                 \
    MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT mrdocs_status           \
    MRDOCS_PLUGIN_CALL mrdocs_plugin_init(mrdocs_env* env);             \
                                                                        \
    MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT uint32_t                \
    MRDOCS_PLUGIN_CALL mrdocs_plugin_abi_version(void)                  \
    {                                                                   \
        return MRDOCS_PLUGIN_ABI_TARGET;                                \
    }                                                                   \
                                                                        \
    static mrdocs_status mrdocs_plugin_init_body(mrdocs_env* env_);     \
                                                                        \
    MRDOCS_PLUGIN_EXTERN_C MRDOCS_PLUGIN_EXPORT mrdocs_status           \
    MRDOCS_PLUGIN_CALL mrdocs_plugin_init(mrdocs_env* env)              \
    {                                                                   \
        return mrdocs_plugin_init_body(env);                            \
    }                                                                   \
                                                                        \
    static mrdocs_status mrdocs_plugin_init_body(mrdocs_env* env_)

/*------------------------------------------------------------------------
 *
 * Registration
 *
 *-----------------------------------------------------------------------*/

/* Register a generator.

   The generator becomes selectable under its id like one that ships with
   MrDocs. A generator whose id is taken, by a built-in generator or by
   another plugin, is an error.

   MrDocs copies the descriptor and the strings in it. Once the descriptor is
   valid, MrDocs owns `data` and calls `release` on it exactly once, even when
   the registration fails. Only `MRDOCS_STATUS_INVALID_ARG` leaves `data` with
   the caller. Callable from `mrdocs_plugin_init` only.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_register_generator(mrdocs_env* env, const mrdocs_generator_desc* desc);

/* Register a corpus transform.

   Transforms run in the order they were registered. See
   @ref mrdocs_register_generator for who owns `data`. Callable from
   `mrdocs_plugin_init` only.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_register_transform(mrdocs_env* env, const mrdocs_transform_desc* desc);

/*------------------------------------------------------------------------
 *
 * The context of a callback
 *
 *-----------------------------------------------------------------------*/

/* Get the corpus.

   In a transform the symbols are writable views of the live symbols; in a
   generator they are read-only. The `$meta` object of a symbol names its
   type and is shared by every symbol of that type, so it is read-only in a
   transform too, while a `$meta` property of an object the plugin made is
   the plugin's own. `INVALID_ARG` in `mrdocs_plugin_init`, where
   there is no corpus yet.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_corpus(mrdocs_env* env, mrdocs_value* result);

/* Get the configuration, as an object keyed by option name in camelCase:
   `source-root` is the key `sourceRoot`, and `warn-as-error` is `warnAsError`.
   A key spelled like the option, with hyphens, is not there, and reading it
   gives `MRDOCS_VALUE_UNDEFINED` with `MRDOCS_STATUS_OK`, as any missing key
   does. The `generator` option has no value in this object, so a generator
   cannot learn from it which generators the run selected. It is read-only.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_config(mrdocs_env* env, mrdocs_value* result);

/* Get the parameters of the callback.

   They are the object the user wrote under `generator-options.<id>` for a
   generator and under `transform-options.<id>` for a transform, and an empty
   object when there is none. In `mrdocs_plugin_init` there is no id yet, and
   the object is empty. It is read-only.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_params(mrdocs_env* env, mrdocs_value* result);

/* Get the directory a generator writes into.

   The path is absolute, in UTF-8. MrDocs has created the directory, and any
   missing parent, before it calls `build`, as it does for the generators it
   ships, so a plugin writes into it without creating it. A plugin generator
   always gets a directory, whatever its name, so `docs/v1.2` is one. The run
   fails before `build`, with a message that names the generator, when the
   `output` option is a path that exists and is not a directory, or when the
   run is single-page (`multipage` is false) and the path does not exist yet
   and ends in an extension, which is how the single-page generators MrDocs
   ships read a file name. On Windows a C
   plugin converts the path with `MultiByteToWideChar(CP_UTF8, ...)` before
   it calls `_wfopen_s` or another wide function, since the narrow functions
   read the bytes in the ANSI code page. The function follows @ref mrdocs_get_string_utf8
   for `buffer`, `capacity` and `length`. `INVALID_ARG` outside a generator.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_output_dir(
    mrdocs_env* env, char* buffer, size_t capacity, size_t* length);

/* Find a symbol by its id.

   `id` is the base58 string found in the `id` property of a symbol. The
   result has the access the symbols of @ref mrdocs_get_corpus have.
   `MRDOCS_STATUS_KEY_NOT_FOUND` when no such symbol exists.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_corpus_find(mrdocs_env* env, const char* id, mrdocs_value* result);

/* Find a symbol by its qualified name, as `corpus.lookup` does in a script.

   `name` is a name such as `std::vector`. It is looked up from the global
   namespace when `context_id` is null. Otherwise `context_id` is the base58
   string found in the `id` property of a symbol, and the name is resolved
   the way that scope sees it, so an unqualified name finds a sibling or a
   member of an enclosing scope. `INVALID_ARG` if `context_id` is not the id
   of a symbol of the corpus, whether it is not an id at all or names no
   symbol. The result has the access the symbols of
   @ref mrdocs_get_corpus have. `MRDOCS_STATUS_KEY_NOT_FOUND` when the name
   names no symbol. A symbol an earlier transform renamed is found under its
   new name by the transforms that run after it, and by the generators.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_corpus_lookup(
    mrdocs_env* env,
    const char* name,
    const char* context_id,
    mrdocs_value* result);

/*------------------------------------------------------------------------
 *
 * Values
 *
 *-----------------------------------------------------------------------*/

/* Get the kind of a value.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_kind(mrdocs_env* env, mrdocs_value value, mrdocs_value_kind* result);

/* Get a boolean. `TYPE_MISMATCH` unless the value is a boolean.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_bool(mrdocs_env* env, mrdocs_value value, bool* result);

/* Get an integer. `TYPE_MISMATCH` unless the value is an integer.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_int64(mrdocs_env* env, mrdocs_value value, int64_t* result);

/* Get a string as UTF-8. `TYPE_MISMATCH` unless the value is a string or a
   safe string.

   `length` receives the length of the string in bytes without the terminating
   NUL, whatever the capacity is. If `buffer` is not null, the function writes
   at most `capacity - 1` bytes of the string and a terminating NUL, so the
   string was cut short when `*length >= capacity`. Pass a null buffer to
   learn the length first. `buffer` may be null only if `capacity` is zero,
   and `length` may be null if the length is not needed.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_get_string_utf8(
    mrdocs_env* env,
    mrdocs_value value,
    char* buffer,
    size_t capacity,
    size_t* length);

/* Get the value of a property. `TYPE_MISMATCH` unless `object` is an object.
   A property the object does not have is `undefined`, as in a script: a symbol
   without documentation has no `doc`, and asking for it is not an error. Use
   @ref mrdocs_object_has to tell a missing property from one set to
   `undefined`.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_object_get(
    mrdocs_env* env, mrdocs_value object, const char* key, mrdocs_value* result);

/* Set a property. `READ_ONLY` if the object is a read-only view, or if the
   property is one of the identity and structure fields of a symbol: `kind`,
   `id`, `parent` and `inheritedFrom` never change, since they decide the
   type of the symbol, the key it is stored under and the scope it is
   reached through. A transform changes the data of a symbol, not its place
   in the corpus, so a list of symbol ids, such as the members of a namespace
   or a record, can be reordered or shortened but not extended. The objects
   inside a symbol, such as `doc`, use these names as ordinary properties.
   `HOST_ERROR` if the object does not take that property or that value, as
   when a symbol field is given a value of another type, a list of ids with
   one the list did not hold, or a container that nests too deep or holds
   itself, which MrDocs cannot copy into the symbol.

   `value` must not be a view: `INVALID_ARG` if it is, whether the object is
   one the plugin made or a symbol, since a view lasts for the callback only.
   Copy the data into values of your own first. Writing into a symbol field
   accepts scalars and containers the plugin made; MrDocs validates and
   copies them into the symbol, so later changes to the plugin's container
   do not reach it.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_object_set(
    mrdocs_env* env, mrdocs_value object, const char* key, mrdocs_value value);

/* Tell whether an object has a property.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_object_has(
    mrdocs_env* env, mrdocs_value object, const char* key, bool* result);

/* Call `callback` for each property of an object, in the order the object
   keeps them, until it returns false. The properties visited are those the
   object had when the visit started, so the callback can change the object
   safely. For an object the plugin made, the callback sees the values the
   properties had then. For a symbol or another view, a write through a view
   during the visit makes the visit read each remaining property again, so
   the callback sees what the property holds now.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_object_visit(
    mrdocs_env* env, mrdocs_value object, mrdocs_visit_fn callback, void* data);

/* Get the number of elements of an array.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_array_length(mrdocs_env* env, mrdocs_value array, size_t* result);

/* Get an element. `INVALID_ARG` if `index` is not less than the length.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_array_get(
    mrdocs_env* env, mrdocs_value array, size_t index, mrdocs_value* result);

/* Append an element. `READ_ONLY` if the array is a read-only view.
   `INVALID_ARG` if `value` is a view, as for @ref mrdocs_object_set: copy
   the data into values of your own first.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_array_push(mrdocs_env* env, mrdocs_value array, mrdocs_value value);

/* Make a `null`.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_null(mrdocs_env* env, mrdocs_value* result);

/* Make a boolean.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_bool(mrdocs_env* env, bool value, mrdocs_value* result);

/* Make an integer.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_int64(mrdocs_env* env, int64_t value, mrdocs_value* result);

/* Make a string from UTF-8 text. `length` is the length in bytes, or
   @ref MRDOCS_AUTO_LENGTH if `text` is NUL-terminated.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_string(
    mrdocs_env* env, const char* text, size_t length, mrdocs_value* result);

/* Make an empty object.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_object(mrdocs_env* env, mrdocs_value* result);

/* Make an empty array.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_create_array(mrdocs_env* env, mrdocs_value* result);

/*------------------------------------------------------------------------
 *
 * Handle lifetime
 *
 *-----------------------------------------------------------------------*/

/* Open a scope.

   Every handle made after the call is released by the matching
   @ref mrdocs_scope_close. A loop over a large corpus opens a scope per
   iteration, so that its handles do not pile up until the callback returns.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_scope_open(mrdocs_env* env, mrdocs_scope* result);

/* Close a scope, releasing the handles made since it was opened. Scopes close
   in the reverse order they opened: `INVALID_ARG` unless `scope` is the
   innermost scope that is open, so a scope closed twice, or an outer scope
   closed before an inner one, is refused and releases nothing.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_scope_close(mrdocs_env* env, mrdocs_scope scope);

/* Make a reference to a value, which keeps the value past the callback.

   A reference can hold a value the plugin made, or a handle derived from one
   (a property of a created object, say). A value obtained from the corpus,
   the configuration or the parameters is a view of memory that MrDocs owns
   only for the callback, so `INVALID_ARG` refuses a reference to one. A
   container the plugin made cannot hold a view, so it can always be kept.

   The reference and the value share their containers: a change the plugin
   makes to one is seen through the other.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_ref_create(mrdocs_env* env, mrdocs_value value, mrdocs_ref* result);

/* Get a handle, valid in the current callback, to the value a reference holds.

   The handle names the value the reference holds, not a copy of it.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_ref_get(mrdocs_env* env, mrdocs_ref ref, mrdocs_value* result);

/* Delete a reference. Passing a null reference is fine, and so is passing a
   null environment, which is what a `release` function has.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_ref_delete(mrdocs_env* env, mrdocs_ref ref);

/*------------------------------------------------------------------------
 *
 * Diagnostics
 *
 *-----------------------------------------------------------------------*/

/* Report an error.

   The message, in UTF-8, becomes the error that stops the run, whatever
   status the callback goes on to return. Only the first message of a callback
   is kept.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_set_error(mrdocs_env* env, const char* message);

/* Write a message to MrDocs's log, subject to the user's log level.

   A warning follows the user's `warn-as-error` setting, as the warnings of
   MrDocs and of scripts do: with it on, a `MRDOCS_LOG_WARN` message is an
   error. An error-level message makes the run exit with a failure status
   once it ends, even when nothing else went wrong, so a callback that can
   go on after a problem logs a warning instead, and one that cannot calls
   @ref mrdocs_set_error and returns a status other than ok.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_log(mrdocs_env* env, mrdocs_log_level level, const char* message);

/* Get facts about the MrDocs that loaded the plugin.

   `abi_version` receives @ref MRDOCS_PLUGIN_ABI_VERSION as MrDocs was built
   with it, which is the newest ABI the host provides. `release` is filled in
   like the string of @ref mrdocs_get_string_utf8, with the release of MrDocs.
   Any pointer may be null to skip that part.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_host_info(
    mrdocs_env* env,
    uint32_t* abi_version,
    char* release,
    size_t capacity,
    size_t* length);

/* Get the reason the last function that failed gave.

   Every function that fails records why, and the next call to a function
   replaces the record: one that succeeds leaves none. This function does not
   replace it, so the plugin reads the reason right after the failure, before
   it makes another call. The text is filled in like the string of
   @ref mrdocs_get_string_utf8, and is empty when there is no reason.
   @ref mrdocs_set_error does not record one.

   @since ABI 1
*/
MRDOCS_PLUGIN_API mrdocs_status MRDOCS_PLUGIN_CALL
mrdocs_last_failure(
    mrdocs_env* env, char* buffer, size_t capacity, size_t* length);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MRDOCS_PLUGIN_H */
