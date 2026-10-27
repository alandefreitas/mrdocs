# tests/cmake

An out-of-tree project that consumes an installed MrDocs the way a user does: `find_package(mrdocs)`, a program that links `mrdocs::mrdocs-core`, a run of the installed tool on a small library, and a plugin that links `mrdocs::mrdocs`. CI configures it with `-D mrdocs_ROOT=<install prefix>` after the build job installs MrDocs, on every platform.

## The plugin and the compiler it was built with

A plugin has to be built against the same MrDocs build and the same standard library as the MrDocs that loads it. The compiler is not part of that rule: on Linux, GCC and Clang both use libstdc++ and the Itanium C++ ABI, so a plugin built with Clang loads into a MrDocs built with GCC. `src/plugin.cpp` is built twice in the CI jobs that run on Linux with gcc or clang, except the ones with a sanitizer or coverage, and the main gcc job, whose MrDocs is linked with `-static` and can't load plugins.

- `consumer_plugin` is built by the compiler of the project, which is the one that built MrDocs. `consumer-plugin-loads` loads it.
- `consumer_plugin_foreign` is the same file built by a compiler of the other family, and `consumer-plugin-foreign-compiler-loads` loads it. A gcc-built MrDocs loads a plugin built by clang, and a clang-built MrDocs loads one built by gcc.

The second build shows that a compiler of the other family can build a plugin from the installed headers alone, outside the toolchain of the project, and that MrDocs loads the result. It only holds where the two compilers share a standard library, which is the case on Linux by default. A plugin built with `-stdlib=libc++` is refused, and the `foreign-tag` case of `tests/plugin-api` covers that refusal.

The foreign build is off unless the cache variable `MRDOCS_CONSUMER_FOREIGN_CXX` names a C++ compiler, either a path or a name found on `PATH` such as `clang++`. CMake refuses a compiler of the same family as `CMAKE_CXX_COMPILER`, since that would prove nothing. The compiler is not a toolchain of the CMake project: the project runs the command a plugin author would type,

    <compiler> -std=c++23 -fPIC -shared -fvisibility=hidden -I<include> plugin.cpp -o consumer_plugin_foreign.so

which is all it takes on Linux, where a shared library may leave the symbols of MrDocs undefined and the tool supplies them when it loads the library. The variable is for Linux only, which is where CI has two compiler families to choose from. When the installed MrDocs can't load plugins, setting the variable is a configuration error. A clang foreign compiler is pointed at the GCC that built MrDocs with `--gcc-install-dir`, when it accepts the option, so both use one set of libstdc++ headers. On clang rows CI uses the `g++-<N>` of the GCC installation the row's clang selects, read from the "Selected GCC installation" line of `$CXX -v` (14 on ubuntu:24.04, 15 on ubuntu:26.04), so the plugin is built against the libstdc++ headers MrDocs was built against. A change to an installed header rebuilds the foreign copy, since the build tracks the headers the compiler read.

To run it by hand against an installed MrDocs:

    cmake -S tests/cmake -B build/cmake-consumer -G Ninja \
        -D mrdocs_ROOT=<install prefix> \
        -D CMAKE_CXX_COMPILER=g++ \
        -D MRDOCS_CONSUMER_FOREIGN_CXX=clang++
    cmake --build build/cmake-consumer
    ctest --test-dir build/cmake-consumer --output-on-failure

`.github/workflows/ci-build.yml` does this in the "CMake Consumer Test (cmake test)" step, after the step that installs the other compiler.
