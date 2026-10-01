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

// Name every function of the plugin C API from outside the tool, the way a
// plugin does, so that building this file checks that the API is exported.
// The plugin side of the Windows loader resolves each name against the
// tool's export table at link time, so a function the tool does not export is
// an unresolved external here, and nothing else in the tree would notice.
//
// Nothing runs, and there is no ctest entry: the object is linked whole, so
// every function it names has to resolve. Elsewhere a module library may
// leave symbols to the loader, so what this checks there is that the header
// compiles as C++ and that a library that links mrdocs::plugin builds. The
// callbacks name MRDOCS_PLUGIN_CALL as a plugin does, so that the MSVC build
// that compiles this file with /Gv (see CMakeLists.txt) accepts them.

#include <mrdocs/plugin.h>

namespace {

bool MRDOCS_PLUGIN_CALL
visit(mrdocs_env*, char const*, mrdocs_value, void*)
{
    return true;
}

void MRDOCS_PLUGIN_CALL
release(void*)
{
}

mrdocs_status MRDOCS_PLUGIN_CALL
run(mrdocs_env*, void*)
{
    return MRDOCS_STATUS_OK;
}

} // (anon)

// The entry points the macro defines compile as C++ too.
MRDOCS_PLUGIN_INIT(env)
{
    return mrdocs_log(env, MRDOCS_LOG_INFO, "");
}

extern "C" MRDOCS_PLUGIN_EXPORT mrdocs_status
mrdocs_probe_every_function(mrdocs_env* env)
{
    mrdocs_generator_desc generator = {};
    generator.struct_size = sizeof(generator);
    generator.id = "probe";
    generator.build = run;
    generator.release = release;
    mrdocs_transform_desc transform = {};
    transform.struct_size = sizeof(transform);
    transform.id = "probe";
    transform.apply = run;
    transform.release = release;

    mrdocs_value value = nullptr;
    mrdocs_value other = nullptr;
    mrdocs_value_kind kind;
    mrdocs_scope scope;
    mrdocs_ref ref = nullptr;
    bool flag = false;
    int64_t number = 0;
    size_t length = 0;
    uint32_t abi = 0;
    char buffer[16] = {};

    mrdocs_status status = MRDOCS_STATUS_OK;
    status = mrdocs_register_generator(env, &generator);
    status = mrdocs_register_transform(env, &transform);
    status = mrdocs_get_corpus(env, &value);
    status = mrdocs_get_config(env, &value);
    status = mrdocs_get_params(env, &value);
    status = mrdocs_get_output_dir(env, buffer, sizeof buffer, &length);
    status = mrdocs_corpus_find(env, "", &value);
    status = mrdocs_corpus_lookup(env, "", nullptr, &value);
    status = mrdocs_get_kind(env, value, &kind);
    status = mrdocs_get_bool(env, value, &flag);
    status = mrdocs_get_int64(env, value, &number);
    status = mrdocs_get_string_utf8(env, value, buffer, sizeof buffer, &length);
    status = mrdocs_object_get(env, value, "", &other);
    status = mrdocs_object_set(env, value, "", other);
    status = mrdocs_object_has(env, value, "", &flag);
    status = mrdocs_object_visit(env, value, visit, nullptr);
    status = mrdocs_array_length(env, value, &length);
    status = mrdocs_array_get(env, value, 0, &other);
    status = mrdocs_array_push(env, value, other);
    status = mrdocs_create_null(env, &value);
    status = mrdocs_create_bool(env, true, &value);
    status = mrdocs_create_int64(env, 1, &value);
    status = mrdocs_create_string(env, "", MRDOCS_AUTO_LENGTH, &value);
    status = mrdocs_create_object(env, &value);
    status = mrdocs_create_array(env, &value);
    status = mrdocs_scope_open(env, &scope);
    status = mrdocs_scope_close(env, scope);
    status = mrdocs_ref_create(env, value, &ref);
    status = mrdocs_ref_get(env, ref, &value);
    status = mrdocs_ref_delete(env, ref);
    status = mrdocs_set_error(env, "");
    status = mrdocs_log(env, MRDOCS_LOG_INFO, "");
    status = mrdocs_host_info(env, &abi, buffer, sizeof buffer, &length);
    status = mrdocs_last_failure(env, buffer, sizeof buffer, &length);
    return status;
}
