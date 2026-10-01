# utils/

Contributor and developer tooling: scripts for building, testing, linting, code
generation, and CI checks. These are maintenance helpers, not something the
project ships to its users (user-facing executables live in `tools/`).

## Contents
- `bootstrap/` — the `bootstrap.py` dependency/build setup tool and its Python tests.
- `cmake/`: CMake helpers: `helpers.cmake`, `install.cmake`, and `release-linux.cmake`, which sets the link flags of the Linux release.
- `codegen/` — code generators (config-info and the YAML config schema).
- `danger/` — the Danger.js pull-request size and hygiene checks run in CI.
- `docs/` — docs-tooling checks and generators, such as the bootstrap-options generator.
- `linting/` — formatting helpers (`reformat.py` / clang-format) and checkers
- `release/`: checks on a release package: `check-linux-portability.sh` fails when the Linux binary needs a newer glibc than the floor or any library outside glibc, and `test-check-linux-portability.sh` is its self-test. The Releases and Utility Tests workflows run them.
- `testing/` — the `run_all_tests.py` test runner and `run_ci_with_act.py`.
