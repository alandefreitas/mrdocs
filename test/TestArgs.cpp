//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdox
//

#include "TestArgs.hpp"
#include <fmt/format.h>
#include <cstddef>
#include <vector>

namespace clang {
namespace mrdox {

TestArgs TestArgs::instance_;

//------------------------------------------------

TestArgs::
TestArgs()
    : commonCat("COMMON")

    , usageText(
R"(MrDox Test Program
)")

    , extraHelp(
R"(
ADDONS:
    The location of the addons directory is determined in this order:

    1. The --addons command line argument if present, or
    2. The directory containing the mrdox tool executable, otherwise
    3. The environment variable MRDOX_ADDONS_DIR if set.

EXAMPLES:
    mrdox-test .. ( compile-commands )
    mrdox-test .. --action ( "test" | "create" | "update" ) ( dir | file )...
    mrdox-test --action test friend.cpp
    mrdox-test --format adoc compile_commands.json
)")

//
// Common options
//

, action(
    "action",
    llvm::cl::desc(R"(Which action should be performed:)"),
    llvm::cl::init(test),
    llvm::cl::values(
        clEnumVal(test, "Compare output against expected."),
        clEnumVal(create, "Create missing expected xml files."),
        clEnumVal(update, "Update all expected xml files.")),
    llvm::cl::cat(commonCat))

, addonsDir(
    "addons",
    llvm::cl::desc("The path to the addons directory."),
    llvm::cl::cat(commonCat))

, inputPaths(
    "inputs",
    llvm::cl::Sink,
    llvm::cl::desc("A list of directories and/or .cpp files to test."),
    llvm::cl::cat(commonCat))

//
// Test options
//

, badOption(
    "bad",
    llvm::cl::desc("Write a .bad.xml file for each test failure."),
    llvm::cl::init(true))

, unitOption(
    "unit",
    llvm::cl::desc("Run all or selected unit test suites."),
    llvm::cl::init(true))
{
}

void
TestArgs::
hideForeignOptions()
{
    // VFALCO When adding an option, it must
    // also be added to this list or else it
    // will stay hidden.

    std::vector<llvm::cl::Option const*> ours({
        &action,
        &addonsDir,
        std::addressof(inputPaths),
        &badOption,
        &unitOption
    });

    // Really hide the clang/llvm default
    // options which we didn't ask for.
    auto optionMap = llvm::cl::getRegisteredOptions();
    for(auto& opt : optionMap)
    {
        if(std::find(ours.begin(), ours.end(), opt.getValue()) != ours.end())
            opt.getValue()->setHiddenFlag(llvm::cl::NotHidden);
        else
            opt.getValue()->setHiddenFlag(llvm::cl::ReallyHidden);
    }
}

} // mrdox
} // clang