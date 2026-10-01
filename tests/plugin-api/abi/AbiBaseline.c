/*
 * Licensed under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
 *
 * Official repository: https://github.com/cppalliance/mrdocs
 */

/* The ABI that include/mrdocs/plugin.h promises, written down so that the
   compiler can check it.

   Each ABI version has a block below, guarded by
   `#if MRDOCS_PLUGIN_ABI_TARGET >= n`, that holds everything that version
   introduced: the offset and the type of each new field of a descriptor
   structure (the full pointer type for a callback, since that is what the
   host calls), the value of each new enumerator, and the signature of each
   new function. The
   size of a structure depends on the target, since a later version appends
   fields, so the size a version gives it is recorded in the block of that
   version under `#if MRDOCS_PLUGIN_ABI_TARGET == n`. The file is compiled
   once for every version from 1 to MRDOCS_PLUGIN_ABI_VERSION, with
   MRDOCS_PLUGIN_ABI_TARGET set to it (see CMakeLists.txt), and nothing links
   or runs it. A change to something an earlier block records fails to
   compile, because the earlier block is still compiled against the changed
   header.

   The blocks only grow. When the ABI grows to version n, append a block
   guarded by `#if MRDOCS_PLUGIN_ABI_TARGET >= n` with the new functions, the
   offsets and types of the new fields, the new enumerators and, under
   `#if MRDOCS_PLUGIN_ABI_TARGET == n`, the new size of each structure that
   grew, and never edit the entries of an earlier block.
   utils/docs/generate_plugin_api_reference.py --check fails when a function,
   field, enumerator or constant of the header is missing from this file.

   Layouts are recorded for targets with 64-bit pointers, which are the
   targets MrDocs is built for. */

#include <mrdocs/plugin.h>

#include <stdint.h>

/* The calling convention the header promises, spelled out here and not
   through MRDOCS_PLUGIN_CALL: every expected function and callback type
   below uses it, so a change to the macro makes the types differ from the
   declarations of the header, on Windows, where it is not empty. */
#if defined(_WIN32)
#    define ABI_CALL __cdecl
#else
#    define ABI_CALL
#endif

/* The name of the checked thing is the message of the failure. */
#define ABI_SIZE(T, n) \
    _Static_assert(sizeof(T) == (n), "size of " #T)
#define ABI_OFFSET(T, field, n) \
    _Static_assert(offsetof(T, field) == (n), "offset of " #T "." #field)
#define ABI_VALUE(name, n) \
    _Static_assert((name) == (n), "value of " #name)
#define ABI_FIELD(T, field, type) \
    _Static_assert( \
        _Generic(((T*)0)->field, type: 1, default: 0), \
        "type of " #T "." #field)
#define ABI_FN(name, type) \
    _Static_assert(_Generic(&name, type: 1, default: 0), "signature of " #name)
#define ABI_TYPE(T, type) \
    _Static_assert(_Generic(*(T*)0, type: 1, default: 0), "type of " #T)

/*------------------------------------------------------------------------
 *
 * ABI 1
 *
 *-----------------------------------------------------------------------*/
#if MRDOCS_PLUGIN_ABI_TARGET >= 1

/* Constants */
ABI_VALUE(MRDOCS_AUTO_LENGTH, SIZE_MAX);
_Static_assert(MRDOCS_PLUGIN_ABI_VERSION >= 1, "MRDOCS_PLUGIN_ABI_VERSION");
_Static_assert(MRDOCS_PLUGIN_ABI_TARGET >= 1, "MRDOCS_PLUGIN_ABI_TARGET");

/* A function declared with the macro has the convention written above. */
void MRDOCS_PLUGIN_CALL abi_call_probe(void);
ABI_FN(abi_call_probe, void (ABI_CALL *)(void));

/* The two entry points the macro defines, with the version it reports. */
MRDOCS_PLUGIN_INIT(env)
{
    (void)env;
    return MRDOCS_STATUS_OK;
}
ABI_FN(mrdocs_plugin_abi_version, uint32_t (ABI_CALL *)(void));
ABI_FN(mrdocs_plugin_init, mrdocs_status (ABI_CALL *)(mrdocs_env*));

/* Handles and callbacks */
ABI_TYPE(mrdocs_value, struct mrdocs_value_s*);
ABI_TYPE(mrdocs_ref, struct mrdocs_ref_s*);
ABI_TYPE(mrdocs_scope, size_t);
ABI_TYPE(
    mrdocs_visit_fn,
    bool (ABI_CALL *)(mrdocs_env*, const char*, mrdocs_value, void*));

/* Enumerations */
ABI_VALUE(MRDOCS_STATUS_OK, 0);
ABI_VALUE(MRDOCS_STATUS_INVALID_ARG, 1);
ABI_VALUE(MRDOCS_STATUS_TYPE_MISMATCH, 2);
ABI_VALUE(MRDOCS_STATUS_KEY_NOT_FOUND, 3);
ABI_VALUE(MRDOCS_STATUS_READ_ONLY, 4);
ABI_VALUE(MRDOCS_STATUS_PLUGIN_ERROR, 5);
ABI_VALUE(MRDOCS_STATUS_HOST_ERROR, 6);

ABI_VALUE(MRDOCS_VALUE_UNDEFINED, 0);
ABI_VALUE(MRDOCS_VALUE_NULL, 1);
ABI_VALUE(MRDOCS_VALUE_BOOLEAN, 2);
ABI_VALUE(MRDOCS_VALUE_INTEGER, 3);
ABI_VALUE(MRDOCS_VALUE_STRING, 4);
ABI_VALUE(MRDOCS_VALUE_SAFE_STRING, 5);
ABI_VALUE(MRDOCS_VALUE_ARRAY, 6);
ABI_VALUE(MRDOCS_VALUE_OBJECT, 7);
ABI_VALUE(MRDOCS_VALUE_FUNCTION, 8);

ABI_VALUE(MRDOCS_LOG_TRACE, 0);
ABI_VALUE(MRDOCS_LOG_DEBUG, 1);
ABI_VALUE(MRDOCS_LOG_INFO, 2);
ABI_VALUE(MRDOCS_LOG_WARN, 3);
ABI_VALUE(MRDOCS_LOG_ERROR, 4);

/* Descriptors */
#if UINTPTR_MAX == UINT64_MAX
#if MRDOCS_PLUGIN_ABI_TARGET == 1
ABI_SIZE(mrdocs_generator_desc, 56);
ABI_SIZE(mrdocs_transform_desc, 40);
#endif
ABI_OFFSET(mrdocs_generator_desc, struct_size, 0);
ABI_OFFSET(mrdocs_generator_desc, id, 8);
ABI_OFFSET(mrdocs_generator_desc, display_name, 16);
ABI_OFFSET(mrdocs_generator_desc, file_extension, 24);
ABI_OFFSET(mrdocs_generator_desc, build, 32);
ABI_OFFSET(mrdocs_generator_desc, data, 40);
ABI_OFFSET(mrdocs_generator_desc, release, 48);
ABI_FIELD(mrdocs_generator_desc, struct_size, size_t);
ABI_FIELD(mrdocs_generator_desc, id, const char*);
ABI_FIELD(mrdocs_generator_desc, display_name, const char*);
ABI_FIELD(mrdocs_generator_desc, file_extension, const char*);
ABI_FIELD(mrdocs_generator_desc, build, mrdocs_status (ABI_CALL *)(mrdocs_env*, void*));
ABI_FIELD(mrdocs_generator_desc, data, void*);
ABI_FIELD(mrdocs_generator_desc, release, void (ABI_CALL *)(void*));

ABI_OFFSET(mrdocs_transform_desc, struct_size, 0);
ABI_OFFSET(mrdocs_transform_desc, id, 8);
ABI_OFFSET(mrdocs_transform_desc, apply, 16);
ABI_OFFSET(mrdocs_transform_desc, data, 24);
ABI_OFFSET(mrdocs_transform_desc, release, 32);
ABI_FIELD(mrdocs_transform_desc, struct_size, size_t);
ABI_FIELD(mrdocs_transform_desc, id, const char*);
ABI_FIELD(mrdocs_transform_desc, apply, mrdocs_status (ABI_CALL *)(mrdocs_env*, void*));
ABI_FIELD(mrdocs_transform_desc, data, void*);
ABI_FIELD(mrdocs_transform_desc, release, void (ABI_CALL *)(void*));
#endif

/* Functions */
ABI_FN(mrdocs_register_generator,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, const mrdocs_generator_desc*));
ABI_FN(mrdocs_register_transform,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, const mrdocs_transform_desc*));
ABI_FN(mrdocs_get_corpus,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value*));
ABI_FN(mrdocs_get_config,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value*));
ABI_FN(mrdocs_get_params,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value*));
ABI_FN(mrdocs_get_output_dir,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, char*, size_t, size_t*));
ABI_FN(mrdocs_corpus_find,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, const char*, mrdocs_value*));
ABI_FN(mrdocs_corpus_lookup,
    mrdocs_status (ABI_CALL *)(
        mrdocs_env*, const char*, const char*, mrdocs_value*));
ABI_FN(mrdocs_get_kind,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, mrdocs_value_kind*));
ABI_FN(mrdocs_get_bool,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, bool*));
ABI_FN(mrdocs_get_int64,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, int64_t*));
ABI_FN(mrdocs_get_string_utf8,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, char*, size_t, size_t*));
ABI_FN(mrdocs_object_get,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, const char*, mrdocs_value*));
ABI_FN(mrdocs_object_set,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, const char*, mrdocs_value));
ABI_FN(mrdocs_object_has,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, const char*, bool*));
ABI_FN(mrdocs_object_visit,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, mrdocs_visit_fn, void*));
ABI_FN(mrdocs_array_length,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, size_t*));
ABI_FN(mrdocs_array_get,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, size_t, mrdocs_value*));
ABI_FN(mrdocs_array_push,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, mrdocs_value));
ABI_FN(mrdocs_create_null,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value*));
ABI_FN(mrdocs_create_bool,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, bool, mrdocs_value*));
ABI_FN(mrdocs_create_int64,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, int64_t, mrdocs_value*));
ABI_FN(mrdocs_create_string,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, const char*, size_t, mrdocs_value*));
ABI_FN(mrdocs_create_object,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value*));
ABI_FN(mrdocs_create_array,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value*));
ABI_FN(mrdocs_scope_open,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_scope*));
ABI_FN(mrdocs_scope_close,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_scope));
ABI_FN(mrdocs_ref_create,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_value, mrdocs_ref*));
ABI_FN(mrdocs_ref_get,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_ref, mrdocs_value*));
ABI_FN(mrdocs_ref_delete,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_ref));
ABI_FN(mrdocs_set_error,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, const char*));
ABI_FN(mrdocs_log,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, mrdocs_log_level, const char*));
ABI_FN(mrdocs_host_info,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, uint32_t*, char*, size_t, size_t*));
ABI_FN(mrdocs_last_failure,
    mrdocs_status (ABI_CALL *)(mrdocs_env*, char*, size_t, size_t*));

#endif /* MRDOCS_PLUGIN_ABI_TARGET >= 1 */
