#!/usr/bin/env bash
# generate_milestone_reports.sh
#
# Walks every commit on the current branch (oldest -> newest) and builds the
# mac-release binary for each commit into releases/. Restores the original HEAD
# (and any stashed work) on exit.
#
# Per-commit written summaries live in major_updates/ and are authored separately;
# this script only handles the release builds.

set -uo pipefail

# ---------- Configuration ----------
PROJECT_SUBPATH="v2/Yope3D"          # location of CMakeLists/CMakePresets relative to repo root
PRESET="mac-release"                 # cmake configure preset
BUILD_SUBDIR="build/mac-release"     # build tree relative to PROJECT_SUBPATH
BINARY_NAME="yope3d"                 # release executable to capture
RELEASES_DIR_NAME="Yope3D Project Demos"
# -----------------------------------

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"
if [ -z "$REPO_ROOT" ]; then
    echo "error: not inside a git repository" >&2
    exit 1
fi
cd "$REPO_ROOT"

PROJECT_DIR="$REPO_ROOT/$PROJECT_SUBPATH"
RELEASES_DIR="$REPO_ROOT/$RELEASES_DIR_NAME"
LOG_DIR="/tmp/yope3d_build_logs"

mkdir -p "$RELEASES_DIR" "$LOG_DIR"

# Remember where we started so we can restore.
ORIGINAL_REF="$(git symbolic-ref --short -q HEAD || git rev-parse HEAD)"

# Stash any uncommitted work (including untracked files) so checkouts succeed.
STASHED=0
if [ -n "$(git status --porcelain)" ]; then
    echo ">> Stashing local changes before walking history..."
    if git stash push --include-untracked -m "milestone-build auto-stash" >/dev/null; then
        STASHED=1
    else
        echo "error: failed to stash local changes; aborting" >&2
        exit 1

    fi
fi

cleanup() {
    echo ""
    echo ">> Restoring original state ($ORIGINAL_REF)..."
    git checkout --quiet "$ORIGINAL_REF" 2>/dev/null || true
    if [ "$STASHED" -eq 1 ]; then
        git stash pop --quiet 2>/dev/null || echo "   (could not auto-pop stash; check 'git stash list')"
    fi
}
trap cleanup EXIT INT TERM

# Collect 2026 commits on the current branch, oldest first.
COMMITS=()
while IFS= read -r hash; do
    COMMITS+=("$hash")
done < <(git log --reverse --format=%H --after="2025-12-31" "$ORIGINAL_REF")
TOTAL="${#COMMITS[@]}"
echo ">> Found $TOTAL commits on '$ORIGINAL_REF' (2026 only)"

idx=0
for hash in "${COMMITS[@]}"; do
    idx=$((idx + 1))
    short="$(git rev-parse --short "$hash")"
    subject="$(git log -1 --format=%s "$hash")"
    date_iso="$(git log -1 --format=%cs "$hash")"

    # Filesystem-safe slug from the subject.
    slug="$(printf '%s' "$subject" | tr '[:upper:]' '[:lower:]' | tr -c 'a-z0-9._-' '_' | sed 's/__*/_/g; s/^_//; s/_$//')"
    slug="${slug:0:20}"
    base_name="$(printf '%s' "$short")"

    echo ""
    echo "=== [$idx/$TOTAL] $short  $date_iso  $subject ==="

    if ! git checkout --quiet --detach "$hash"; then
        echo "   ! checkout failed, skipping build"
        continue
    fi

    if [ ! -f "$PROJECT_DIR/CMakePresets.json" ]; then
        echo "   (no CMakePresets.json at this commit  build skipped)"
        continue
    fi

    build_log="$LOG_DIR/${base_name}.log"
    rm -rf "$PROJECT_DIR/$BUILD_SUBDIR"

    echo "   building... (log: ${build_log#$REPO_ROOT/})"
    if (
        cd "$PROJECT_DIR" \
            && cmake --preset "$PRESET" \
            && cmake --build "$BUILD_SUBDIR" --config Release
    ) > "$build_log" 2>&1; then
        bin_src="$PROJECT_DIR/$BUILD_SUBDIR/$BINARY_NAME"
        if [ -f "$bin_src" ]; then
            cp "$bin_src" "$RELEASES_DIR/${base_name}"
            echo "    built -> ${RELEASES_DIR_NAME}/${base_name}"
        else
            echo "   ! build succeeded but '$BINARY_NAME' not found at $bin_src"
            : > "$RELEASES_DIR/${base_name}.MISSING_BINARY"
        fi
    else
        echo "   x build failed (see log)"
        : > "$RELEASES_DIR/${base_name}.FAILED"
    fi
done

echo ""
echo ">> Done. Binaries in $RELEASES_DIR_NAME/."
