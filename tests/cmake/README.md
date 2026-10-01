# tests/cmake

An out-of-tree project that consumes an installed MrDocs the way a user does: `find_package(mrdocs)`, a program that links `mrdocs::mrdocs-core`, a run of the installed tool on a small library, and a plugin that links `mrdocs::plugin`. CI configures it with `-D mrdocs_ROOT=<install prefix>` after the build job installs MrDocs, on every platform.

## The plugin and the toolchain it was built with

`src/plugin.cpp` is built twice in the CI jobs that run on Linux with gcc or clang, except the ones with a sanitizer or coverage, and the main gcc job, whose MrDocs is linked with `-static` and can't load plugins.

- `consumer_plugin` is built by the compiler of the project, which is the one that built MrDocs. `consumer-plugin-loads` loads it.
- `consumer_plugin_foreign` is the same file built by a compiler of the other family, and `consumer-plugin-foreign-compiler-loads` loads it. A gcc-built MrDocs loads a plugin built by clang, and a clang-built MrDocs loads one built by gcc.

The second build shows that a compiler of the other family can build a plugin from the installed header alone, outside the toolchain of the project, and that MrDocs loads the result. It does not show that the plugin is independent of the standard library or the C++ runtime: on Linux gcc and clang both use `libstdc++.so` and the Itanium C++ ABI by default, so a plugin that passed a `std::string` across the boundary would load and run here all the same. That independence comes from the design of the plugin API, a C ABI with no C++ type crossing it, and from `tests/plugin-api`, which builds plugins in plain C.

The foreign build is off unless the cache variable `MRDOCS_CONSUMER_FOREIGN_CXX` names a C++ compiler, either a path or a name found on `PATH` such as `clang++`. CMake refuses a compiler of the same family as `CMAKE_CXX_COMPILER`, since that would prove nothing. The compiler is not a toolchain of the CMake project: the project runs the command a plugin author would type,

    <compiler> -std=c++17 -fPIC -shared -fvisibility=hidden -I<include> plugin.cpp -o consumer_plugin_foreign.so

which is all it takes on Linux, where a shared library may leave the symbols of MrDocs undefined and the tool supplies them when it loads the library. The variable is for Linux only, which is where CI has two compiler families to choose from. When the installed MrDocs can't load plugins, the project says so in a status message and skips the plugin tests, including this one. A change to the installed `mrdocs/plugin.h` rebuilds the foreign copy, since the build tracks the headers the compiler read.

To run it by hand against an installed MrDocs:

    cmake -S tests/cmake -B build/cmake-consumer -G Ninja \
        -D mrdocs_ROOT=<install prefix> \
        -D CMAKE_CXX_COMPILER=g++ \
        -D MRDOCS_CONSUMER_FOREIGN_CXX=clang++
    cmake --build build/cmake-consumer
    ctest --test-dir build/cmake-consumer --output-on-failure

`.github/workflows/ci-build.yml` does this in the "CMake Consumer Test (cmake test)" step, after the step that installs the other compiler.
