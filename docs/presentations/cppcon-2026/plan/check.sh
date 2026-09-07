#!/usr/bin/env bash
# Rebuild both decks (build/index.html for the talk, build/rehearsal.html for practice) and validate the plan.
# Run from anywhere; pipeline flags pass through, e.g. ./check.sh --strict
set -euo pipefail
cd "$(dirname "$0")"

# The IDE's python3 may not be the one with the dependencies (pyyaml, jsonschema); pick the first that has them.
PY=""
for candidate in "${PYTHON:-}" "$HOME/.pyenv/shims/python3" python3.12 python3.11 python3 /opt/homebrew/bin/python3 /usr/bin/python3; do
  [ -n "$candidate" ] || continue
  if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "import yaml, jsonschema" >/dev/null 2>&1; then PY="$candidate"; break; fi
done
if [ -z "$PY" ]; then
  echo "check.sh: no python3 with pyyaml and jsonschema found. Install them for the interpreter you use:" >&2
  echo "  python3 -m pip install -r tools/requirements.txt" >&2
  echo "or point PYTHON at one that has them: PYTHON=/path/to/python3 $0" >&2
  exit 1
fi

"$PY" tools/pipeline.py build
"$PY" tools/pipeline.py build --rehearsal
"$PY" tools/pipeline.py pace
echo
"$PY" tools/pipeline.py validate "$@"
