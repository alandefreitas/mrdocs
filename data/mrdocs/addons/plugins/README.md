# data/mrdocs/addons/plugins/

Holds the shared libraries that MrDocs loads when it is launched: every
`.dll`, `.so`, or `.dylib` directly inside it is a plugin. MrDocs opens it, in
name order, and calls its `mrdocs_plugin_init` function so that it can register
what it provides. A library that cannot be opened, lacks
`mrdocs_plugin_abi_version` or `mrdocs_plugin_init`, or fails to initialize
stops the run. Any other file here, this one included, is ignored.

The libraries a plugin depends on go in the `lib/` subdirectory, which MrDocs
never searches for plugins and is the only subdirectory it adds to the Windows
search path. On Linux and macOS the plugin finds them through its own rpath; on
macOS that only works for a library whose install name starts with `@rpath/`,
and on Linux a library in `lib/` that needs another one there needs an
`$ORIGIN` rpath of its own.

A supplemental addons directory can carry a `plugins` directory of its own, so
a plugin does not have to be installed next to MrDocs.

See the Plugins page of the documentation for how to write one.
