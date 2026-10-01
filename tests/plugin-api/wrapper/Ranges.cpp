//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// The wrapper compiled as C++20, where the C++20 range algorithms and views
// take Array and Object. Building this file is the test: nothing links or
// runs it.

#include <mrdocs/plugin.hpp>
#include <version>

#if defined(__cpp_lib_ranges)

#include <algorithm>
#include <iterator>
#include <ranges>

namespace {

using namespace mrdocs::plugin;

static_assert(std::input_iterator<Array::Iterator>);
static_assert(std::input_iterator<Object::Iterator>);
static_assert(std::ranges::range<Array>);
static_assert(std::ranges::range<Object>);
static_assert(std::ranges::input_range<Array>);
static_assert(std::ranges::input_range<Object>);

[[maybe_unused]] std::ptrdiff_t
countIntegers(Value const& value)
{
    return std::ranges::count_if(
        Array(value), [](Value const& v) { return v.isInteger(); });
}

[[maybe_unused]] std::size_t
countFiltered(Value const& value)
{
    std::size_t count = 0;
    for (Value const& v : Array(value) | std::views::filter(
        [](Value const& e) { return e.isString(); }))
    {
        count += v.str().size();
    }
    return count;
}

[[maybe_unused]] std::size_t
countProperties(Value const& value)
{
    return static_cast<std::size_t>(std::ranges::distance(Object(value)));
}

} // (anon)

#endif
