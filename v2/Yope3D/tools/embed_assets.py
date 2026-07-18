#!/usr/bin/env python3
"""
tools/embed_assets.py
Walks the given input directories and generates files in the output directory
that let the engine look up an asset's bytes by its path (relative to its
input directory) at runtime via getEmbeddedAsset().

Two backends:
  incbin   (default) — emits embedded_assets.s (one `.incbin` directive per
           asset, referencing the file directly) + a slim embedded_assets.cpp
           lookup table. The assembler just copies bytes into the object file;
           the compiler never tokenizes them, so build time doesn't scale with
           asset size. Requires a GAS-compatible assembler (clang/gcc).
  hexarray — the original codegen: every byte becomes a `0x..` literal in a
           C array. Works with any C++ compiler (used for MSVC, whose
           assembler doesn't support `.incbin`), but compile time scales
           badly with asset size — avoid for large meshes/textures.

The first input directory is treated as the "assets root": when --manifest is
given, only paths listed in it (or falling under a listed directory) are
embedded from that root. Every other input directory (e.g. compiled_shaders/)
is always embedded in full, manifest or not.

Usage (called by CMake, not directly):
  python3 embed_assets.py <assets_dir> [<extra_dir> ...] <output_dir>
                           [--backend incbin|hexarray] [--manifest <file>]
"""

import argparse
import os
import sys


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def path_to_symbol(rel_path: str) -> str:
    """Turn a relative path like 'textures/tnail.png' into a valid C identifier."""
    return "asset_" + "".join(c if c.isalnum() else "_" for c in rel_path)


def chunks(lst: list, n: int):
    for i in range(0, len(lst), n):
        yield lst[i : i + n]


def load_manifest(manifest_path: str) -> list:
    """Returns a list of manifest entries (relative paths, '/'-normalized),
    ignoring blank lines and '#' comments."""
    entries = []
    with open(manifest_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            entries.append(line.replace(os.sep, "/").rstrip("/"))
    return entries


def manifest_includes(rel_path: str, manifest_entries: list) -> bool:
    for entry in manifest_entries:
        if rel_path == entry or rel_path.startswith(entry + "/"):
            return True
    return False


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dirs", nargs="+", help="<input_dir1> [input_dir2 ...] <output_dir>")
    parser.add_argument("--backend", choices=["incbin", "hexarray"], default="incbin")
    parser.add_argument("--manifest", default=None,
                         help="If given, only embed manifest-listed paths from the FIRST input dir")
    args = parser.parse_args()

    if len(args.dirs) < 2:
        print("Usage: embed_assets.py <input_dir1> [input_dir2 ...] <output_dir> "
              "[--backend incbin|hexarray] [--manifest <file>]", file=sys.stderr)
        sys.exit(1)

    input_dirs = args.dirs[:-1]
    output_dir = args.dirs[-1]
    os.makedirs(output_dir, exist_ok=True)

    manifest_entries = load_manifest(args.manifest) if args.manifest else None

    # Collect all files from all input directories, sorted for deterministic output.
    # entries: list of (rel_path, symbol, abs_path, size)
    entries = []
    for dir_index, input_dir in enumerate(input_dirs):
        is_assets_root = (dir_index == 0)
        for root, _dirs, files in os.walk(input_dir):
            for filename in sorted(files):
                full_path = os.path.join(root, filename)
                rel_path  = os.path.relpath(full_path, input_dir).replace(os.sep, "/")
                if is_assets_root and manifest_entries is not None:
                    if not manifest_includes(rel_path, manifest_entries):
                        continue
                symbol = path_to_symbol(rel_path)
                size = os.path.getsize(full_path)
                entries.append((rel_path, symbol, os.path.abspath(full_path), size))

    h_path   = os.path.join(output_dir, "embedded_assets.h")
    cpp_path = os.path.join(output_dir, "embedded_assets.cpp")
    asm_path = os.path.join(output_dir, "embedded_assets.s")

    if args.backend == "incbin":
        write_incbin_backend(entries, h_path, cpp_path, asm_path)
    else:
        write_hexarray_backend(entries, h_path, cpp_path)
        # CMake declares embedded_assets.s as a build output regardless of
        # backend (so the custom command's OUTPUT list is stable); keep it
        # present but empty when unused.
        open(asm_path, "w").close()

    total_bytes = sum(size for _, _, _, size in entries)
    dir_names = ", ".join([os.path.basename(d) for d in input_dirs])
    scope = f"manifest ({args.manifest})" if manifest_entries is not None else "full"
    print(f"[embed_assets] Embedded {len(entries)} file(s) from [{dir_names}] "
          f"({scope} scope, {args.backend} backend), {total_bytes:,} bytes total -> {output_dir}")


# ---------------------------------------------------------------------------
# incbin backend — linker-embed, near-zero compile cost regardless of size
# ---------------------------------------------------------------------------

def write_incbin_backend(entries, h_path, cpp_path, asm_path):
    # Darwin's Mach-O ABI prefixes C symbol names with '_' at the assembly
    # level (the C compiler does this automatically when compiling C/C++, but
    # hand-written assembly has to do it explicitly to match).
    sym_prefix = "_" if sys.platform == "darwin" else ""

    with open(asm_path, "w") as s:
        s.write("# Auto-generated by tools/embed_assets.py — do not edit.\n")
        if sys.platform == "darwin":
            s.write(".section __TEXT,__const\n")
        else:
            s.write('.section .rodata,"a"\n')
        for rel_path, symbol, abs_path, _size in entries:
            start = f"{sym_prefix}{symbol}"
            end   = f"{sym_prefix}{symbol}_end"
            s.write(f"# {rel_path}\n")
            s.write(f".globl {start}\n")
            s.write(f"{start}:\n")
            s.write(f'    .incbin "{abs_path}"\n')
            s.write(f".globl {end}\n")
            s.write(f"{end}:\n\n")

    with open(h_path, "w") as h:
        h.write("// Auto-generated by tools/embed_assets.py — do not edit.\n")
        h.write("#pragma once\n")
        h.write("#include <cstddef>\n")
        h.write("#include <cstdint>\n\n")
        h.write("struct EmbeddedAsset {\n")
        h.write("    const uint8_t* data;\n")
        h.write("    std::size_t    size;\n")
        h.write("};\n\n")
        for rel_path, symbol, _abs_path, _size in entries:
            h.write(f"// {rel_path}\n")
            h.write(f'extern "C" const uint8_t {symbol}[];\n')
            h.write(f'extern "C" const uint8_t {symbol}_end[];\n\n')
        h.write("// Returns the asset for 'path' (relative to assets/ root),\n")
        h.write("// or {nullptr, 0} if not found.\n")
        h.write("EmbeddedAsset getEmbeddedAsset(const char* path);\n")

    with open(cpp_path, "w") as cpp:
        cpp.write("// Auto-generated by tools/embed_assets.py — do not edit.\n")
        cpp.write('#include "embedded_assets.h"\n')
        cpp.write("#include <cstring>\n\n")
        cpp.write("EmbeddedAsset getEmbeddedAsset(const char* path) {\n")
        for rel_path, symbol, _abs_path, _size in entries:
            cpp.write(f'    if (std::strcmp(path, "{rel_path}") == 0)')
            cpp.write(f' return {{ {symbol}, static_cast<std::size_t>({symbol}_end - {symbol}) }};\n')
        cpp.write("    return { nullptr, 0 };\n")
        cpp.write("}\n")


# ---------------------------------------------------------------------------
# hexarray backend — original codegen, kept for MSVC (no .incbin support)
# ---------------------------------------------------------------------------

def write_hexarray_backend(entries, h_path, cpp_path):
    with open(h_path, "w") as h:
        h.write("// Auto-generated by tools/embed_assets.py — do not edit.\n")
        h.write("#pragma once\n")
        h.write("#include <cstddef>\n")
        h.write("#include <cstdint>\n\n")
        h.write("struct EmbeddedAsset {\n")
        h.write("    const uint8_t* data;\n")
        h.write("    std::size_t    size;\n")
        h.write("};\n\n")
        for rel_path, symbol, _abs_path, size in entries:
            h.write(f"// {rel_path}\n")
            h.write(f"extern const uint8_t {symbol}[{size}];\n\n")
        h.write("// Returns the asset for 'path' (relative to assets/ root),\n")
        h.write("// or {nullptr, 0} if not found.\n")
        h.write("EmbeddedAsset getEmbeddedAsset(const char* path);\n")

    with open(cpp_path, "w") as cpp:
        cpp.write("// Auto-generated by tools/embed_assets.py — do not edit.\n")
        cpp.write('#include "embedded_assets.h"\n')
        cpp.write("#include <cstring>\n\n")

        for rel_path, symbol, abs_path, size in entries:
            with open(abs_path, "rb") as f:
                data = f.read()
            cpp.write(f"// {rel_path}\n")
            cpp.write(f"const uint8_t {symbol}[{size}] = {{\n")
            hex_bytes = [f"0x{b:02x}" for b in data]
            for row in chunks(hex_bytes, 16):
                cpp.write("    " + ", ".join(row) + ",\n")
            cpp.write("};\n\n")

        cpp.write("EmbeddedAsset getEmbeddedAsset(const char* path) {\n")
        for rel_path, symbol, _abs_path, size in entries:
            cpp.write(f'    if (std::strcmp(path, "{rel_path}") == 0)')
            cpp.write(f' return {{ {symbol}, {size} }};\n')
        cpp.write("    return { nullptr, 0 };\n")
        cpp.write("}\n")


if __name__ == "__main__":
    main()
