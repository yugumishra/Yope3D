---
hide:
  - navigation
---

<!--
Single-page site: the `hide: navigation` front matter above removes the left
nav sidebar. Otherwise Material turns this page's headings into a deeply nested
left-nav tree whose short parent scrollwrap swallows wheel events, and it
duplicates the right-hand TOC. Keeping only the right TOC scrolls correctly.
-->

# Yope3D Scripting API

This is the rendered reference for the **`yope3d`** embedded Python module — the
scripting surface available inside behavior scripts (`scripts/behaviors/*.py`).

`yope3d` exists only inside the running engine process; it cannot be imported by a
stand-alone interpreter. This site is generated from the hand-maintained type
stub at `typings/yope3d/__init__.pyi`, which is also what powers editor
autocompletion. **Edit the stub, then rebuild this site** — see
`tools/build_docs.sh`.

The page below is the full module: conventions, hazards, the behavior-script
contract, worked examples, and every binding grouped by category.

::: yope3d
