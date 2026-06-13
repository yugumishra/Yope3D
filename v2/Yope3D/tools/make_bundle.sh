#!/usr/bin/env bash
# make_bundle.sh — assemble a standalone Yope3D.app from a compiled release binary.
#
# Usage:  tools/make_bundle.sh <binary_path> [output_dir]
# Example: tools/make_bundle.sh build/mac-release/yope3d dist
#
# Requires: otool, install_name_tool, rsync (all standard on macOS)

set -euo pipefail

BINARY="${1:?Usage: make_bundle.sh <binary_path> [output_dir]}"
OUTDIR="${2:-.}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

APP="$OUTDIR/Yope3D.app"
MACOS_DIR="$APP/Contents/MacOS"
FRAMEWORKS_DIR="$APP/Contents/Frameworks"
RESOURCES_DIR="$APP/Contents/Resources"
PLIST_SRC="$REPO_ROOT/cmake/Info.plist"

if [ ! -f "$BINARY" ]; then echo "Error: binary not found: $BINARY"; exit 1; fi
if [ ! -f "$PLIST_SRC" ]; then echo "Error: $PLIST_SRC missing — run cmake first."; exit 1; fi

echo "→ Assembling $APP"
rm -rf "$APP"
mkdir -p "$MACOS_DIR" "$FRAMEWORKS_DIR" "$RESOURCES_DIR"

cp "$PLIST_SRC" "$APP/Contents/Info.plist"
cp "$BINARY"    "$MACOS_DIR/yope3d"
chmod +x "$MACOS_DIR/yope3d"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Collect rpaths embedded in a binary (one per line)
get_rpaths() {
    otool -l "$1" 2>/dev/null \
        | awk '/LC_RPATH/{found=1} found && /[[:space:]]path /{print $2; found=0}'
}

# Collect dylib references from a binary (skip the first line = self-ID)
get_deps() {
    otool -L "$1" 2>/dev/null | tail -n +2 | awk '{print $1}'
}

# Is this an OS-provided lib we should never bundle?
is_system() {
    case "$1" in
        /usr/lib/*|/System/*|/usr/local/lib/libMoltenVK*) return 0 ;;
        # libMoltenVK is dlopen'd from the ICD JSON — we copy it explicitly below
    esac
    return 1
}

# Resolve an @rpath/foo.dylib reference to an absolute path using a list of
# rpaths (one per line in $2 file). Prints the result; returns 1 if not found.
resolve_dep() {
    local ref="$1"
    local rpaths_file="$2"

    case "$ref" in
        @rpath/*)
            local name="${ref#@rpath/}"
            while IFS= read -r rp; do
                [ -f "$rp/$name" ] && echo "$rp/$name" && return 0
            done < "$rpaths_file"
            return 1
            ;;
        @loader_path/*|@executable_path/*)
            return 1
            ;;
        *)
            echo "$ref"
            return 0
            ;;
    esac
}

# ---------------------------------------------------------------------------
# Dylib bundling
# Tracks what's already been copied to avoid double-processing.
# ---------------------------------------------------------------------------

BUNDLED_NAMES=""  # space-separated list of basenames already handled

already_bundled() { [[ " $BUNDLED_NAMES " == *" $1 "* ]]; }

# Copy a resolved dylib path into Frameworks/, fix its ID, and queue its deps.
# $1 = absolute source path, $2 = rpaths_file used to resolve $1's own deps
bundle_one() {
    local src="$1"
    local parent_rpaths_file="$2"
    local name; name="$(basename "$src")"

    already_bundled "$name" && return
    BUNDLED_NAMES="$BUNDLED_NAMES $name"

    echo "  + $name"
    cp "$src" "$FRAMEWORKS_DIR/$name"
    install_name_tool -id "@rpath/$name" "$FRAMEWORKS_DIR/$name" 2>/dev/null || true

    # Collect rpaths embedded in this lib (may be empty)
    local lib_rpaths_file; lib_rpaths_file="$(mktemp)"
    get_rpaths "$src" > "$lib_rpaths_file"
    # Append parent rpaths as fallback
    cat "$parent_rpaths_file" >> "$lib_rpaths_file"

    # Walk this lib's deps
    while IFS= read -r dep; do
        # Skip self-reference
        [ "$(basename "$dep")" = "$name" ] && continue

        local resolved
        resolved=$(resolve_dep "$dep" "$lib_rpaths_file") || continue
        is_system "$resolved" && continue
        [ -f "$resolved" ] || continue

        local dep_name; dep_name="$(basename "$resolved")"

        # Fix up the reference inside the copy we already wrote to Frameworks/
        install_name_tool -change "$dep" "@rpath/$dep_name" \
            "$FRAMEWORKS_DIR/$name" 2>/dev/null || true

        # Recurse
        bundle_one "$resolved" "$lib_rpaths_file"
    done < <(get_deps "$src")

    rm -f "$lib_rpaths_file"
}

# ---------------------------------------------------------------------------
# Walk the main binary's deps
# ---------------------------------------------------------------------------

echo "  Bundling dylibs..."

BINARY_RPATHS_FILE="$(mktemp)"
get_rpaths "$MACOS_DIR/yope3d" > "$BINARY_RPATHS_FILE"

while IFS= read -r dep; do
    resolved=$(resolve_dep "$dep" "$BINARY_RPATHS_FILE") || continue
    is_system "$resolved" && continue
    [ -f "$resolved" ] || continue

    dep_name="$(basename "$resolved")"
    # The binary already uses @rpath/name — no -change needed, just ensure
    # the lib lands in Frameworks/ and the rpath is updated below.
    bundle_one "$resolved" "$BINARY_RPATHS_FILE"
done < <(get_deps "$MACOS_DIR/yope3d")

# MoltenVK is dlopen'd by the Vulkan loader at runtime (not in otool -L output),
# so we must copy it explicitly.
MOLTENVK_SRC="/usr/local/lib/libMoltenVK.dylib"
if [ -f "$MOLTENVK_SRC" ]; then
    bundle_one "$MOLTENVK_SRC" "$BINARY_RPATHS_FILE"
else
    echo "  Warning: libMoltenVK.dylib not found at $MOLTENVK_SRC"
fi

rm -f "$BINARY_RPATHS_FILE"

# Replace the build-time rpaths with the bundle-relative one so the binary
# finds libs in Frameworks/ and cannot fall back to system paths.
while IFS= read -r rp; do
    install_name_tool -delete_rpath "$rp" "$MACOS_DIR/yope3d" 2>/dev/null || true
done < <(get_rpaths "$MACOS_DIR/yope3d")
install_name_tool -add_rpath "@executable_path/../Frameworks" "$MACOS_DIR/yope3d"

# ---------------------------------------------------------------------------
# Python stdlib
# Derive prefix from whichever libpython the binary was linked against.
# ---------------------------------------------------------------------------

PYTHON_DYLIB=$(get_deps "$MACOS_DIR/yope3d" | grep "libpython" | head -1 || true)
# After rpath substitution the dep shows as @rpath/libpython... — use the copy
# we just made in Frameworks/ to derive the version.
if [ -z "$PYTHON_DYLIB" ]; then
    PYTHON_DYLIB=$(ls "$FRAMEWORKS_DIR"/libpython*.dylib 2>/dev/null | head -1 || true)
fi

if [ -n "$PYTHON_DYLIB" ]; then
    PYTHON_VER=$(basename "$PYTHON_DYLIB" | grep -o '[0-9]\+\.[0-9]\+' | head -1)
    # Find the prefix: look up from our copied dylib's original location via
    # the rpath that resolved it (stored in the system Frameworks).
    PYTHON_PREFIX=""
    for candidate in \
        "/opt/homebrew/Caskroom/miniconda/base" \
        "/opt/homebrew" \
        "/usr/local" \
        "$(python3 -c 'import sys; print(sys.prefix)' 2>/dev/null || true)"
    do
        [ -d "$candidate/lib/python$PYTHON_VER" ] && PYTHON_PREFIX="$candidate" && break
    done

    if [ -n "$PYTHON_PREFIX" ] && [ -d "$PYTHON_PREFIX/lib/python$PYTHON_VER" ]; then
        STDLIB_SRC="$PYTHON_PREFIX/lib/python$PYTHON_VER"
        STDLIB_DST="$RESOURCES_DIR/python/lib/python$PYTHON_VER"
        echo "  Bundling Python $PYTHON_VER stdlib from $STDLIB_SRC..."
        mkdir -p "$STDLIB_DST"
        rsync -a --quiet \
            --exclude='__pycache__'   \
            --exclude='test'          \
            --exclude='tests'         \
            --exclude='tkinter'       \
            --exclude='idlelib'       \
            --exclude='lib2to3'       \
            --exclude='turtledemo'    \
            --exclude='ensurepip'     \
            --exclude='distutils'     \
            --exclude='site-packages' \
            "$STDLIB_SRC/" "$STDLIB_DST/"
        "$PYTHON_PREFIX/bin/python$PYTHON_VER" \
            -m compileall -q "$STDLIB_DST" 2>/dev/null || true
    else
        echo "  Warning: could not locate Python $PYTHON_VER stdlib — behaviors may fail."
    fi
else
    echo "  Warning: no libpython found — Python behaviors will not work."
fi

# ---------------------------------------------------------------------------
# Vulkan ICD JSON
# Tells the Vulkan loader where to find MoltenVK inside the bundle.
# Path is relative to this JSON file's directory.
# ---------------------------------------------------------------------------

ICD_DIR="$RESOURCES_DIR/vulkan/icd.d"
mkdir -p "$ICD_DIR"
cat > "$ICD_DIR/MoltenVK_icd.json" << 'EOF'
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../../Frameworks/libMoltenVK.dylib",
        "api_version": "1.3.0"
    }
}
EOF

# ---------------------------------------------------------------------------
# Scripts and config
# ---------------------------------------------------------------------------

echo "  Copying scripts..."
cp -r "$REPO_ROOT/scripts" "$RESOURCES_DIR/scripts"

echo "  Copying yope3d.cfg..."
cp "$REPO_ROOT/yope3d.cfg" "$RESOURCES_DIR/yope3d.cfg"

# ---------------------------------------------------------------------------

echo ""
echo "✓ Done: $APP"
echo "  Run with: open \"$APP\""
echo "  Or:       \"$MACOS_DIR/yope3d\""
