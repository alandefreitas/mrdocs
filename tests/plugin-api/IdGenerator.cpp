//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A plugin that installs a generator whose id the compile definition
// MRDOCS_TEST_GENERATOR_ID sets. Built more than once with one id, or with
// the id of a generator that ships with MrDocs, the second installation
// fails because the id is taken, and the run has to stop and name the
// library that was refused.

#include <mrdocs/Config.hpp>
#include <mrdocs/Corpus.hpp>
#include <mrdocs/Generator.hpp>
#include <mrdocs/Plugin.hpp>
#include <memory>
#include <string_view>

#ifndef MRDOCS_TEST_GENERATOR_ID
#    error "MRDOCS_TEST_GENERATOR_ID names the generator"
#endif

namespace {

class IdGenerator final
    : public mrdocs::Generator
{
public:
    std::string_view
    id() const noexcept override
    {
        return MRDOCS_TEST_GENERATOR_ID;
    }

    std::string_view
    displayName() const noexcept override
    {
        return "Test generator";
    }

    std::string_view
    fileExtension() const noexcept override
    {
        return "txt";
    }

    mrdocs::Expected<void>
    build(mrdocs::Corpus const&, mrdocs::Config const&) const override
    {
        return {};
    }
};

} // (anon)

MRDOCS_PLUGIN_MAIN(context)
{
    return context.installGenerator(std::make_unique<IdGenerator>());
}
