# examples/plugins/deprecations/

A second plugin, closer to what a project would write for itself than the
`stats` one, and a project documented with it.

- plugin.cpp is the plugin, written on the C++ wrapper of `mrdocs/plugin.hpp`.
  It registers a generator, `deprecations`, which writes a report of every
  symbol that carries `[[deprecated]]`, with the message of the attribute and
  the location of the first declaration (the definition only for a symbol with no
  separate declaration), and a transform, `deprecation-brief`, which
  gives a deprecated symbol that has no brief the message as its brief, and
  does the same for an overload set of deprecated functions, whose brief
  MrDocs makes before the transforms run.
- sample-project/ is the project it is run on: a little C++ with deprecated
  functions (six of them in three overload sets, one defined out of line in
  legacy.cpp),
  types, an unnamed enumeration, a variable, an enumerator, a member of each of
  two explicit specializations of a class template (one of them on a type of
  another library, which the report names with its scope), a type in a hidden `detail`
  namespace (it stays out of the report), a header of another library that it
  uses but does not document (what that header deprecates stays out of the
  report), a header in external/, outside the source root, whose deprecated
  function the report lists without a location, an mrdocs.yml that asks for `generator: deprecations`, and
  deprecations.txt, what the generator wrote for that input.

The `mrdocs-plugin-example-deprecations` test builds the library and documents
the sample project with it, and `mrdocs-plugin-example-deprecations-compare`
checks that the report is the deprecations.txt kept here. The
`mrdocs-plugin-example-deprecation-brief` tests run the XML generator next to
the plugin and check the briefs the transform wrote. By hand, build the
library into the plugins subdirectory of a directory of your own, then name
that directory when you run MrDocs:

```
mrdocs --config=sample-project/mrdocs.yml --addons-supplemental=<that directory>
```

The Plugins page of the documentation walks through this code.
