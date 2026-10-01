# examples/plugins/stats/

A plugin, and a project documented with it. The two are separate things, and
this directory holds one of each:

- plugin.cpp is the plugin. It registers a generator, `stats`, which writes one
  line per symbol kind saying how many symbols of that kind the corpus has,
  and a transform that gives an undocumented symbol a placeholder brief. It
  uses the C interface of `mrdocs/plugin.h` directly.
- sample-project/ is the project it is run on: a little C++ to document, an
  mrdocs.yml that asks for `generator: stats`, and stats.txt, what the
  generator wrote for that input.

Building the library and documenting the sample project with it is what the
`mrdocs-plugin-example-stats` test does; `mrdocs-plugin-example-stats-compare`
then checks that the output is the stats.txt kept here, and the
`mrdocs-plugin-example-brief-filler` tests check the transform. The
`mrdocs-plugin-example-pipeline` tests run it next to a Lua transform in
pipeline-project/, which has to run after the plugin's. By hand, it is the same two steps:
build the library into the plugins subdirectory of a directory of your own,
then name that directory when you run MrDocs.

```
mrdocs --config=sample-project/mrdocs.yml --addons-supplemental=<that directory>
```

See the Plugins page of the documentation for what the code is doing, and for
how to build the library against an installed MrDocs rather than this one.
