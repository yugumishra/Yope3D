#!/usr/bin/env python3
"""Canonical FIM wire format — the single source of truth for this project.

Every training example and every verification check goes through this module.
Nothing else in fim-finetuning/ may hand-roll a `<|fim_prefix|>` string. The
whole point is that there is exactly one place to fix if the format is wrong.

WHY THIS MATTERS MORE THAN ANYTHING ELSE HERE
    Training examples must be byte-identical in structure to what llama.cpp's
    /infill endpoint actually sends. Train on a format that differs even in
    token ordering and you do not get "slightly worse" — you get zero transfer.
    The evidence is already measured: --spm-infill reorders the SAME content
    (suffix before prefix) and Qwen2.5-Coder scores 0.0% on every metric. It
    starts emitting file headers inside method bodies. Format is not a detail.

STATUS: VERIFIED 2026-07-28 against llama.cpp b9890 (llama-server /infill),
token-ID identical on both the no-extra and input_extra cases.
Re-run `verify_format.py` after any llama.cpp upgrade.

WHAT VERIFICATION CHANGED (the reconstruction was wrong twice)
    1. There is NO file-level-only shape. llama.cpp emits the repo-level
       wrapper unconditionally — even with no input_extra and no filename.
       The original guess sent a bare `<|fim_prefix|>...` and diverged at
       token 0.
    2. The repo name and the target filename are HARDCODED PLACEHOLDERS,
       `myproject` and `filename`. Probed against repo_name/filename/path
       request fields: none of them change the output. Only `input_extra`
       entries carry real paths.

    Consequence, and the reason this matters more than it looks: training
    examples must contain the literal strings `myproject` and `filename`.
    Using the real repo name ("Yope3D") and real target paths would train the
    model on different anchor tokens than it sees at inference, on exactly the
    tokens that mark the repo-level structure. That is a silent, plausible-
    looking mismatch — the kind that produces a fine-tune which trains cleanly
    and transfers nothing.
"""

from __future__ import annotations

from dataclasses import dataclass, field

# Qwen2.5-Coder FIM special tokens. Byte-exact: these are single tokens in the
# vocab, and a typo silently degrades them into several ordinary tokens rather
# than raising anything.
FIM_PREFIX = "<|fim_prefix|>"
FIM_SUFFIX = "<|fim_suffix|>"
FIM_MIDDLE = "<|fim_middle|>"
REPO_NAME = "<|repo_name|>"
FILE_SEP = "<|file_sep|>"

SPECIALS = (FIM_PREFIX, FIM_SUFFIX, FIM_MIDDLE, REPO_NAME, FILE_SEP)

# llama.cpp hardcodes both of these. They are NOT configurable via the /infill
# request — verified by probing repo_name / filename / path, none of which had
# any effect on the emitted prompt. Training data must use them verbatim.
REPO_PLACEHOLDER = "myproject"
FILE_PLACEHOLDER = "filename"


@dataclass
class Chunk:
    """One extra context file, as llama.cpp sends via `input_extra`."""

    path: str
    text: str


@dataclass
class Example:
    """One training example, pre-serialisation.

    prefix/suffix/middle are raw source text. `middle` is the target — the span
    the model must produce. At inference the server sends everything up to and
    including FIM_MIDDLE and the model generates `middle` itself.
    """

    prefix: str
    suffix: str
    middle: str
    path: str = ""
    repo: str = ""
    extra: list[Chunk] = field(default_factory=list)

    def prompt(self) -> str:
        """The inference-time half — what the server sends. No `middle`."""
        return render_prompt(self)

    def text(self) -> str:
        """The training-time whole — prompt plus the target span."""
        return self.prompt() + self.middle


def render_prompt(ex: Example) -> str:
    """Serialise everything the server sends, up to and including FIM_MIDDLE.

    ONE shape, always — verified against llama-server:

        <|repo_name|>myproject
        <|file_sep|>{extra[0].path}
        {extra[0].text}<|file_sep|>{extra[1].path}
        {extra[1].text}<|file_sep|>filename
        <|fim_prefix|>PREFIX<|fim_suffix|>SUFFIX<|fim_middle|>

    The repo header is emitted even with no extra chunks. `myproject` and
    `filename` are llama.cpp's hardcoded placeholders, not stand-ins for
    values we are supposed to fill in — see the module docstring.

    Extra chunk text is concatenated verbatim; the following FILE_SEP token
    provides the boundary, so no separator is inserted after it.

    Note the asymmetry that costs latency: PREFIX precedes SUFFIX in token
    order (PSM). Everything up to the end of PREFIX is a stable KV-cache
    prefix across keystrokes; SUFFIX sits after the cursor's insertion point
    and is therefore re-prefilled on *every* keystroke. That is the whole
    reason n_suffix is tuned down to 10 lines while n_prefix stays at 120.
    """
    out: list[str] = [REPO_NAME + REPO_PLACEHOLDER + "\n"]
    for c in ex.extra:
        out.append(FILE_SEP + c.path + "\n" + c.text)
    out.append(FILE_SEP + FILE_PLACEHOLDER + "\n")
    out.append(FIM_PREFIX + ex.prefix)
    out.append(FIM_SUFFIX + ex.suffix)
    out.append(FIM_MIDDLE)
    return "".join(out)


def strip_specials(s: str) -> str:
    """Remove FIM control tokens from source text.

    Source that contains a literal `<|fim_prefix|>` — this project's own
    tooling and docs do — would inject a spurious control token mid-example
    and corrupt the training signal. Cheap to strip, invisible if skipped.
    """
    for t in SPECIALS:
        s = s.replace(t, "")
    return s


def make_example(
    lines: list[str],
    cut: int,
    n_prefix: int = 120,
    n_suffix: int = 10,
    path: str = "",
    extra: list[Chunk] | None = None,
    col: int | None = None,
) -> Example:
    """Cut a file into a FIM example at line `cut`.

    `lines` keeps line endings (splitlines(keepends=True)). Windows are in
    lines, matching how llama.vscode actually builds requests — see PLAN.txt
    section 5.4 for why lines rather than tokens is the client-side unit.

    `col` optionally splits mid-line: the first `col` characters of the cut
    line join the prefix, and the target becomes the rest of that line. This
    reproduces the realistic invocation point. It matters because accuracy is
    strongly conditioned on how much of the line is already typed — 20.0% exact
    with nothing typed vs 56.8% at three-quarters (PLAN.txt 5.1). Training only
    at col=0 would fit the rarest and hardest case.
    """
    target = lines[cut]
    head, middle = (target[:col], target[col:]) if col else ("", target)

    return Example(
        prefix=strip_specials("".join(lines[max(0, cut - n_prefix):cut]) + head),
        suffix=strip_specials("".join(lines[cut + 1:cut + 1 + n_suffix])),
        middle=strip_specials(middle),
        path=path,
        extra=[Chunk(c.path, strip_specials(c.text)) for c in (extra or [])],
    )
