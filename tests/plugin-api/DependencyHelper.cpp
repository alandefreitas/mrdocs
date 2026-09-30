//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2026 Alan de Freitas (alandefreitas@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

// A shared library a plugin depends on, kept in a directory that is neither
// the plugins directory nor the one holding the `mrdocs` executable. The
// ctest entry puts that directory in PATH, which the Windows loader must not
// consult when it resolves a plugin's dependencies.

extern "C" __declspec(dllexport) int
mrdocs_test_dependency_helper()
{
    return 0;
}
