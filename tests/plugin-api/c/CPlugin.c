/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* A plugin written in C11, with no C++ anywhere: the compiler, the runtime
   and the standard library it uses are the C ones, and nothing but the
   functions of <mrdocs/plugin.h> connects it to MrDocs. It registers a
   transform and a generator. The transform counts the symbols of the corpus
   and keeps the count in a reference; the generator writes one line per
   symbol, sorted, and the count the transform kept. The ctest entry compares
   the file with c-plugin.txt. */

#define _POSIX_C_SOURCE 200809L

#include <mrdocs/plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#endif

/* Open `path`, which is UTF-8, for writing in binary mode. The narrow
   functions of the C runtime read the bytes in the ANSI code page on
   Windows, so the path is converted and opened with the wide function, in its
   `_s` form, which the C runtime of MSVC does not deprecate. */
static FILE*
open_utf8(const char* path)
{
#ifdef _WIN32
    wchar_t wide[4200];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide,
            (int)(sizeof wide / sizeof wide[0])) == 0)
    {
        return NULL;
    }
    FILE* file = NULL;
    return _wfopen_s(&file, wide, L"wb") == 0 ? file : NULL;
#else
    return fopen(path, "wb");
#endif
}

#define TRY(call)                                                           \
    do                                                                      \
    {                                                                       \
        mrdocs_status const status_ = (call);                               \
        if (status_ != MRDOCS_STATUS_OK)                                    \
        {                                                                   \
            return status_;                                                 \
        }                                                                   \
    } while (0)

/* What the transform leaves for the generator, which runs in a later
   callback. The two share it, and the order in which MrDocs releases them
   is not specified, so it is freed by whichever releases it last. */
typedef struct shared
{
    int users;
    mrdocs_ref ref;
} shared;

static void MRDOCS_PLUGIN_CALL
release_shared(void* data)
{
    shared* const s = (shared*)data;
    if (--s->users == 0)
    {
        /* A release function has no environment. */
        mrdocs_ref_delete(NULL, s->ref);
        free(s);
    }
}

/* Copy the string property `key` of `object` into a new buffer, or leave
   `*out` null if the property is not a string. */
static mrdocs_status
copy_string(mrdocs_env* env, mrdocs_value object, const char* key, char** out)
{
    mrdocs_value value;
    mrdocs_value_kind kind;
    size_t length = 0;
    char* text;
    *out = NULL;
    TRY(mrdocs_object_get(env, object, key, &value));
    TRY(mrdocs_get_kind(env, value, &kind));
    if (kind != MRDOCS_VALUE_STRING && kind != MRDOCS_VALUE_SAFE_STRING)
    {
        return MRDOCS_STATUS_OK;
    }
    TRY(mrdocs_get_string_utf8(env, value, NULL, 0, &length));
    text = (char*)malloc(length + 1);
    if (!text)
    {
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    if (mrdocs_get_string_utf8(env, value, text, length + 1, &length)
        != MRDOCS_STATUS_OK)
    {
        free(text);
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    *out = text;
    return MRDOCS_STATUS_OK;
}

static mrdocs_status MRDOCS_PLUGIN_CALL
apply_count(mrdocs_env* env, void* data)
{
    shared* const s = (shared*)data;
    mrdocs_value corpus;
    mrdocs_value symbols;
    mrdocs_value params;
    mrdocs_value kept;
    mrdocs_value count;
    mrdocs_value tag;
    size_t size = 0;
    char* text = NULL;

    TRY(mrdocs_get_corpus(env, &corpus));
    TRY(mrdocs_object_get(env, corpus, "symbols", &symbols));
    TRY(mrdocs_array_length(env, symbols, &size));

    TRY(mrdocs_create_object(env, &kept));
    TRY(mrdocs_create_int64(env, (int64_t)size, &count));
    TRY(mrdocs_object_set(env, kept, "symbols", count));

    /* The text comes from transform-options.c-transform. It is copied last,
       so that no earlier call can return while it is held. */
    TRY(mrdocs_get_params(env, &params));
    TRY(copy_string(env, params, "tag", &text));
    if (text)
    {
        mrdocs_status status =
            mrdocs_create_string(env, text, MRDOCS_AUTO_LENGTH, &tag);
        free(text);
        if (status == MRDOCS_STATUS_OK)
        {
            status = mrdocs_object_set(env, kept, "tag", tag);
        }
        TRY(status);
    }
    /* A ref made by an earlier call is replaced, so it is deleted first. */
    TRY(mrdocs_ref_delete(env, s->ref));
    s->ref = NULL;
    TRY(mrdocs_ref_create(env, kept, &s->ref));
    return MRDOCS_STATUS_OK;
}

static int
compare_lines(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static void
free_lines(char** lines, size_t size)
{
    size_t i;
    for (i = 0; i < size; ++i)
    {
        free(lines[i]);
    }
    free(lines);
}

/* Make "<kind> '<name>'" for every symbol of the corpus. */
static mrdocs_status
describe_symbols(mrdocs_env* env, char*** out_lines, size_t* out_size)
{
    mrdocs_value corpus;
    mrdocs_value symbols;
    size_t size = 0;
    size_t i;
    char** lines;
    *out_lines = NULL;
    *out_size = 0;
    TRY(mrdocs_get_corpus(env, &corpus));
    TRY(mrdocs_object_get(env, corpus, "symbols", &symbols));
    TRY(mrdocs_array_length(env, symbols, &size));
    lines = (char**)calloc(size ? size : 1, sizeof(char*));
    if (!lines)
    {
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    for (i = 0; i < size; ++i)
    {
        /* The handles made for one symbol are released before the next. */
        mrdocs_scope scope;
        mrdocs_value symbol = NULL;
        char* kind = NULL;
        char* name = NULL;
        mrdocs_status status = mrdocs_scope_open(env, &scope);
        int const scope_opened = status == MRDOCS_STATUS_OK;
        if (status == MRDOCS_STATUS_OK)
        {
            status = mrdocs_array_get(env, symbols, i, &symbol);
        }
        if (status == MRDOCS_STATUS_OK)
        {
            status = copy_string(env, symbol, "kind", &kind);
        }
        if (status == MRDOCS_STATUS_OK)
        {
            status = copy_string(env, symbol, "name", &name);
        }
        if (status == MRDOCS_STATUS_OK)
        {
            const char* const k = kind ? kind : "";
            const char* const n = name ? name : "";
            size_t const length = strlen(k) + strlen(n) + 4;
            lines[i] = (char*)malloc(length);
            if (lines[i])
            {
                snprintf(lines[i], length, "%s '%s'", k, n);
            }
            else
            {
                status = MRDOCS_STATUS_PLUGIN_ERROR;
            }
        }
        free(kind);
        free(name);
        /* Every path after the scope opened closes it. */
        if (scope_opened)
        {
            mrdocs_status const closed = mrdocs_scope_close(env, scope);
            if (status == MRDOCS_STATUS_OK)
            {
                status = closed;
            }
        }
        if (status != MRDOCS_STATUS_OK)
        {
            free_lines(lines, size);
            return status;
        }
    }
    qsort(lines, size, sizeof(char*), compare_lines);
    *out_lines = lines;
    *out_size = size;
    return MRDOCS_STATUS_OK;
}

static mrdocs_status
write_report(mrdocs_env* env, shared const* s, FILE* file)
{
    char** lines;
    size_t size;
    size_t i;
    mrdocs_value kept = NULL;
    mrdocs_value count = NULL;
    int64_t symbols = 0;
    char* tag = NULL;
    mrdocs_status status = describe_symbols(env, &lines, &size);
    if (status != MRDOCS_STATUS_OK)
    {
        return status;
    }
    for (i = 0; i < size; ++i)
    {
        fprintf(file, "%s\n", lines[i]);
    }
    free_lines(lines, size);

    /* The reference holds the value the transform made. */
    status = mrdocs_ref_get(env, s->ref, &kept);
    if (status == MRDOCS_STATUS_OK)
    {
        status = mrdocs_object_get(env, kept, "symbols", &count);
    }
    if (status == MRDOCS_STATUS_OK)
    {
        status = mrdocs_get_int64(env, count, &symbols);
    }
    if (status == MRDOCS_STATUS_OK)
    {
        status = copy_string(env, kept, "tag", &tag);
    }
    if (status == MRDOCS_STATUS_OK)
    {
        fprintf(file, "transform tag: %s\n", tag ? tag : "");
        fprintf(file, "transform symbols: %d\n", (int)symbols);
    }
    free(tag);
    return status;
}

static mrdocs_status MRDOCS_PLUGIN_CALL
build_report(mrdocs_env* env, void* data)
{
    char dir[4096];
    char path[4200];
    size_t length = 0;
    FILE* file;
    mrdocs_status status;

    TRY(mrdocs_get_output_dir(env, dir, sizeof dir, &length));
    if (length >= sizeof dir)
    {
        mrdocs_set_error(env, "the output directory is too long");
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    /* MrDocs has created the directory. */
    snprintf(path, sizeof path, "%s/c-plugin.txt", dir);
    /* Binary mode keeps the line endings the same on every platform. */
    file = open_utf8(path);
    if (!file)
    {
        char message[4300];
        snprintf(message, sizeof message,
            "could not open \"%s\" for writing", path);
        mrdocs_set_error(env, message);
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    status = write_report(env, (shared const*)data, file);
    fclose(file);
    return status;
}

MRDOCS_PLUGIN_INIT(env)
{
    mrdocs_transform_desc transform = { .struct_size = sizeof(mrdocs_transform_desc) };
    mrdocs_generator_desc generator = { .struct_size = sizeof(mrdocs_generator_desc) };
    shared* const s = (shared*)calloc(1, sizeof(shared));
    if (!s)
    {
        mrdocs_set_error(env, "out of memory");
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    /* Each registration owns one use of the state. MrDocs calls release even
       when a registration fails, except for an invalid descriptor, which
       leaves the use with the caller. The second use is added only once the
       first registration has succeeded. */
    mrdocs_status status;
    s->users = 1;

    transform.id = "c-transform";
    transform.apply = apply_count;
    transform.data = s;
    transform.release = release_shared;
    status = mrdocs_register_transform(env, &transform);
    if (status != MRDOCS_STATUS_OK)
    {
        if (status == MRDOCS_STATUS_INVALID_ARG)
        {
            release_shared(s);
        }
        return status;
    }

    s->users = 2;
    generator.id = "c-plugin";
    generator.display_name = "C plugin";
    generator.file_extension = "txt";
    generator.build = build_report;
    generator.data = s;
    generator.release = release_shared;
    status = mrdocs_register_generator(env, &generator);
    if (status == MRDOCS_STATUS_INVALID_ARG)
    {
        release_shared(s);
    }
    return status;
}
