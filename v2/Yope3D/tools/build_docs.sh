#!/usr/bin/env bash
# Build (or serve) the Yope3D scripting-API documentation site.
#
# Renders typings/yope3d/__init__.pyi into a website via mkdocs-material +
# mkdocstrings. This is docs-only and entirely separate from the CMake/engine
# build — nothing here links against or imports the engine.
#
#   tools/build_docs.sh            build the static site to docs/site/
#   tools/build_docs.sh serve      live-reload dev server (http://127.0.0.1:8000)
#   tools/build_docs.sh open       build, then open docs/site/index.html
#
# A self-contained venv is created at docs/.venv on first run (gitignored).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCS_DIR="$SCRIPT_DIR/../docs"
VENV="$DOCS_DIR/.venv"
PY="${PYTHON:-python3}"

if [[ ! -x "$VENV/bin/mkdocs" ]]; then
  echo "[build_docs] creating venv at $VENV ..."
  "$PY" -m venv "$VENV"
  "$VENV/bin/pip" install -q --upgrade pip
  "$VENV/bin/pip" install -q -r "$DOCS_DIR/requirements-docs.txt"
fi

cmd="${1:-build}"
cd "$DOCS_DIR"
case "$cmd" in
  serve)
    exec "$VENV/bin/mkdocs" serve
    ;;
  open)
    "$VENV/bin/mkdocs" build --strict
    echo "[build_docs] built -> $DOCS_DIR/site/index.html"
    command -v open >/dev/null && open "$DOCS_DIR/site/index.html" || true
    ;;
  build)
    "$VENV/bin/mkdocs" build --strict
    echo "[build_docs] built -> $DOCS_DIR/site/index.html"
    ;;
  *)
    echo "usage: tools/build_docs.sh [build|serve|open]" >&2
    exit 2
    ;;
esac
