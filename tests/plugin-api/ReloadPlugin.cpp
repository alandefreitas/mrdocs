//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A plugin for the test that loads the same plugins into a process more than
// once, see ReloadHost.cpp. Its entry point installs a generator, which fails
// when the entry point runs a second time because the id is taken, and a
// transform that appends an X to the name of every symbol named Widget, so
// the name says how many times the transform ran.

#include <mrdocs/Config.hpp>
#include <mrdocs/Corpus.hpp>
#include <mrdocs/Generator.hpp>
#include <mrdocs/Metadata.hpp>
#include <mrdocs/Plugin.hpp>
#include <mrdocs/Transform.hpp>
#include <memory>
#include <string_view>

namespace {

class ReloadGenerator final
    : public mrdocs::Generator
{
public:
    std::string_view
    id() const noexcept override
    {
        return "reload-test";
    }

    std::string_view
    displayName() const noexcept override
    {
        return "Reload test";
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

class AppendTransform final
    : public mrdocs::Transform
{
public:
    std::string_view
    id() const noexcept override
    {
        return "reload-append";
    }

    mrdocs::Expected<void>
    apply(
        mrdocs::Corpus& corpus,
        mrdocs::Config const&,
        mrdocs::dom::Object const&) const override
    {
        for (mrdocs::Symbol const& symbol : corpus)
        {
            if (symbol.Name.starts_with("Widget"))
            {
                corpus.find(symbol.id)->Name += "X";
            }
        }
        return {};
    }
};

} // (anon)

MRDOCS_PLUGIN_MAIN(context)
{
    MRDOCS_TRY(context.installGenerator(std::make_unique<ReloadGenerator>()));
    return context.installTransform(std::make_unique<AppendTransform>());
}
