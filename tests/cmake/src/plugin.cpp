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

// A plugin built the way a plugin author builds one: out of tree, against
// nothing but an installed MrDocs. It stays deliberately small, since what
// is under test is the installation rather than the generator: compiling it
// exercises the header the package ships and the usage requirements the
// imported tool carries, and linking it exercises the functions the tool
// exports.

#include <mrdocs/plugin.h>
#include <filesystem>
#include <exception>
#include <fstream>
#include <string>

namespace {

// The directory as a path. The text is UTF-8, which a path made from a
// std::string does not read as such on Windows.
std::filesystem::path
toPath(std::string const& utf8)
{
#if defined(__cpp_lib_char8_t)
    return std::filesystem::path(std::u8string(
        reinterpret_cast<char8_t const*>(utf8.data()), utf8.size()));
#else
    return std::filesystem::u8path(utf8);
#endif
}

// Write the number of symbols the corpus has into probe.txt, under the
// directory the generator is given.
mrdocs_status
writeProbe(mrdocs_env* env)
{
    mrdocs_value corpus;
    mrdocs_value symbols;
    std::size_t size = 0;
    mrdocs_status status = mrdocs_get_corpus(env, &corpus);
    if (status == MRDOCS_STATUS_OK)
    {
        status = mrdocs_object_get(env, corpus, "symbols", &symbols);
    }
    if (status == MRDOCS_STATUS_OK)
    {
        status = mrdocs_array_length(env, symbols, &size);
    }

    std::string dir(1024, '\0');
    std::size_t length = 0;
    if (status == MRDOCS_STATUS_OK)
    {
        status = mrdocs_get_output_dir(env, dir.data(), dir.size(), &length);
    }
    if (status != MRDOCS_STATUS_OK || length >= dir.size())
    {
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    dir.resize(length);

    std::filesystem::path const file = toPath(dir) / "probe.txt";
    std::ofstream os(file);
    if (!os)
    {
        std::string const message =
            "could not open \"" + file.string() + "\" for writing";
        mrdocs_set_error(env, message.c_str());
        return MRDOCS_STATUS_PLUGIN_ERROR;
    }
    os << size << '\n';
    return MRDOCS_STATUS_OK;
}

// No exception leaves a callback: it becomes the error of the call.
mrdocs_status MRDOCS_PLUGIN_CALL
buildProbe(mrdocs_env* env, void*)
{
    try
    {
        return writeProbe(env);
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

} // (anon)

MRDOCS_PLUGIN_INIT(env)
{
    mrdocs_generator_desc generator = {};
    generator.struct_size = sizeof(generator);
    generator.id = "consumer-probe";
    generator.display_name = "Consumer probe";
    generator.file_extension = "txt";
    generator.build = buildProbe;
    return mrdocs_register_generator(env, &generator);
}
